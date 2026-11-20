// floatype: whether a kana reading says a number (spec section 4, zenz's
// digits for numbers typed in kana).

#ifndef MOZC_FLOATYPE_ZENZ_NUMBER_READING_H_
#define MOZC_FLOATYPE_ZENZ_NUMBER_READING_H_

#include <cstdint>

#include "absl/strings/string_view.h"

namespace mozc::floatype {

// Whether `reading` (hiragana) is a way to read `value` aloud, counting the
// forms numbers take before counters: せんきゅうひゃくななじゅうに and 1972,
// じゅうはっ(さい) and 18, ごじゅっ(ぷん) and 50, よ(ねん) and 4, く(がつ)
// and 9. Numbers of 10^12 and more, and digits read one by one (にーまるにーろく),
// are not read.
bool ReadsAsNumber(absl::string_view reading, uint64_t value);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ZENZ_NUMBER_READING_H_
