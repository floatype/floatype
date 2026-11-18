// Assert-based tests for protocol.h. Run with ctest, or build/protocol_test.

#include "protocol.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using floatype::scorer::FormatRequest;
using floatype::scorer::FormatResponse;
using floatype::scorer::ParseRequest;
using floatype::scorer::ParseResponse;
using floatype::scorer::Request;
using floatype::scorer::Response;
using floatype::scorer::TakeLatestLine;

int failures = 0;

void Expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

std::string WithoutNewline(std::string line) {
  if (!line.empty() && line.back() == '\n') line.pop_back();
  return line;
}

void TestRequestRoundTrip() {
  const std::string line =
      FormatRequest(Request{42, "熱が\tある\r\nよ", "キョウ"});
  Expect(line == "42\t熱が あるよ\tキョウ\n",
         "a request is one line; tabs become spaces, line breaks go");
  const auto request = ParseRequest(WithoutNewline(line));
  Expect(request.has_value() && request->id == 42 &&
             request->left_context == "熱が あるよ" &&
             request->reading == "キョウ",
         "a formatted request parses back");
  Expect(ParseRequest("\t\tキョウ") == std::nullopt, "an id is required");
  Expect(ParseRequest("1\tキョウ") == std::nullopt,
         "three fields are required");
  Expect(ParseRequest("x1\t\tキョウ") == std::nullopt, "ids are numbers");
}

void TestResponseRoundTrip() {
  const std::string line = FormatResponse(Response{7, "今日", "eog", 12500});
  Expect(line == "7\t今日\teog\t12500\n", "a response is one line");
  const auto response = ParseResponse(WithoutNewline(line));
  Expect(response.has_value() && response->id == 7 &&
             response->conversion == "今日" && response->stop == "eog" &&
             response->microseconds == 12500,
         "a formatted response parses back");
  Expect(ParseResponse("7\t今日\teog\t1.5") == std::nullopt,
         "microseconds are whole numbers");
  Expect(ParseResponse("7\t\teog\t1").has_value(),
         "an empty conversion is a valid response");
}

void TestTakeLatestLine() {
  std::string buffer = "1\ta\n2\tb\n3\tc";
  std::string line;
  Expect(TakeLatestLine(&buffer, &line) == 2, "two complete lines");
  Expect(line == "2\tb", "the latest complete line is taken");
  Expect(buffer == "3\tc", "a partial line stays in the buffer");
  Expect(TakeLatestLine(&buffer, &line) == 0, "no complete line yet");
  buffer += "\n";
  Expect(
      TakeLatestLine(&buffer, &line) == 1 && line == "3\tc" && buffer.empty(),
      "the line completes once its newline arrives");
  buffer = "\n";
  Expect(TakeLatestLine(&buffer, &line) == 1 && line.empty(),
         "an empty line is a line");
}

}  // namespace

int main() {
  TestRequestRoundTrip();
  TestResponseRoundTrip();
  TestTakeLatestLine();
  if (failures == 0) {
    std::puts("protocol_test: all passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
