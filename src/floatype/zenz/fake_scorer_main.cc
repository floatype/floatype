// A stand-in for floatype_scorer in tests. It answers each request with its
// reading as the conversion, or with the reading's answer in the optional
// "<reading>\t<answer>" file given as the only argument. These readings act:
//   CRASH  aborts without answering.
//   スロウ  answers after 200ms.
//   カップ  answers with the stop reason "cap".

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "floatype/scorer/protocol.h"

int main(int argc, char** argv) {
  using ::floatype::scorer::ParseRequest;
  using ::floatype::scorer::Request;
  std::map<std::string, std::string> answers;
  if (argc > 1) {
    std::ifstream file(argv[1]);
    for (std::string entry; std::getline(file, entry);) {
      const size_t tab = entry.find('\t');
      if (tab != std::string::npos) {
        answers[entry.substr(0, tab)] = entry.substr(tab + 1);
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
    if (request->reading == "スロウ") {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    const auto it = answers.find(request->reading);
    const std::string answer = ::floatype::scorer::FormatResponse(
        {request->id, it == answers.end() ? request->reading : it->second,
         request->reading == "カップ" ? "cap" : "eog", 1});
    if (write(STDOUT_FILENO, answer.data(), answer.size()) < 0) return 0;
  }
}
