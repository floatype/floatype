// floatype_scorer: the process that converts readings with zenz.
//
// The Mozc converter starts it with a socket as stdin and stdout and talks the
// line protocol in protocol.h. Requests that are already superseded when the
// scorer gets to them are skipped. The scorer exits when stdin closes, and then
// prints its load time and memory footprint as one JSON object to stderr.
//
// Usage: floatype_scorer --model <gguf> [--threads N] [--show_tokens]
// The backend is fixed at build time (GGML_METAL).

#include <mach/mach.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gguf.h"
#include "llama.h"
#include "protocol.h"
#include "zenz.h"

namespace {

// zenz's GGUF names its pre-tokenizer "gpt2-small-japanese-char", which
// upstream llama.cpp does not know. It uses the same regex as the GPT-2
// pre-tokenizer (azooKey/llama.cpp only adds the name), so such models are
// loaded as "gpt-2".
constexpr char kPreTokenizerKey[] = "tokenizer.ggml.pre";
constexpr char kZenzPreTokenizer[] = "gpt2-small-japanese-char";

// Conversions are never longer than their readings in AJIMEE-Bench; the margin
// covers characters outside the vocabulary, which take one token per byte.
constexpr int kOutputTokenMargin = 8;

std::string ReadPreTokenizer(const std::string& model_path) {
  gguf_init_params params = {/*no_alloc=*/true, /*ctx=*/nullptr};
  gguf_context* gguf = gguf_init_from_file(model_path.c_str(), params);
  if (gguf == nullptr) {
    return "";
  }
  std::string value;
  const int64_t key_id = gguf_find_key(gguf, kPreTokenizerKey);
  if (key_id >= 0 && gguf_get_kv_type(gguf, key_id) == GGUF_TYPE_STRING) {
    value = gguf_get_val_str(gguf, key_id);
  }
  gguf_free(gguf);
  return value;
}

// Returns a kv_overrides array (terminated by an empty key) for
// llama_model_params.
std::vector<llama_model_kv_override> PreTokenizerOverrides(
    const std::string& model_path) {
  std::vector<llama_model_kv_override> overrides;
  if (ReadPreTokenizer(model_path) == kZenzPreTokenizer) {
    llama_model_kv_override o{};
    o.tag = LLAMA_KV_OVERRIDE_TYPE_STR;
    std::snprintf(o.key, sizeof(o.key), "%s", kPreTokenizerKey);
    std::snprintf(o.val_str, sizeof(o.val_str), "%s", "gpt-2");
    overrides.push_back(o);
  }
  overrides.push_back(
      llama_model_kv_override{});  // Terminator: key[0] == '\0'.
  return overrides;
}

using Clock = std::chrono::steady_clock;

double MillisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

void LogErrorsOnly(ggml_log_level level, const char* text,
                   void* /*user_data*/) {
  if (level >= GGML_LOG_LEVEL_ERROR) {
    std::fputs(text, stderr);
  }
}

std::vector<llama_token> Tokenize(const llama_vocab* vocab,
                                  std::string_view text) {
  // add_special follows the GGUF (zenz sets add_bos_token=false), as AzooKey
  // does.
  std::vector<llama_token> tokens(text.size() + 2);
  int n = llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()),
                         tokens.data(), static_cast<int32_t>(tokens.size()),
                         /*add_special=*/true,
                         /*parse_special=*/false);
  if (n < 0) {
    tokens.resize(-n);
    n = llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()),
                       tokens.data(), static_cast<int32_t>(tokens.size()), true,
                       false);
  }
  tokens.resize(n < 0 ? 0 : n);
  return tokens;
}

struct Completion {
  std::string text;
  const char* stop = "cap";
};

// Greedy decoding until an end-of-generation token, a zenz tag or max_tokens.
// The draft, Mozc's conversion, is checked first in the same pass as the
// prompt: zenz's greedy choice at each draft token either matches it, and the
// token is taken without a pass of its own, or ends the draft there. The text
// is what greedy decoding alone gives; only the number of passes drops.
//
// `cached` holds the tokens whose keys and values are in the context from the
// last request. The part the new prompt shares with them is kept: while the
// user types on, requests share the context and the start of the reading.
Completion Complete(llama_context* ctx, const llama_vocab* vocab,
                    llama_sampler* greedy,
                    const std::vector<llama_token>& prompt,
                    std::vector<llama_token> draft, int max_tokens,
                    std::vector<llama_token>& cached) {
  llama_memory_t memory = llama_get_memory(ctx);
  Completion out;
  if (prompt.empty()) {
    out.stop = "error";
    return out;
  }
  if (draft.size() > static_cast<size_t>(max_tokens)) {
    draft.resize(max_tokens);
  }
  // At least the prompt's last token is decoded again for its logits.
  size_t kept = 0;
  while (kept + 1 < prompt.size() && kept < cached.size() &&
         cached[kept] == prompt[kept]) {
    ++kept;
  }
  llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(kept), -1);
  cached.resize(kept);
  const size_t n = prompt.size() - kept + draft.size();
  llama_batch batch = llama_batch_init(static_cast<int32_t>(n), 0, 1);
  for (size_t i = 0; i < n; ++i) {
    const size_t position = kept + i;
    batch.token[i] = position < prompt.size() ? prompt[position]
                                              : draft[position - prompt.size()];
    batch.pos[i] = static_cast<llama_pos>(position);
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = 0;
    // Logits after the prompt and after each draft token.
    batch.logits[i] = position + 1 >= prompt.size();
    cached.push_back(batch.token[i]);
  }
  batch.n_tokens = static_cast<int32_t>(n);
  const int32_t decoded = llama_decode(ctx, batch);
  llama_batch_free(batch);
  if (decoded != 0) {
    llama_memory_clear(memory, /*data=*/true);
    cached.clear();
    out.stop = "error";
    return out;
  }
  // The token zenz chooses after the prompt and each accepted draft token.
  size_t accepted = 0;
  llama_token token = 0;
  for (;; ++accepted) {
    token = llama_sampler_sample(
        greedy, ctx, static_cast<int32_t>(prompt.size() - 1 - kept + accepted));
    if (accepted == draft.size() || token != draft[accepted]) {
      break;
    }
    char piece[128];
    const int len =
        llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
    if (len > 0) {
      out.text.append(piece, len);
    }
  }
  // The cache keeps the prompt and the accepted draft only.
  llama_memory_seq_rm(memory, 0,
                      static_cast<llama_pos>(prompt.size() + accepted), -1);
  cached.resize(prompt.size() + accepted);
  for (int i = static_cast<int>(accepted); i < max_tokens; ++i) {
    if (i > static_cast<int>(accepted)) {
      token = llama_sampler_sample(greedy, ctx, -1);
    }
    if (llama_vocab_is_eog(vocab, token)) {
      out.stop = "eog";
      return out;
    }
    char piece[128];
    const int len =
        llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
    if (len > 0) {
      out.text.append(piece, len);
    }
    if (const size_t stop = floatype::zenz::FindStop(out.text);
        stop != std::string::npos) {
      out.text.resize(stop);
      out.stop = "tag";
      return out;
    }
    if (llama_decode(ctx, llama_batch_get_one(&token, 1)) != 0) {
      llama_memory_clear(memory, /*data=*/true);
      cached.clear();
      out.stop = "error";
      return out;
    }
    cached.push_back(token);
  }
  return out;
}

bool WriteAll(int fd, std::string_view data) {
  while (!data.empty()) {
    const ssize_t written = write(fd, data.data(), data.size());
    if (written < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data.remove_prefix(written);
  }
  return true;
}

double ToMegabytes(double bytes) { return bytes / (1024.0 * 1024.0); }

}  // namespace

int main(int argc, char** argv) {
  std::string model_path;
  int threads = 4;
  bool show_tokens = false;
  bool reuse_cache = true;
  // 0 runs until the client closes the connection.
  int idle_exit_seconds = 0;
  // Repacking the weights for the CPU's fast kernels copies the model into
  // memory (about 64MB); without it zenz takes about twice as long.
  bool repack = true;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--model" && i + 1 < argc) {
      model_path = argv[++i];
    } else if (arg == "--threads" && i + 1 < argc) {
      threads = std::atoi(argv[++i]);
    } else if (arg == "--show_tokens") {
      show_tokens = true;
    } else if (arg == "--no_cache_reuse") {  // For measuring.
      reuse_cache = false;
    } else if (arg == "--idle_exit_seconds" && i + 1 < argc) {
      idle_exit_seconds = std::atoi(argv[++i]);
    } else if (arg == "--no_repack") {
      repack = false;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
      return 2;
    }
  }
  if (model_path.empty() || threads < 1) {
    std::fprintf(stderr,
                 "usage: floatype_scorer --model <gguf> [--threads N] "
                 "[--idle_exit_seconds N] [--no_repack] [--show_tokens] "
                 "[--no_cache_reuse]\n");
    return 2;
  }
  // A closed socket must end the loop through write() errors, not a signal.
  std::signal(SIGPIPE, SIG_IGN);

  llama_log_set(LogErrorsOnly, nullptr);
  llama_backend_init();

  const Clock::time_point load_start = Clock::now();
  llama_model_params model_params = llama_model_default_params();
  const std::vector<llama_model_kv_override> overrides =
      PreTokenizerOverrides(model_path);
  model_params.kv_overrides = overrides.data();
  model_params.use_extra_bufts = repack;
  // The default callback prints progress dots to stderr.
  model_params.progress_callback = [](float, void*) { return true; };
  llama_model* model =
      llama_model_load_from_file(model_path.c_str(), model_params);
  if (model == nullptr) {
    std::fprintf(stderr, "failed to load model: %s\n", model_path.c_str());
    return 1;
  }
  const llama_vocab* vocab = llama_model_get_vocab(model);

  llama_context_params context_params = llama_context_default_params();
  context_params.n_ctx = 512;
  context_params.n_batch = 512;
  context_params.n_threads = threads;
  context_params.n_threads_batch = threads;
  context_params.no_perf = true;
  llama_context* ctx = llama_init_from_model(model, context_params);
  if (ctx == nullptr) {
    std::fprintf(stderr, "failed to create a context\n");
    llama_model_free(model);
    return 1;
  }
  llama_sampler* greedy = llama_sampler_init_greedy();
  std::vector<llama_token> cached;
  const double load_ms = MillisecondsSince(load_start);

  std::string buffer;
  std::string line;
  char chunk[1 << 16];
  while (true) {
    // With no request for a while, the process ends to give its memory back;
    // the client starts it again when the user types.
    if (idle_exit_seconds > 0) {
      pollfd input{STDIN_FILENO, POLLIN, 0};
      const int ready = poll(&input, 1, idle_exit_seconds * 1000);
      if (ready < 0 && errno == EINTR) continue;
      if (ready == 0) break;
    }
    // read() returns what has arrived so far, so a burst of requests is seen
    // together and only the latest one is converted.
    const ssize_t n = read(STDIN_FILENO, chunk, sizeof(chunk));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    buffer.append(chunk, n);
    if (floatype::scorer::TakeLatestLine(&buffer, &line) == 0) continue;
    const std::optional<floatype::scorer::Request> request =
        floatype::scorer::ParseRequest(line);
    if (!request.has_value()) continue;

    const Clock::time_point start = Clock::now();
    const std::vector<llama_token> prompt = Tokenize(
        vocab,
        floatype::zenz::BuildPrompt(request->left_context, request->reading));
    const int max_tokens =
        static_cast<int>(Tokenize(vocab, request->reading).size()) +
        kOutputTokenMargin;
    if (show_tokens) {
      for (const llama_token token : prompt) {
        char piece[128];
        const int len =
            llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, true);
        std::fprintf(stderr, "%d:[%.*s] ", token, len > 0 ? len : 0, piece);
      }
      std::fputc('\n', stderr);
    }
    // A request that does not fit fails here instead of aborting inside
    // llama_decode.
    std::vector<llama_token> draft;
    if (!request->draft.empty()) {
      draft = Tokenize(vocab, request->draft);
    }
    const bool fits = prompt.size() <= llama_n_batch(ctx) &&
                      prompt.size() + max_tokens <= llama_n_ctx(ctx);
    // A draft that does not fit in one pass is left out.
    if (prompt.size() + draft.size() > llama_n_batch(ctx)) {
      draft.clear();
    }
    if (!reuse_cache) {
      cached.clear();
    }
    const Completion completion =
        fits ? Complete(ctx, vocab, greedy, prompt, std::move(draft),
                        max_tokens, cached)
             : Completion{"", "error"};
    const floatype::scorer::Response response{
        request->id, completion.text, completion.stop,
        static_cast<uint64_t>(MillisecondsSince(start) * 1000)};
    if (!WriteAll(STDOUT_FILENO, floatype::scorer::FormatResponse(response))) {
      break;
    }
  }

  // phys_footprint is what Activity Monitor shows as Memory; it includes GPU
  // allocations and only the touched pages of the mapped model.
  task_vm_info_data_t vm_info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  const bool has_vm_info = task_info(mach_task_self(), TASK_VM_INFO,
                                     reinterpret_cast<task_info_t>(&vm_info),
                                     &count) == KERN_SUCCESS;
  std::fprintf(
      stderr,
      "{\"load_ms\":%.1f,\"threads\":%d,\"phys_footprint_mb\":%.1f,"
      "\"peak_phys_footprint_mb\":%.1f}\n",
      load_ms, threads,
      has_vm_info ? ToMegabytes(vm_info.phys_footprint) : -1.0,
      has_vm_info ? ToMegabytes(vm_info.ledger_phys_footprint_peak) : -1.0);

  llama_sampler_free(greedy);
  llama_free(ctx);
  llama_model_free(model);
  llama_backend_free();
  return 0;
}
