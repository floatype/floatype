#include "floatype/english/user_words.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace mozc::floatype {
namespace {
#include "floatype/english/preset_words.inc"

// The word a line keeps or takes out, or "" for a comment.
absl::string_view WordOf(absl::string_view line) {
  line = absl::StripAsciiWhitespace(line);
  if (line.empty() || line.starts_with('#')) {
    return "";
  }
  return absl::StripAsciiWhitespace(line.substr(line.starts_with('-') ? 1 : 0));
}
}  // namespace

UserEnglishWords ParseUserEnglishWords(absl::string_view text) {
  UserEnglishWords user;
  const std::vector<absl::string_view> lines = absl::StrSplit(text, '\n');
  for (size_t i = 0; i < lines.size(); ++i) {
    const absl::string_view line = absl::StripAsciiWhitespace(lines[i]);
    if (line.empty() || line.starts_with('#')) {
      continue;
    }
    const bool removes = line.starts_with('-');
    const absl::string_view word =
        absl::StripAsciiWhitespace(line.substr(removes ? 1 : 0));
    // EnglishSpans takes 3 to 32 letters, LooksEnglish 3 or more.
    if (word.size() < 3 || word.size() > 32 ||
        !std::all_of(word.begin(), word.end(), [](char c) {
          return absl::ascii_isalpha(static_cast<unsigned char>(c));
        })) {
      user.left_out.push_back(absl::StrCat(i + 1, " 行目「", line, "」"));
      continue;
    }
    absl::StrAppend(removes ? &user.removed : &user.words, word, "\n");
  }
  return user;
}

const std::string& PresetEnglishWords() {
  static absl::NoDestructor<std::string> words(
      ParseUserEnglishWords(kPresetWords).words);
  return *words;
}

std::string EditUserEnglishWords(absl::string_view text, absl::string_view word,
                                 EnglishWordEdit edit) {
  const std::string line = edit == EnglishWordEdit::kTakeOut
                               ? absl::StrCat("-", word)
                               : std::string(word);
  std::vector<std::string> lines = absl::StrSplit(text, '\n');
  // A file ending in a newline splits into a last empty piece.
  const bool ended = !lines.empty() && lines.back().empty();
  if (ended) {
    lines.pop_back();
  }
  bool placed = edit == EnglishWordEdit::kForget;
  std::vector<std::string> out;
  for (std::string& old : lines) {
    if (!absl::EqualsIgnoreCase(WordOf(old), word)) {
      out.push_back(std::move(old));
    } else if (!placed) {
      out.push_back(line);
      placed = true;
    }
  }
  if (!placed) {
    out.push_back(line);
  }
  if (out.empty()) {
    return "";
  }
  return absl::StrCat(absl::StrJoin(out, "\n"), "\n");
}

}  // namespace mozc::floatype
