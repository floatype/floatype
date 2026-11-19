#include "protocol.h"

#include <algorithm>
#include <charconv>
#include <vector>

namespace floatype::scorer {
namespace {

// zenz drops line breaks anyway (AzooKey does the same), and a tab would end
// the field.
std::string Sanitize(std::string_view field) {
  std::string out;
  out.reserve(field.size());
  for (const char c : field) {
    if (c == '\t') {
      out.push_back(' ');
    } else if (c != '\n' && c != '\r') {
      out.push_back(c);
    }
  }
  return out;
}

std::vector<std::string_view> SplitTabs(std::string_view line) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    fields.push_back(line.substr(start, tab - start));
    if (tab == std::string_view::npos) {
      return fields;
    }
    start = tab + 1;
  }
}

std::optional<uint64_t> ParseNumber(std::string_view text) {
  uint64_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size() ||
      text.empty()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::string FormatRequest(const Request& request) {
  std::string line = std::to_string(request.id) + '\t' +
                     Sanitize(request.left_context) + '\t' +
                     Sanitize(request.reading);
  if (!request.draft.empty() || !request.excluded.empty()) {
    line += '\t' + Sanitize(request.draft);
  }
  for (const std::string& excluded : request.excluded) {
    line += '\t' + Sanitize(excluded);
  }
  return line + '\n';
}

std::optional<Request> ParseRequest(std::string_view line) {
  const std::vector<std::string_view> fields = SplitTabs(line);
  if (fields.size() < 3) {
    return std::nullopt;
  }
  const std::optional<uint64_t> id = ParseNumber(fields[0]);
  if (!id.has_value()) {
    return std::nullopt;
  }
  return Request{
      *id, std::string(fields[1]), std::string(fields[2]),
      fields.size() > 3 ? std::string(fields[3]) : "",
      std::vector<std::string>(
          fields.begin() + std::min<size_t>(fields.size(), 4), fields.end())};
}

std::string FormatResponse(const Response& response) {
  return std::to_string(response.id) + '\t' + Sanitize(response.conversion) +
         '\t' + Sanitize(response.stop) + '\t' +
         std::to_string(response.microseconds) + '\n';
}

std::optional<Response> ParseResponse(std::string_view line) {
  const std::vector<std::string_view> fields = SplitTabs(line);
  if (fields.size() != 4) {
    return std::nullopt;
  }
  const std::optional<uint64_t> id = ParseNumber(fields[0]);
  const std::optional<uint64_t> microseconds = ParseNumber(fields[3]);
  if (!id.has_value() || !microseconds.has_value()) {
    return std::nullopt;
  }
  return Response{*id, std::string(fields[1]), std::string(fields[2]),
                  *microseconds};
}

int TakeLatestLine(std::string* buffer, std::string* line) {
  const size_t last_newline = buffer->rfind('\n');
  if (last_newline == std::string::npos) {
    return 0;
  }
  int count = 0;
  for (size_t i = 0; i <= last_newline; ++i) {
    count += (*buffer)[i] == '\n';
  }
  const size_t previous_newline = last_newline == 0
                                      ? std::string::npos
                                      : buffer->rfind('\n', last_newline - 1);
  const size_t start =
      previous_newline == std::string::npos ? 0 : previous_newline + 1;
  *line = buffer->substr(start, last_newline - start);
  buffer->erase(0, last_newline + 1);
  return count;
}

}  // namespace floatype::scorer
