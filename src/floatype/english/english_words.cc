#include "floatype/english/english_words.h"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "base/strings/unicode.h"

namespace mozc::floatype {

EnglishWords::EnglishWords(const std::string& path) {
  const absl::StatusOr<std::string> contents = FileUtil::GetContents(path);
  if (!contents.ok()) {
    return;
  }
  for (const absl::string_view line :
       absl::StrSplit(*contents, '\n', absl::SkipEmpty())) {
    words_.push_back(absl::AsciiStrToLower(line));
  }
  std::sort(words_.begin(), words_.end());
  words_.erase(std::unique(words_.begin(), words_.end()), words_.end());
}

bool EnglishWords::Contains(absl::string_view word) const {
  return std::binary_search(words_.begin(), words_.end(),
                            absl::AsciiStrToLower(word));
}

const EnglishWords& EnglishWords::Default() {
  static absl::NoDestructor<EnglishWords> words("/usr/share/dict/words");
  return *words;
}

bool LooksEnglish(absl::string_view raw, absl::string_view preedit,
                  const EnglishWords& words) {
  if (raw.size() < 3 || !std::all_of(raw.begin(), raw.end(), [](char c) {
        return absl::ascii_isalpha(static_cast<unsigned char>(c));
      })) {
    return false;
  }
  // Letters the romaji did not turn into kana, half or full width.
  bool leftover = false;
  for (const char32_t c : Utf8AsChars32(preedit)) {
    leftover = leftover || (c >= U'a' && c <= U'z') ||
               (c >= U'A' && c <= U'Z') || (c >= U'ａ' && c <= U'ｚ') ||
               (c >= U'Ａ' && c <= U'Ｚ');
  }
  return leftover && words.Contains(raw);
}

}  // namespace mozc::floatype
