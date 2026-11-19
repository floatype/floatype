// floatype: telling an English word typed in a kana mode (spec section 2,
// mixed Japanese and English input).

#ifndef MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_
#define MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc::floatype {

// A word list, compared without case.
class EnglishWords {
 public:
  // `text` has one word a line.
  explicit EnglishWords(absl::string_view text);
  bool Contains(absl::string_view word) const;
  size_t size() const { return words_.size(); }

  // SCOWL's size-60 American list (english_words.txt, made by
  // floatype/tools/make_english_words.py), built into the binary.
  static const EnglishWords& Default();

 private:
  std::vector<std::string> words_;  // Lower case, sorted.
};

// Whether the keys `raw` typed into the composition queried as `preedit` (with
// a trailing n already ん, so ten stays てん) were an
// English word: the romaji left letters unconverted (まcぼおk for macbook,
// unlike へっぉ for hello, which stays Japanese), and `raw`, at least three
// letters, is in `words`.
bool LooksEnglish(absl::string_view raw, absl::string_view preedit,
                  const EnglishWords& words);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_
