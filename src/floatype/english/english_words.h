// floatype: telling an English word typed in a kana mode (spec section 2,
// mixed Japanese and English input).

#ifndef MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_
#define MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc::floatype {

// A word list, one word a line, compared without case.
class EnglishWords {
 public:
  explicit EnglishWords(const std::string& path);
  bool Contains(absl::string_view word) const;
  size_t size() const { return words_.size(); }

  // macOS's own list (/usr/share/dict/words, Webster's Second International,
  // in the public domain), read on first use.
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
