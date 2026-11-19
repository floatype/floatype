// A stand-in for floatype_scorer in tests. It answers each request with its
// reading as the conversion, or with the reading's answer in the optional
// "<reading>\t<answer>[\t<next best>...]" file given as the only argument:
// the first answer the request does not exclude, or the stop "exhausted". An
// entry for "<left context>|<reading>" answers only after that context. These
// readings act:
//   CRASH  aborts without answering.
//   スロウ…  (any reading starting so) answers after 200ms, unless the request
//          excludes answers.
//   カップ  answers with the stop reason "cap".
//   アイドル  ends the process without answering, as the scorer does after idling.

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "floatype/scorer/protocol.h"

int main(int argc, char** argv) {
  using ::floatype::scorer::ParseRequest;
  using ::floatype::scorer::Request;
  std::map<std::string, std::vector<std::string>> answers;
  if (argc > 1) {
    std::ifstream file(argv[1]);
    for (std::string entry; std::getline(file, entry);) {
      const size_t tab = entry.find('\t');
      if (tab != std::string::npos) {
        std::vector<std::string>& ranked = answers[entry.substr(0, tab)];
        for (size_t start = tab + 1, end = 0; end != std::string::npos;
             start = end + 1) {
          end = entry.find('\t', start);
          ranked.push_back(entry.substr(start, end - start));
        }
      }
    }
  }
  std::string buffer;
  std::string line;
  char chunk[4096];
  while (true) {
    const ssize_t n = read(STDIN_FILENO, chunk, sizeof(chunk));
    if (n <= 0) return 0;
    buffer.append(chunk, n);
    if (::floatype::scorer::TakeLatestLine(&buffer, &line) == 0) continue;
    const std::optional<Request> request = ParseRequest(line);
    if (!request.has_value()) continue;
    if (request->reading == "CRASH") std::abort();
    if (request->reading == "アイドル") return 0;
    if (request->reading.starts_with("スロウ") && request->excluded.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    auto it = answers.find(request->left_context + "|" + request->reading);
    if (it == answers.end()) it = answers.find(request->reading);
    const std::vector<std::string> ranked =
        it == answers.end() ? std::vector<std::string>{request->reading}
                            : it->second;
    const auto next = std::find_if(
        ranked.begin(), ranked.end(), [&](const std::string& conversion) {
          return std::find(request->excluded.begin(), request->excluded.end(),
                           conversion) == request->excluded.end();
        });
    const std::string answer = ::floatype::scorer::FormatResponse(
        {request->id, next == ranked.end() ? "" : *next,
         next == ranked.end()           ? "exhausted"
         : request->reading == "カップ" ? "cap"
                                        : "eog",
         1});
    if (write(STDOUT_FILENO, answer.data(), answer.size()) < 0) return 0;
  }
}
