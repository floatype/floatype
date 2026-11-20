// floatype: how the letters of the alphabet are read aloud in Japanese (spec
// section 4, zenz's letters for names typed in kana).

#ifndef MOZC_FLOATYPE_ZENZ_LETTER_READING_H_
#define MOZC_FLOATYPE_ZENZ_LETTER_READING_H_

#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace mozc::floatype {

// The names of `letter` (A to Z, either case) in hiragana: じぇー and じぇい
// for J, あーる for R. Empty for any other character.
absl::Span<const absl::string_view> LetterNames(char letter);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ZENZ_LETTER_READING_H_
