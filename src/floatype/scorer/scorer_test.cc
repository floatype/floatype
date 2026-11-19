// Assert-based tests for floatype_scorer with the real model: requests with
// exclusions answer zenz's next best, and leave later answers as they were.
// Run with ctest, or build/scorer_test <scorer> <model>.

#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "protocol.h"
#include "zenz.h"

namespace {

using floatype::scorer::Request;
using floatype::scorer::Response;

int failures = 0;

void Expect(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    ++failures;
  }
}

// The scorer as a child process, asked one request at a time.
class Scorer {
 public:
  Scorer(const char* scorer, const char* model) {
    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) std::abort();
    pid_ = fork();
    if (pid_ == 0) {
      dup2(to_child[0], STDIN_FILENO);
      dup2(from_child[1], STDOUT_FILENO);
      close(to_child[1]);
      close(from_child[0]);
      execl(scorer, scorer, "--model", model, static_cast<char*>(nullptr));
      std::_Exit(127);
    }
    close(to_child[0]);
    close(from_child[1]);
    in_ = fdopen(to_child[1], "w");
    out_ = fdopen(from_child[0], "r");
  }
  ~Scorer() {
    std::fclose(in_);
    std::fclose(out_);
    waitpid(pid_, nullptr, 0);
  }

  Response Ask(const std::string& context, const std::string& reading,
               std::vector<std::string> excluded = {}) {
    const std::string line = floatype::scorer::FormatRequest(
        Request{++id_, context, reading, "", std::move(excluded)});
    std::fputs(line.c_str(), in_);
    std::fflush(in_);
    std::string answer;
    for (int c = std::fgetc(out_); c != EOF && c != '\n';
         c = std::fgetc(out_)) {
      answer.push_back(static_cast<char>(c));
    }
    const std::optional<Response> response =
        floatype::scorer::ParseResponse(answer);
    return response.value_or(Response{0, "", "no answer", 0});
  }

 private:
  pid_t pid_ = -1;
  FILE* in_ = nullptr;
  FILE* out_ = nullptr;
  uint64_t id_ = 0;
};

struct Item {
  std::string context;
  std::string reading;
};

const std::vector<Item> kItems = {
    {"熱が", "キョウハイシャニイク"},
    {"", "ノイライガクルヨウニ"},
    {"", "デハ、ギインノフタイホトッケン"},
};

// `text` with ASCII in full width (U+FF01..U+FF5E).
std::string Widen(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c > 0x20 && c < 0x7F) {
      const int code_point = 0xFF01 + (c - 0x21);
      out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool Finished(const Response& response) {
  return response.stop == "eog" || response.stop == "tag";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fputs("usage: scorer_test <scorer> <model>\n", stderr);
    return EXIT_FAILURE;
  }
  Scorer scorer(argv[1], argv[2]);
  std::vector<std::string> first;
  for (const Item& item : kItems) {
    const Response response = scorer.Ask(item.context, item.reading);
    Expect(Finished(response) && !response.conversion.empty(),
           item.reading + " is converted");
    first.push_back(response.conversion);
  }

  const Item& item = kItems[1];
  const Response next = scorer.Ask(item.context, item.reading, {first[1]});
  Expect(
      Finished(next) && !next.conversion.empty() && next.conversion != first[1],
      "an excluded answer gives way to the next best: " + next.conversion);
  const Response third =
      scorer.Ask(item.context, item.reading, {first[1], next.conversion});
  Expect(Finished(third) && !third.conversion.empty() &&
             third.conversion != first[1] &&
             third.conversion != next.conversion,
         "and that one to the one after: " + third.conversion);
  Expect(scorer.Ask(item.context, item.reading, {next.conversion}).conversion ==
             first[1],
         "an exclusion that is not zenz's answer changes nothing");
  Expect(scorer.Ask(item.context, item.reading, {first[1] + "た"}).conversion ==
             first[1],
         "exclusions are whole answers");
  // zenz writes 8月1日 here; excluding ８月１日 excludes it too.
  const std::string digits = "ハシンレキハチガツツイタチ";
  const std::string half = scorer.Ask("", digits).conversion;
  Expect(half.find('8') != std::string::npos, "zenz writes digits: " + half);
  Expect(scorer.Ask("", digits, {Widen(half)}).conversion != half,
         "width does not matter in exclusions");

  for (size_t i = 0; i < kItems.size(); ++i) {
    Expect(
        scorer.Ask(kItems[i].context, kItems[i].reading).conversion == first[i],
        kItems[i].reading + " converts as before the exclusions");
  }
  if (failures == 0) {
    std::puts("scorer_test: all passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
