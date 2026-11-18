#include "zenz.h"

namespace floatype::zenz {
namespace {

constexpr std::string_view kInputTag = "\xEE\xB8\x80";          // U+EE00
constexpr std::string_view kOutputTag = "\xEE\xB8\x81";         // U+EE01
constexpr std::string_view kContextTag = "\xEE\xB8\x82";        // U+EE02
constexpr std::string_view kIdeographicSpace = "\xE3\x80\x80";  // U+3000
constexpr size_t kMaxContextChars = 40;

bool IsContinuationByte(char c) {
  return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Returns the suffix of UTF-8 `text` that holds its last `n` code points.
std::string_view LastCodePoints(std::string_view text, size_t n) {
  size_t start = text.size();
  while (start > 0 && n > 0) {
    --start;
    if (!IsContinuationByte(text[start])) {
      --n;
    }
  }
  return text.substr(start);
}

// zenz's tokenizer expects ideographic spaces and no line breaks.
void AppendSanitized(std::string_view text, std::string* out) {
  for (const char c : text) {
    if (c == ' ') {
      out->append(kIdeographicSpace);
    } else if (c != '\n' && c != '\r') {
      out->push_back(c);
    }
  }
}

}  // namespace

std::string BuildPrompt(std::string_view left_context,
                        std::string_view reading) {
  std::string prompt;
  const std::string_view context =
      LastCodePoints(left_context, kMaxContextChars);
  if (!context.empty()) {
    prompt.append(kContextTag);
    AppendSanitized(context, &prompt);
  }
  prompt.append(kInputTag);
  AppendSanitized(reading, &prompt);
  prompt.append(kOutputTag);
  return prompt;
}

size_t FindStop(std::string_view text) {
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n' || text[i] == '\r') {
      return i;
    }
    // U+EE00..U+EE0F are EE B8 80..8F in UTF-8.
    if (text.substr(i, 2) == "\xEE\xB8" && i + 2 < text.size() &&
        (static_cast<unsigned char>(text[i + 2]) & 0xF0) == 0x80) {
      return i;
    }
  }
  return std::string_view::npos;
}

}  // namespace floatype::zenz
