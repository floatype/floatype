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

std::string FoldWidth(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    // U+FF01..U+FF5E are EF BC 81..BF and EF BD 80..9E in UTF-8.
    if (i + 2 < text.size() && static_cast<unsigned char>(text[i]) == 0xEF) {
      const unsigned char second = text[i + 1];
      const unsigned char third = text[i + 2];
      if ((second == 0xBC && third >= 0x81 && third <= 0xBF) ||
          (second == 0xBD && third >= 0x80 && third <= 0x9E)) {
        out.push_back(static_cast<char>(
            (second == 0xBC ? 0x21 - 0x81 : 0x60 - 0x80) + third));
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
  }
  return out;
}

}  // namespace floatype::zenz
