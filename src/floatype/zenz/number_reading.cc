#include "floatype/zenz/number_reading.h"

#include <cstdint>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace mozc::floatype {
namespace {

using Forms = std::vector<absl::string_view>;

// The ways to say digit d (1 to 9) in each place of a group of four digits.
// The forms with っ come before a counter (いっさい, ろっぴき, じゅっぷん).
const Forms& Thousands(int d) {
  static const Forms* const kForms = new Forms[10]{
      {},           {"せん", "いっせん"}, {"にせん"},   {"さんぜん"},
      {"よんせん"}, {"ごせん"},           {"ろくせん"}, {"ななせん"},
      {"はっせん"}, {"きゅうせん"}};
  return kForms[d];
}
const Forms& Hundreds(int d) {
  static const Forms* const kForms = new Forms[10]{
      {},
      {"ひゃく", "ひゃっ"},
      {"にひゃく", "にひゃっ"},
      {"さんびゃく", "さんびゃっ"},
      {"よんひゃく", "よんひゃっ"},
      {"ごひゃく", "ごひゃっ"},
      {"ろっぴゃく", "ろっぴゃっ"},
      {"ななひゃく", "ななひゃっ"},
      {"はっぴゃく", "はっぴゃっ"},
      {"きゅうひゃく", "きゅうひゃっ"}};
  return kForms[d];
}
const Forms& Tens(int d) {
  static const Forms* const kForms = new Forms[10]{
      {},
      {"じゅう", "じゅっ", "じっ"},
      {"にじゅう", "にじゅっ", "にじっ"},
      {"さんじゅう", "さんじゅっ", "さんじっ"},
      {"よんじゅう", "よんじゅっ", "よんじっ"},
      {"ごじゅう", "ごじゅっ", "ごじっ"},
      {"ろくじゅう", "ろくじゅっ", "ろくじっ"},
      {"ななじゅう", "ななじゅっ", "ななじっ", "しちじゅう", "しちじゅっ"},
      {"はちじゅう", "はちじゅっ", "はちじっ"},
      {"きゅうじゅう", "きゅうじゅっ", "きゅうじっ"}};
  return kForms[d];
}
const Forms& Ones(int d) {
  static const Forms* const kForms = new Forms[10]{
      {},       {"いち", "いっ"}, {"に"},           {"さん"},         {"よん", "よ", "し"},
      {"ご"},   {"ろく", "ろっ"}, {"なな", "しち"}, {"はち", "はっ"}, {"きゅう", "く"}};
  return kForms[d];
}

// Whether `reading` says the places in `slots` in order and then nothing.
bool Says(absl::string_view reading, const std::vector<const Forms*>& slots,
          size_t slot) {
  if (slot == slots.size()) {
    return reading.empty();
  }
  for (const absl::string_view form : *slots[slot]) {
    if (absl::StartsWith(reading, form) &&
        Says(reading.substr(form.size()), slots, slot + 1)) {
      return true;
    }
  }
  return false;
}

// Adds the places of `group` (1 to 9999) to `slots`.
void AddGroup(int group, std::vector<const Forms*>* slots) {
  if (const int d = group / 1000; d != 0) {
    slots->push_back(&Thousands(d));
  }
  if (const int d = group / 100 % 10; d != 0) {
    slots->push_back(&Hundreds(d));
  }
  if (const int d = group / 10 % 10; d != 0) {
    slots->push_back(&Tens(d));
  }
  if (const int d = group % 10; d != 0) {
    slots->push_back(&Ones(d));
  }
}

// The ways to say 0 to 19 and the tens in English, in kana.
const Forms& EnglishOnes(int n) {
  static const Forms* const kForms = new Forms[20]{
      {"ぜろ"},
      {"わん"},
      {"つー", "とぅー"},
      {"すりー"},
      {"ふぉー"},
      {"ふぁいぶ", "ふぁいゔ"},
      {"しっくす"},
      {"せぶん", "せゔん"},
      {"えいと"},
      {"ないん"},
      {"てん"},
      {"いれぶん", "いれゔん"},
      {"とぅえるぶ", "とぅえるゔ"},
      {"さーてぃーん"},
      {"ふぉーてぃーん"},
      {"ふぃふてぃーん"},
      {"しっくすてぃーん"},
      {"せぶんてぃーん"},
      {"えいてぃーん"},
      {"ないんてぃーん"}};
  return kForms[n];
}
const Forms& EnglishTens(int d) {
  static const Forms* const kForms = new Forms[10]{
      {},
      {},
      {"とぅえんてぃー", "とぅうぇんてぃー"},
      {"さーてぃー"},
      {"ふぉーてぃー"},
      {"ふぃふてぃー"},
      {"しっくすてぃー"},
      {"せぶんてぃー"},
      {"えいてぃー"},
      {"ないんてぃー"}};
  return kForms[d];
}

// Whether `reading` says `value` (below 100) in English.
bool SaysInEnglish(absl::string_view reading, int value) {
  std::vector<const Forms*> slots;
  if (value < 20) {
    slots.push_back(&EnglishOnes(value));
  } else {
    slots.push_back(&EnglishTens(value / 10));
    if (value % 10 != 0) {
      slots.push_back(&EnglishOnes(value % 10));
    }
  }
  return Says(reading, slots, 0);
}

}  // namespace

bool ReadsAsNumber(absl::string_view reading, uint64_t value) {
  static const Forms* const kZero = new Forms{"ぜろ", "れい", "まる"};
  static const Forms* const kMan = new Forms{"まん"};
  static const Forms* const kOku = new Forms{"おく"};
  if (value >= 1'0000'0000'0000) {
    return false;
  }
  if (value < 100 && SaysInEnglish(reading, static_cast<int>(value))) {
    return true;
  }
  std::vector<const Forms*> slots;
  if (value == 0) {
    slots.push_back(kZero);
  }
  if (const int oku = static_cast<int>(value / 1'0000'0000); oku != 0) {
    AddGroup(oku, &slots);
    slots.push_back(kOku);
  }
  if (const int man = static_cast<int>(value / 1'0000 % 1'0000); man != 0) {
    AddGroup(man, &slots);
    slots.push_back(kMan);
  }
  AddGroup(static_cast<int>(value % 1'0000), &slots);
  return Says(reading, slots, 0);
}

}  // namespace mozc::floatype
