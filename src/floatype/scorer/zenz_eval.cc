// zenz_eval: runs greedy decoding of a zenz model over prompts read from stdin.
//
// Input: one UTF-8 prompt per line (already in zenz's format).
// Output: "<completion>\t<milliseconds>" per line, in the same order.
// On exit, prints {"load_ms":..., "peak_rss_mb":...} to stderr.
//
// Usage: zenz_eval --model <gguf> [--cpu] [--threads N] [--max_tokens N] [--show_tokens]

#include <sys/resource.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "gguf.h"
#include "llama.h"

namespace {

// zenz's GGUF names its pre-tokenizer "gpt2-small-japanese-char", which upstream
// llama.cpp does not know. It uses the same regex as the GPT-2 pre-tokenizer
// (azooKey/llama.cpp only adds the name), so such models are loaded as "gpt-2".
constexpr char kPreTokenizerKey[] = "tokenizer.ggml.pre";
constexpr char kZenzPreTokenizer[] = "gpt2-small-japanese-char";

std::string ReadPreTokenizer(const std::string &model_path) {
  gguf_init_params params = {/*no_alloc=*/true, /*ctx=*/nullptr};
  gguf_context *gguf = gguf_init_from_file(model_path.c_str(), params);
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

// Returns a kv_overrides array (terminated by an empty key) for llama_model_params.
std::vector<llama_model_kv_override> PreTokenizerOverrides(const std::string &model_path) {
  std::vector<llama_model_kv_override> overrides;
  if (ReadPreTokenizer(model_path) == kZenzPreTokenizer) {
    llama_model_kv_override o{};
    o.tag = LLAMA_KV_OVERRIDE_TYPE_STR;
    std::snprintf(o.key, sizeof(o.key), "%s", kPreTokenizerKey);
    std::snprintf(o.val_str, sizeof(o.val_str), "%s", "gpt-2");
    overrides.push_back(o);
  }
  overrides.push_back(llama_model_kv_override{});  // Terminator: key[0] == '\0'.
  return overrides;
}

using Clock = std::chrono::steady_clock;

double MillisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void LogErrorsOnly(ggml_log_level level, const char *text, void * /*user_data*/) {
  if (level >= GGML_LOG_LEVEL_ERROR) {
    std::fputs(text, stderr);
  }
}

std::vector<llama_token> Tokenize(const llama_vocab *vocab, std::string_view text) {
  // add_special follows the GGUF (zenz sets add_bos_token=false), as AzooKey does.
  std::vector<llama_token> tokens(text.size() + 2);
  int n = llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()), tokens.data(),
                         static_cast<int32_t>(tokens.size()), /*add_special=*/true,
                         /*parse_special=*/false);
  if (n < 0) {
    tokens.resize(-n);
    n = llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()), tokens.data(),
                       static_cast<int32_t>(tokens.size()), true, false);
  }
  tokens.resize(n < 0 ? 0 : n);
  return tokens;
}

// Greedy decoding until an end-of-generation token or max_tokens.
std::string Complete(llama_context *ctx, const llama_vocab *vocab, llama_sampler *greedy,
                     std::vector<llama_token> prompt, int max_tokens) {
  llama_memory_clear(llama_get_memory(ctx), /*data=*/true);
  std::string out;
  if (prompt.empty() ||
      llama_decode(ctx, llama_batch_get_one(prompt.data(), static_cast<int32_t>(prompt.size()))) !=
          0) {
    return out;
  }
  for (int i = 0; i < max_tokens; ++i) {
    llama_token token = llama_sampler_sample(greedy, ctx, -1);
    if (llama_vocab_is_eog(vocab, token)) {
      break;
    }
    char piece[128];
    const int len = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
    if (len > 0) {
      out.append(piece, len);
    }
    if (llama_decode(ctx, llama_batch_get_one(&token, 1)) != 0) {
      break;
    }
  }
  return out;
}

}  // namespace

int main(int argc, char **argv) {
  std::string model_path;
  bool use_gpu = true;
  int threads = 4;
  int max_tokens = 64;
  bool show_tokens = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--model" && i + 1 < argc) {
      model_path = argv[++i];
    } else if (arg == "--cpu") {
      use_gpu = false;
    } else if (arg == "--threads" && i + 1 < argc) {
      threads = std::atoi(argv[++i]);
    } else if (arg == "--max_tokens" && i + 1 < argc) {
      max_tokens = std::atoi(argv[++i]);
    } else if (arg == "--show_tokens") {
      show_tokens = true;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
      return 2;
    }
  }
  if (model_path.empty()) {
    std::fprintf(stderr, "usage: zenz_eval --model <gguf> [--cpu] [--threads N] [--max_tokens N] [--show_tokens]\n");
    return 2;
  }

  llama_log_set(LogErrorsOnly, nullptr);
  llama_backend_init();

  const Clock::time_point load_start = Clock::now();
  llama_model_params model_params = llama_model_default_params();
  model_params.n_gpu_layers = use_gpu ? -1 : 0;
  const std::vector<llama_model_kv_override> overrides = PreTokenizerOverrides(model_path);
  model_params.kv_overrides = overrides.data();
  llama_model *model = llama_model_load_from_file(model_path.c_str(), model_params);
  if (model == nullptr) {
    std::fprintf(stderr, "failed to load model: %s\n", model_path.c_str());
    return 1;
  }
  const llama_vocab *vocab = llama_model_get_vocab(model);

  llama_context_params context_params = llama_context_default_params();
  context_params.n_ctx = 512;
  context_params.n_batch = 512;
  context_params.n_threads = threads;
  context_params.n_threads_batch = threads;
  context_params.no_perf = true;
  llama_context *ctx = llama_init_from_model(model, context_params);
  if (ctx == nullptr) {
    std::fprintf(stderr, "failed to create a context\n");
    llama_model_free(model);
    return 1;
  }
  llama_sampler *greedy = llama_sampler_init_greedy();
  const double load_ms = MillisecondsSince(load_start);

  std::string line;
  while (std::getline(std::cin, line)) {
    const Clock::time_point start = Clock::now();
    const std::vector<llama_token> prompt = Tokenize(vocab, line);
    if (show_tokens) {
      for (const llama_token token : prompt) {
        char piece[128];
        const int len = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, true);
        std::fprintf(stderr, "%d:[%.*s] ", token, len > 0 ? len : 0, piece);
      }
      std::fputc('\n', stderr);
    }
    const std::string completion = Complete(ctx, vocab, greedy, prompt, max_tokens);
    std::cout << completion << '\t' << MillisecondsSince(start) << '\n' << std::flush;
  }

  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  // ru_maxrss is in bytes on macOS.
  std::fprintf(stderr, "{\"load_ms\":%.1f,\"peak_rss_mb\":%.1f}\n", load_ms,
               static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0));

  llama_sampler_free(greedy);
  llama_free(ctx);
  llama_model_free(model);
  llama_backend_free();
  return 0;
}
