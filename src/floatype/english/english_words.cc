#include "floatype/english/english_words.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/strings/unicode.h"

namespace mozc::floatype {

namespace {
#include "floatype/english/english_words.inc"
}  // namespace

EnglishWords::EnglishWords(absl::string_view text) {
  for (const absl::string_view line :
       absl::StrSplit(text, '\n', absl::SkipEmpty())) {
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
  static absl::NoDestructor<EnglishWords> words(kEnglishWords);
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

namespace {

bool IsLetter(absl::string_view text) {
  bool letter = !text.empty();
  for (const char32_t c : Utf8AsChars32(text)) {
    letter = letter && ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') ||
                        (c >= U'ａ' && c <= U'ｚ') || (c >= U'Ａ' && c <= U'Ｚ'));
  }
  return letter;
}

}  // namespace

std::vector<EnglishSpan> EnglishSpans(const std::vector<std::string>& raws,
                                      const std::vector<std::string>& query,
                                      const std::vector<std::string>& preedit,
                                      const EnglishWords& words) {
  // Longer than any word worth keeping; bounds the work per key.
  constexpr size_t kMaxLetters = 32;
  const size_t size = raws.size();
  std::vector<EnglishSpan> candidates;
  if (query.size() != size || preedit.size() != size) {
    return candidates;
  }
  // Letters, and among them those the romaji left loose (not kept as typed).
  std::vector<bool> letter(size), loose(size);
  for (size_t i = 0; i < size; ++i) {
    letter[i] = IsLetter(query[i]);
    loose[i] = letter[i] && preedit[i] != raws[i];
  }
  for (size_t begin = 0; begin < size; ++begin) {
    if (begin > 0 && letter[begin - 1]) {
      continue;  // Inside a run of letters.
    }
    std::string raw, shown;
    std::vector<size_t> ends;  // Where each character's keys end in `raw`.
    for (size_t end = begin + 1; end <= size; ++end) {
      raw += raws[end - 1];
      shown += preedit[end - 1];
      ends.push_back(raw.size());
      if (raw.size() > kMaxLetters + 4) {
        break;
      }
      // Letters after the span would belong to it.
      if ((end < size && letter[end]) || shown == raw) {
        continue;
      }
      for (size_t letters = std::min(raw.size(), kMaxLetters); letters >= 3;
           --letters) {
        // Keys retyped start a kana's consonants (t|su, l|wo), never its
        // vowel (update|su, not updates|u), and end in a kana that completes
        // them, not in loose letters that may yet make a longer word.
        if (letters < raw.size() &&
            (letter[end - 1] || absl::StrContains("aiueo", raw[letters]))) {
          continue;
        }
        const absl::string_view word = absl::string_view(raw).substr(0, letters);
        if (!std::all_of(word.begin(), word.end(), [](char c) {
              return absl::ascii_isalpha(static_cast<unsigned char>(c));
            }) ||
            !words.Contains(word)) {
          continue;
        }
        // Loose letters: some in the word, and after it only those right
        // after it, which the next kana took (えまいl|wお).
        bool in_word = false, after_word = false, kana_after = false;
        for (size_t i = 0; i < ends.size(); ++i) {
          if (ends[i] <= letters) {
            in_word = in_word || loose[begin + i];
          } else if (!loose[begin + i]) {
            kana_after = true;
          } else {
            after_word = after_word || kana_after;
          }
        }
        if (in_word && !after_word) {
          candidates.push_back({begin, end, std::string(word),
                                raw.substr(letters)});
        }
        break;  // The longest word for this span.
      }
    }
  }
  // Longer words first; for the same word, the span retyping least.
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const EnglishSpan& a, const EnglishSpan& b) {
                     return a.word.size() != b.word.size()
                                ? a.word.size() > b.word.size()
                                : a.end - a.begin < b.end - b.begin;
                   });
  std::vector<EnglishSpan> spans;
  for (const EnglishSpan& candidate : candidates) {
    if (std::none_of(spans.begin(), spans.end(), [&](const EnglishSpan& s) {
          return candidate.begin < s.end && s.begin < candidate.end;
        })) {
      spans.push_back(candidate);
    }
  }
  std::sort(spans.begin(), spans.end(),
            [](const EnglishSpan& a, const EnglishSpan& b) {
              return a.begin < b.begin;
            });
  return spans;
}

}  // namespace mozc::floatype
