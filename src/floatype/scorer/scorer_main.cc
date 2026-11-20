// floatype_scorer: the process that converts readings with zenz.
//
// The Mozc converter starts it with a socket as stdin and stdout and talks the
// line protocol in protocol.h. Requests that are already superseded when the
// scorer gets to them are skipped. The scorer exits when stdin closes, and then
// prints its load time and memory footprint as one JSON object to stderr.
//
// Usage: floatype_scorer --model <gguf> [--repacked <file>] [--threads N]
//            [--show_tokens]
//        floatype_scorer --model <gguf> --write_repacked <file>
// The backend is fixed at build time (GGML_METAL).
//
// llama.cpp repacks the model's weights for the CPU's fast kernels when it
// loads them, into a copy as large as the model (about 64MB) in memory of the
// process's own. With --repacked the scorer maps a file that holds that copy
// already: the same weights, so the same answers at the same speed, but a
// mapped file is no memory of the process's own and nothing is repacked at
// the start. --write_repacked makes the file, at build time. Which layout the
// kernels want is fixed when llama.cpp is compiled, so one file serves every
// Mac the scorer runs on.

#include <fcntl.h>
#include <mach/mach.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ggml-cpu.h"
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

// The most deviations from zenz's own answer tried for a request with
// exclusions; each costs about as much as a short answer.
constexpr size_t kMaxDeviations = 8;

struct Completion {
  std::string text;
  const char* stop = "cap";
  // The tokens taken: the accepted draft and the generated ones, including
  // the end-of-generation or tag token that ended them.
  std::vector<llama_token> tokens;
};

std::string Piece(const llama_vocab* vocab, llama_token token) {
  char piece[128];
  const int len =
      llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
  return len > 0 ? std::string(piece, len) : "";
}

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
    out.text += Piece(vocab, token);
    out.tokens.push_back(token);
  }
  // The cache keeps the prompt and the accepted draft only.
  llama_memory_seq_rm(memory, 0,
                      static_cast<llama_pos>(prompt.size() + accepted), -1);
  cached.resize(prompt.size() + accepted);
  for (int i = static_cast<int>(accepted); i < max_tokens; ++i) {
    if (i > static_cast<int>(accepted)) {
      token = llama_sampler_sample(greedy, ctx, -1);
    }
    out.tokens.push_back(token);
    if (llama_vocab_is_eog(vocab, token)) {
      out.stop = "eog";
      return out;
    }
    out.text += Piece(vocab, token);
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

bool IsExcluded(const std::vector<std::string>& excluded,
                std::string_view text) {
  const std::string folded = floatype::zenz::FoldWidth(text);
  return std::any_of(excluded.begin(), excluded.end(),
                     [&](const std::string& conversion) {
                       return floatype::zenz::FoldWidth(conversion) == folded;
                     });
}

// A place where zenz's answer could go another way: instead of the answer's
// token at `position`, zenz's runner-up there, `token`, which scored `margin`
// lower (a logit gap, which is also the log-probability gap).
struct Fork {
  size_t position = 0;
  float margin = 0;
  llama_token token = 0;
  // The answer through the fork, once decoded.
  std::optional<Completion> answer;
};

// zenz's answer to `prompt` and the forks along it. Each "not this" for the
// same conversion asks again, so the forks are found and decoded only once.
struct Answers {
  std::vector<llama_token> prompt;
  Completion best;
  int max_tokens = 0;
  // Closest first; nullopt until a request with exclusions needs them.
  std::optional<std::vector<Fork>> forks;
};

// Scores the best answer in one pass and returns the closest kMaxDeviations
// of its forks that keep its length; nullopt when the pass fails. `cached` as
// in Complete().
std::optional<std::vector<Fork>> FindForks(llama_context* ctx,
                                           const llama_vocab* vocab,
                                           const Answers& answers,
                                           std::vector<llama_token>& cached) {
  const std::vector<llama_token>& prompt = answers.prompt;
  const std::vector<llama_token>& path = answers.best.tokens;
  llama_memory_t memory = llama_get_memory(ctx);
  size_t kept = 0;
  while (kept + 1 < prompt.size() && kept < cached.size() &&
         cached[kept] == prompt[kept]) {
    ++kept;
  }
  llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(kept), -1);
  cached.resize(kept);
  // Logits after the prompt and after each token of the path but its last.
  const size_t n = prompt.size() - kept + path.size() - 1;
  llama_batch batch = llama_batch_init(static_cast<int32_t>(n), 0, 1);
  for (size_t i = 0; i < n; ++i) {
    const size_t position = kept + i;
    batch.token[i] = position < prompt.size() ? prompt[position]
                                              : path[position - prompt.size()];
    batch.pos[i] = static_cast<llama_pos>(position);
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = 0;
    batch.logits[i] = position + 1 >= prompt.size();
    cached.push_back(batch.token[i]);
  }
  batch.n_tokens = static_cast<int32_t>(n);
  const int32_t decoded = llama_decode(ctx, batch);
  llama_batch_free(batch);
  if (decoded != 0) {
    llama_memory_clear(memory, /*data=*/true);
    cached.clear();
    return std::nullopt;
  }
  // The token that ended the answer is no part of its text.
  const size_t text_tokens = std::string_view(answers.best.stop) == "cap"
                                 ? path.size()
                                 : path.size() - 1;
  const int32_t n_vocab = llama_vocab_n_tokens(vocab);
  std::vector<Fork> forks;
  for (size_t i = 0;
       i < text_tokens && static_cast<int>(i) < answers.max_tokens; ++i) {
    const float* logits = llama_get_logits_ith(
        ctx, static_cast<int32_t>(prompt.size() - 1 - kept + i));
    llama_token best = -1;
    for (llama_token token = 0; token < n_vocab; ++token) {
      if (token != path[i] && (best < 0 || logits[token] > logits[best])) {
        best = token;
      }
    }
    // Ending the answer earlier or later changes its length, and the answer
    // must still spell the whole reading in Mozc's segments.
    if (best >= 0 && !llama_vocab_is_eog(vocab, best) &&
        floatype::zenz::FindStop(Piece(vocab, best)) == std::string::npos) {
      forks.push_back({i, logits[path[i]] - logits[best], best});
    }
  }
  std::stable_sort(
      forks.begin(), forks.end(),
      [](const Fork& a, const Fork& b) { return a.margin < b.margin; });
  if (forks.size() > kMaxDeviations) {
    forks.resize(kMaxDeviations);
  }
  return forks;
}

// zenz's next best answer when its own is excluded: the runner-up at one fork
// of it, then greedy decoding, trying the closest forks first. The stop is
// "exhausted" when no fork gives an answer that is not excluded.
Completion NextBest(llama_context* ctx, const llama_vocab* vocab,
                    llama_sampler* greedy, Answers& answers,
                    const std::vector<std::string>& excluded,
                    std::vector<llama_token>& cached, bool show_tokens) {
  const std::vector<llama_token>& path = answers.best.tokens;
  if (path.empty()) {
    return {"", "exhausted"};
  }
  if (!answers.forks.has_value()) {
    answers.forks = FindForks(ctx, vocab, answers, cached);
    if (!answers.forks.has_value()) {
      return {"", "error"};
    }
  }
  const size_t text_tokens = std::string_view(answers.best.stop) == "cap"
                                 ? path.size()
                                 : path.size() - 1;
  for (Fork& fork : *answers.forks) {
    if (!fork.answer.has_value()) {
      std::vector<llama_token> start = answers.prompt;
      start.insert(start.end(), path.begin(), path.begin() + fork.position);
      start.push_back(fork.token);
      // The rest of the answer often follows again, so it is the draft.
      Completion next = Complete(
          ctx, vocab, greedy, start,
          std::vector<llama_token>(path.begin() + fork.position + 1,
                                   path.begin() + text_tokens),
          answers.max_tokens - static_cast<int>(fork.position) - 1, cached);
      if (std::string_view(next.stop) == "error") {
        return next;
      }
      std::string text;
      for (size_t i = 0; i < fork.position; ++i) {
        text += Piece(vocab, path[i]);
      }
      next.text = text + Piece(vocab, fork.token) + next.text;
      if (show_tokens) {
        std::fprintf(stderr, "fork %zu margin %.3f: %s %s\n", fork.position,
                     fork.margin, next.text.c_str(), next.stop);
      }
      fork.answer = std::move(next);
    }
    if (!fork.answer->text.empty() &&
        !IsExcluded(excluded, fork.answer->text)) {
      return *fork.answer;
    }
  }
  return {"", "exhausted"};
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

// The file of repacked weights, to map (--repacked) or to make
// (--write_repacked): the buffer llama.cpp repacks into, then kTrailerSize
// bytes that say which model it was made from.
struct RepackedFile {
  std::string path;
  std::string model_path;
  bool write = false;
  // Set once llama.cpp has taken the mapping for its buffer.
  void* memory = nullptr;
  size_t size = 0;
  // llama.cpp asked for a buffer; a second one is allocated as usual.
  bool asked = false;
};

constexpr size_t kTrailerSize = 16;

std::string Temporary(const RepackedFile& file) { return file.path + ".tmp"; }

// The trailer for the model at `path`: a mark, and a fingerprint of the
// model's size and of its start, middle and end (FNV-1a). It keeps another
// model's weights, which may be as large, from being taken for this one's.
std::optional<std::array<char, kTrailerSize>> Trailer(const std::string& path) {
  constexpr off_t kBlock = 1 << 16;
  const int fd = open(path.c_str(), O_RDONLY);
  struct stat status;
  if (fd < 0 || fstat(fd, &status) != 0) {
    if (fd >= 0) {
      close(fd);
    }
    return std::nullopt;
  }
  uint64_t hash = 14695981039346656037ull;
  const auto mix = [&hash](const char* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      hash = (hash ^ static_cast<unsigned char>(data[i])) * 1099511628211ull;
    }
  };
  const uint64_t model_size = static_cast<uint64_t>(status.st_size);
  mix(reinterpret_cast<const char*>(&model_size), sizeof(model_size));
  std::vector<char> block(kBlock);
  bool read_all = true;
  for (const off_t offset :
       {off_t{0}, std::max<off_t>(0, status.st_size / 2 - kBlock / 2),
        std::max<off_t>(0, status.st_size - kBlock)}) {
    const ssize_t got = pread(fd, block.data(), block.size(), offset);
    read_all = read_all && got >= 0;
    if (got > 0) {
      mix(block.data(), static_cast<size_t>(got));
    }
  }
  close(fd);
  if (!read_all) {
    return std::nullopt;
  }
  std::array<char, kTrailerSize> trailer = {'f', 'l', 'o', 'a',
                                            't', 'r', 'p', '1'};
  std::memcpy(trailer.data() + 8, &hash, sizeof(hash));
  return trailer;
}

// ggml_cpu_set_repack_memory()'s callback: the memory for the buffer of
// repacked weights, `size` bytes.
void* RepackMemory(size_t size, bool* filled, void* user_data) {
  RepackedFile& file = *static_cast<RepackedFile*>(user_data);
  if (file.asked) {
    return nullptr;
  }
  file.asked = true;
  void* memory = MAP_FAILED;
  if (file.write) {
    // Under another name until it is whole: a file by the real name is
    // trusted to hold the weights.
    const int fd =
        open(Temporary(file).c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0 &&
        ftruncate(fd, static_cast<off_t>(size + kTrailerSize)) == 0) {
      memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    }
    if (fd >= 0) {
      close(fd);
    }
  } else {
    const std::optional<std::array<char, kTrailerSize>> expected =
        Trailer(file.model_path);
    const int fd = open(file.path.c_str(), O_RDONLY);
    struct stat status;
    std::array<char, kTrailerSize> trailer;
    // Read only: nothing may write to weights that are there already.
    if (fd >= 0 && expected.has_value() && fstat(fd, &status) == 0 &&
        static_cast<size_t>(status.st_size) == size + kTrailerSize &&
        pread(fd, trailer.data(), trailer.size(), static_cast<off_t>(size)) ==
            static_cast<ssize_t>(trailer.size()) &&
        trailer == *expected) {
      memory = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    if (fd >= 0) {
      close(fd);
    }
  }
  if (memory == MAP_FAILED) {
    return nullptr;
  }
  file.memory = memory;
  file.size = size;
  *filled = !file.write;
  return memory;
}

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
  RepackedFile repacked;
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
    } else if ((arg == "--repacked" || arg == "--write_repacked") &&
               i + 1 < argc) {
      repacked.write = arg == "--write_repacked";
      repacked.path = argv[++i];
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
      return 2;
    }
  }
  if (model_path.empty() || threads < 1) {
    std::fprintf(stderr,
                 "usage: floatype_scorer --model <gguf> [--repacked <file>] "
                 "[--threads N] [--idle_exit_seconds N] [--no_repack] "
                 "[--show_tokens] [--no_cache_reuse]\n"
                 "       floatype_scorer --model <gguf> --write_repacked "
                 "<file>\n");
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
  if (repack && !repacked.path.empty()) {
    repacked.model_path = model_path;
    ggml_cpu_set_repack_memory(RepackMemory, &repacked);
  }
  // The default callback prints progress dots to stderr.
  model_params.progress_callback = [](float, void*) { return true; };
  llama_model* model =
      llama_model_load_from_file(model_path.c_str(), model_params);
  if (model == nullptr) {
    std::fprintf(stderr, "failed to load model: %s\n", model_path.c_str());
    return 1;
  }
  if (repacked.write) {
    // The weights are in the mapping now; the file takes its name once they
    // are on disk.
    bool written = repacked.memory != nullptr &&
                   msync(repacked.memory, repacked.size, MS_SYNC) == 0;
    if (const std::optional<std::array<char, kTrailerSize>> trailer =
            Trailer(model_path);
        written && trailer.has_value()) {
      const int fd = open(Temporary(repacked).c_str(), O_WRONLY);
      written = fd >= 0 &&
                pwrite(fd, trailer->data(), trailer->size(),
                       static_cast<off_t>(repacked.size)) ==
                    static_cast<ssize_t>(trailer->size()) &&
                fsync(fd) == 0;
      if (fd >= 0) {
        close(fd);
      }
    } else {
      written = false;
    }
    written = written && std::rename(Temporary(repacked).c_str(),
                                     repacked.path.c_str()) == 0;
    llama_model_free(model);
    if (!written) {
      std::fprintf(stderr, "cannot write the repacked weights to %s\n",
                   repacked.path.c_str());
      return 1;
    }
    return 0;
  }
  if (!repacked.path.empty() && repack && repacked.memory == nullptr) {
    // The answers are the same either way; the memory is not.
    std::fprintf(stderr,
                 "cannot use the repacked weights in %s; repacked in memory\n",
                 repacked.path.c_str());
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
  // The last request's answers.
  std::optional<Answers> last;
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
    Completion completion;
    if (!request->excluded.empty() && last.has_value() &&
        last->prompt == prompt) {
      // Asked again for the same conversion: zenz's answer is known.
      completion = last->best;
    } else {
      completion = fits ? Complete(ctx, vocab, greedy, prompt, std::move(draft),
                                   max_tokens, cached)
                        : Completion{"", "error"};
      last.reset();
      if (std::string_view(completion.stop) != "error") {
        last = Answers{prompt, completion, max_tokens, std::nullopt};
      }
    }
    if (last.has_value() && !request->excluded.empty() &&
        (completion.text.empty() ||
         IsExcluded(request->excluded, completion.text))) {
      completion = NextBest(ctx, vocab, greedy, *last, request->excluded,
                            cached, show_tokens);
    }
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
