// floatype: the user's english_words.txt, read and edited line by line (spec
// section 2, mixed Japanese and English input). Apart from the word lists, so
// that the settings screen edits the file without linking them.

#ifndef MOZC_FLOATYPE_ENGLISH_USER_WORDS_H_
#define MOZC_FLOATYPE_ENGLISH_USER_WORDS_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc::floatype {

// The words a user's english_words.txt adds, one a line, the words it takes
// out of the lists ("-time", a line starting with -), and the lines left out
// ("N 行目「go」"): those not 3 to 32 ASCII letters, which no span takes.
// Lines starting with # and blank lines are comments.
struct UserEnglishWords {
  std::string words;    // One a line.
  std::string removed;  // One a line.
  std::vector<std::string> left_out;
};
UserEnglishWords ParseUserEnglishWords(absl::string_view text);

// The preset words (preset_words.txt: page, time, json), one a line: kept in
// letters as if the user had added them.
const std::string& PresetEnglishWords();

enum class EnglishWordEdit {
  kKeep,     // "word": kept in letters.
  kTakeOut,  // "-word": in no list.
  kForget,   // No line: as the built-in lists say.
};

// `text` with the line of `word` (compared without case, with or without -)
// made what `edit` says: the first such line is changed in place, others go,
// and a new line goes at the end. Comments, blank lines and other words stay
// as they were.
std::string EditUserEnglishWords(absl::string_view text, absl::string_view word,
                                 EnglishWordEdit edit);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ENGLISH_USER_WORDS_H_
