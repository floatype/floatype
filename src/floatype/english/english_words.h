// floatype: telling an English word typed in a kana mode (spec section 2,
// mixed Japanese and English input).

#ifndef MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_
#define MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_

#include <string>
#include <utility>
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

// An English word inside a composition: characters [begin, end), typed as
// `word` and then `retyped`, keys the romaji joined to the kana after the
// word (test + する shows てsつる; email + を shows えまいlwお). The
// characters become the word's letters, and `retyped` is typed again.
struct EnglishSpan {
  size_t begin;
  size_t end;
  std::string word;
  std::string retyped;
  bool operator==(const EnglishSpan&) const = default;
};

// The English words inside a composition whose i-th character was typed as
// `raws[i]`, reads `query[i]` (as in LooksEnglish) and shows `preedit[i]`. A
// word, at least three letters and in `words`, must hold a letter the romaji
// left over, and leave none after it within its span; longer words win, so
// "has" does not take the s of "slack". A span already shown as typed is left
// out. Keys retyped start with a consonant, and are not letters still loose at
// the end, which may yet make a longer word.
std::vector<EnglishSpan> EnglishSpans(const std::vector<std::string>& raws,
                                      const std::vector<std::string>& query,
                                      const std::vector<std::string>& preedit,
                                      const EnglishWords& words);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ENGLISH_ENGLISH_WORDS_H_
