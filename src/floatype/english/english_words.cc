#include "floatype/english/english_words.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "base/file_util.h"
#include "base/strings/unicode.h"
#include "floatype/config/settings_file.h"

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

UserEnglishWords ParseUserEnglishWords(absl::string_view text) {
  UserEnglishWords user;
  const std::vector<absl::string_view> lines = absl::StrSplit(text, '\n');
  for (size_t i = 0; i < lines.size(); ++i) {
    const absl::string_view word = absl::StripAsciiWhitespace(lines[i]);
    if (word.empty() || word.starts_with('#')) {
      continue;
    }
    // EnglishSpans takes 3 to 32 letters, LooksEnglish 3 or more.
    if (word.size() < 3 || word.size() > 32 ||
        !std::all_of(word.begin(), word.end(), [](char c) {
          return absl::ascii_isalpha(static_cast<unsigned char>(c));
        })) {
      user.left_out.push_back(absl::StrCat(i + 1, " 行目「", word, "」"));
      continue;
    }
    absl::StrAppend(&user.words, word, "\n");
  }
  return user;
}

std::shared_ptr<const EnglishWords> CurrentEnglishWords() {
  struct State {
    absl::Mutex mutex;
    std::string path;
    std::optional<FileWatch> watch;
    std::shared_ptr<const EnglishWords> words;
  };
  static absl::NoDestructor<State> state;
  const std::string path = UserFilePath(kEnglishWordsFile);
  absl::MutexLock lock(state->mutex);
  if (state->words != nullptr && path == state->path &&
      !state->watch->Changed()) {
    return state->words;
  }
  state->path = path;
  state->watch.emplace(path);
  // Without the file, the built-in list itself.
  state->words = std::shared_ptr<const EnglishWords>(
      &EnglishWords::Default(), [](const EnglishWords*) {});
  absl::StatusOr<std::string> text =
      path.empty() ? absl::NotFoundError("") : FileUtil::GetContents(path);
  if (!text.ok()) {
    return state->words;
  }
  const UserEnglishWords user = ParseUserEnglishWords(*text);
  if (!user.left_out.empty()) {
    ReportUserFileError(
        "floatype の英単語ファイルで使えない行があります",
        absl::StrCat(kEnglishWordsFile, " の ",
                     absl::StrJoin(user.left_out, "、"),
                     "：英字だけの 3〜32 文字の語が使えます"));
  }
  state->words = std::make_shared<const EnglishWords>(
      absl::StrCat(kEnglishWords, "\n", user.words));
  return state->words;
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

// Whether a character is the second half of the kana before it: a small kana
// not typed on its own with x or l (きょ, どぉ). The composer still splits
// their keys between the two (k|yo).
bool Joined(absl::string_view read, absl::string_view raw) {
  return absl::StrContains("ぁぃぅぇぉゃゅょゎ", read) && !read.empty() &&
         !raw.starts_with('x') && !raw.starts_with('l');
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
  // Plain Japanese, the common case, has no word to find.
  if (std::none_of(loose.begin(), loose.end(), [](bool b) { return b; })) {
    return candidates;
  }
  for (size_t begin = 0; begin < size; ++begin) {
    if ((begin > 0 && letter[begin - 1]) || Joined(query[begin], raws[begin])) {
      continue;  // Inside a run of letters, or of a kana.
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
      if ((end < size && (letter[end] || Joined(query[end], raws[end]))) ||
          shown == raw) {
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
