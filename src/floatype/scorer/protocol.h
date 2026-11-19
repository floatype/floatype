// The line protocol between the Mozc converter and the scorer process.
//
// Requests and responses are single UTF-8 lines with tab-separated fields:
//   request:  <id>\t<left context>\t<katakana reading>[\t<draft>
//             [\t<excluded>...]]\n
//   response: <id>\t<conversion>\t<stop>\t<microseconds>\n
// The scorer answers only the latest request it has received; ids let the
// converter ignore answers to requests it no longer waits for.
//
// This file has no dependencies so that both CMake (the scorer) and Bazel (the
// converter) can build it.

#ifndef FLOATYPE_SCORER_PROTOCOL_H_
#define FLOATYPE_SCORER_PROTOCOL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace floatype::scorer {

struct Request {
  uint64_t id = 0;
  std::string left_context;
  std::string reading;
  // Optional: Mozc's conversion, which the scorer checks in one pass before
  // generating on its own (speculative decoding). It changes the speed only.
  std::string draft;
  // Optional: conversions the user turned down. When zenz's own answer is one
  // of them, ignoring full and half width, the scorer answers with its next
  // best or with the stop "exhausted".
  std::vector<std::string> excluded;
};

struct Response {
  uint64_t id = 0;
  std::string conversion;
  // Why generation ended: "eog" (end of generation), "tag" (zenz tag or line
  // break), "cap" (length cap), "exhausted" (no answer left that is not
  // excluded) or "error".
  std::string stop;
  // Time the scorer spent on the request.
  uint64_t microseconds = 0;
};

// Inside fields, tabs become spaces and line breaks are dropped.
std::string FormatRequest(const Request& request);
std::optional<Request> ParseRequest(std::string_view line);
std::string FormatResponse(const Response& response);
std::optional<Response> ParseResponse(std::string_view line);

// Moves the last complete line (without its newline) out of `buffer` into
// `line` and drops the complete lines before it, keeping any trailing partial
// line. Returns the number of complete lines found; `line` is set only when it
// is positive.
int TakeLatestLine(std::string* buffer, std::string* line);

}  // namespace floatype::scorer

#endif  // FLOATYPE_SCORER_PROTOCOL_H_
