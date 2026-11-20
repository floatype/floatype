#include "floatype/english/english_words.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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
#include "absl/types/span.h"
#include "base/file_util.h"
#include "base/strings/unicode.h"
#include "base/util.h"
#include "floatype/config/settings_file.h"

namespace mozc::floatype {

namespace {
#include "floatype/english/english_words.inc"
#include "floatype/english/preset_words.inc"

// The lines of `text` in lower case, sorted, each once.
std::vector<std::string> SortedWords(absl::string_view text) {
  std::vector<std::string> words;
  for (const absl::string_view line :
       absl::StrSplit(text, '\n', absl::SkipEmpty())) {
    words.push_back(absl::AsciiStrToLower(line));
  }
  std::sort(words.begin(), words.end());
  words.erase(std::unique(words.begin(), words.end()), words.end());
  return words;
}

// The preset words, one a line.
const std::string& PresetWords() {
  static absl::NoDestructor<std::string> words(
      ParseUserEnglishWords(kPresetWords).words);
  return *words;
}
}  // namespace

EnglishWords::EnglishWords(absl::string_view text, absl::string_view user_text,
                           absl::string_view removed_text) {
  const std::vector<std::string> removed = SortedWords(removed_text);
  const auto kept = [&](std::vector<std::string> words) {
    std::vector<std::string> out;
    std::set_difference(words.begin(), words.end(), removed.begin(),
                        removed.end(), std::back_inserter(out));
    return out;
  };
  user_words_ = kept(SortedWords(user_text));
  words_ = kept(SortedWords(absl::StrCat(text, "\n", user_text)));
}

bool EnglishWords::Contains(absl::string_view word) const {
  return std::binary_search(words_.begin(), words_.end(),
                            absl::AsciiStrToLower(word));
}

bool EnglishWords::ContainsUser(absl::string_view word) const {
  return std::binary_search(user_words_.begin(), user_words_.end(),
                            absl::AsciiStrToLower(word));
}

const EnglishWords& EnglishWords::Default() {
  static absl::NoDestructor<EnglishWords> words(kEnglishWords, PresetWords());
  return *words;
}

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
      kEnglishWords, absl::StrCat(PresetWords(), user.words), user.removed);
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

bool ReadsAsJapanese(absl::string_view kana, const ReadingLookup& lookup) {
  // Particles a sentence may end or go on with, one kana each.
  constexpr absl::string_view kParticles =
      "はがをにでともへやかねよわなのさぞぜてば";
  std::vector<size_t> starts;  // Where each character starts.
  for (const absl::string_view ch : Utf8AsChars(kana)) {
    starts.push_back(ch.data() - kana.data());
  }
  starts.push_back(kana.size());
  const size_t n = starts.size() - 1;
  if (n == 0 || !lookup) {
    return true;
  }
  // reads[i]: the characters before i are covered.
  std::vector<bool> reads(n + 1, false);
  reads[0] = true;
  for (size_t i = 0; i < n; ++i) {
    if (!reads[i]) {
      continue;
    }
    if (absl::StrContains(
            kParticles, kana.substr(starts[i], starts[i + 1] - starts[i]))) {
      reads[i + 1] = true;
    }
    lookup(kana.substr(starts[i]), [&](absl::string_view reading) {
      const size_t end = starts[i] + reading.size();
      const auto it = std::lower_bound(starts.begin(), starts.end(), end);
      if (it != starts.end() && *it == end) {
        const size_t j = it - starts.begin();
        if (j >= i + 2) {
          reads[j] = true;
        }
      }
    });
  }
  return reads[n];
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

// Longer than any word worth keeping; bounds the work per key.
constexpr size_t kMaxLetters = 32;

bool IsKana(absl::string_view ch) {
  return Util::IsScriptType(ch, Util::HIRAGANA) || ch == "ー";
}

bool IsAsciiLetters(absl::string_view text) {
  return std::all_of(text.begin(), text.end(), [](char c) {
    return absl::ascii_isalpha(static_cast<unsigned char>(c));
  });
}

// How a stretch of kana reads as Japanese: in pieces of one kana (particles,
// endings and stems) and dictionary words of two morae or more, or of a kana
// with ゃ, ゅ or ょ (しょ, ちゃ). A small kana is no mora and, like ー, starts
// no piece: ふぃ and でぇ alone are no words, though the dictionary has them.
class KanaReader {
 public:
  // `chars`: the stretch, a kana each.
  KanaReader(absl::Span<const std::string> chars, const ReadingLookup& lookup)
      : lookup_(lookup), ends_(chars.size()) {
    morae_.push_back(0);
    for (const std::string& ch : chars) {
      offsets_.push_back(text_.size());
      text_ += ch;
      small_.push_back(IsSmall(ch));
      starts_.push_back(!small_.back() && ch != "ー");
      piece_.push_back(starts_.back() && absl::StrContains(kPieces, ch));
      contracted_.push_back(absl::StrContains("ゃゅょ", ch));
      morae_.push_back(morae_.back() + (small_.back() ? 0 : 1));
    }
    offsets_.push_back(text_.size());
  }

  size_t size() const { return small_.size(); }

  // Whether characters [begin, end) read as Japanese.
  bool Reads(size_t begin, size_t end) { return From(begin)[end]; }

  // The same after the kana `lead` (を, of keys typed again), read with them.
  bool ReadsAfter(absl::string_view lead, size_t begin, size_t end) {
    const auto [it, added] = after_.try_emplace({std::string(lead), begin});
    std::vector<bool>& reached = it->second;
    if (added) {
      reached.assign(size() + 1, false);
      std::vector<size_t> starts;  // Where each character of `lead` starts.
      for (const absl::string_view ch : Utf8AsChars(lead)) {
        starts.push_back(ch.data() - lead.data());
      }
      starts.push_back(lead.size());
      const size_t n = starts.size() - 1;
      std::vector<bool> in_lead(n + 1, false);
      in_lead[0] = true;
      const std::string text =
          absl::StrCat(lead, absl::string_view(text_).substr(offsets_[begin]));
      for (size_t i = 0; i < n; ++i) {
        const absl::string_view ch =
            lead.substr(starts[i], starts[i + 1] - starts[i]);
        if (!in_lead[i] || IsSmall(ch) || ch == "ー") {
          continue;
        }
        if (absl::StrContains(kPieces, ch)) {
          in_lead[i + 1] = true;
        }
        size_t morae_before = 0;  // Of the lead from i on.
        for (size_t j = i; j < n; ++j) {
          morae_before +=
              IsSmall(lead.substr(starts[j], starts[j + 1] - starts[j])) ? 0
                                                                         : 1;
        }
        lookup_(absl::string_view(text).substr(starts[i]),
                [&](absl::string_view reading) {
                  const size_t end = starts[i] + reading.size();
                  if (end <= lead.size()) {
                    const auto at =
                        std::lower_bound(starts.begin(), starts.end(), end);
                    // Within the lead, a word of two kana or more.
                    if (at != starts.end() && *at == end &&
                        static_cast<size_t>(at - starts.begin()) >= i + 2) {
                      in_lead[at - starts.begin()] = true;
                    }
                    return;
                  }
                  const size_t offset = offsets_[begin] + end - lead.size();
                  const auto at = std::lower_bound(offsets_.begin(),
                                                   offsets_.end(), offset);
                  if (at != offsets_.end() && *at == offset) {
                    const size_t j = at - offsets_.begin();
                    if (morae_before + morae_[j] - morae_[begin] >= 2) {
                      reached[j] = true;
                    }
                  }
                });
      }
      if (in_lead[n]) {
        reached[begin] = true;
      }
      for (size_t i = begin; i < size(); ++i) {
        if (reached[i]) {
          for (const size_t j : Ends(i)) {
            reached[j] = true;
          }
        }
      }
    }
    return reached[end];
  }

  // Whether the stretch from `begin` may be Japanese with its last word still
  // being typed: it reads, or reads up to where the rest starts a word.
  bool ReadsSoFar(size_t begin,
                  const std::function<bool(absl::string_view)>& starts_word) {
    const std::vector<bool>& reached = From(begin);
    if (reached[size()]) {
      return true;
    }
    for (size_t i = begin; starts_word && i < size(); ++i) {
      if (reached[i] && starts_[i] &&
          starts_word(absl::string_view(text_).substr(offsets_[i]))) {
        return true;
      }
    }
    return false;
  }

 private:
  // The particles, endings and stems of one kana.
  static constexpr absl::string_view kPieces =
      "はがをにでともへやかねよわなのさぞぜてば"
      "ただるうくいしきすせれらりんっけちみめえおあまこそどぬ";

  static bool IsSmall(absl::string_view ch) {
    return !ch.empty() && absl::StrContains("ぁぃぅぇぉゃゅょゎ", ch);
  }

  // Where the pieces that start at character `i` end, looked up once.
  const std::vector<size_t>& Ends(size_t i) {
    if (!ends_[i].has_value()) {
      std::vector<size_t>& ends = ends_[i].emplace();
      if (!starts_[i]) {
        return ends;
      }
      if (piece_[i]) {
        ends.push_back(i + 1);
      }
      if (lookup_) {
        lookup_(absl::string_view(text_).substr(offsets_[i]),
                [&](absl::string_view reading) {
                  const size_t end = offsets_[i] + reading.size();
                  const auto at =
                      std::lower_bound(offsets_.begin(), offsets_.end(), end);
                  if (at != offsets_.end() && *at == end) {
                    const size_t j = at - offsets_.begin();
                    if (morae_[j] - morae_[i] >= 2 ||
                        (j == i + 2 && contracted_[i + 1])) {
                      ends.push_back(j);
                    }
                  }
                });
      }
    }
    return *ends_[i];
  }

  // reached[j]: characters [begin, j) read as Japanese.
  const std::vector<bool>& From(size_t begin) {
    const auto [it, added] = from_.try_emplace(begin);
    std::vector<bool>& reached = it->second;
    if (added) {
      reached.assign(size() + 1, false);
      reached[begin] = true;
      for (size_t i = begin; i < size(); ++i) {
        if (reached[i]) {
          for (const size_t j : Ends(i)) {
            reached[j] = true;
          }
        }
      }
    }
    return reached;
  }

  const ReadingLookup& lookup_;
  std::string text_;
  std::vector<size_t> offsets_;  // Where each character starts, and the end.
  // By character: a small kana; may start a piece; is a piece alone; is ゃ,
  // ゅ or ょ.
  std::vector<bool> small_, starts_, piece_, contracted_;
  std::vector<size_t> morae_;  // Before each character, and in all.
  std::vector<std::optional<std::vector<size_t>>> ends_;
  std::map<size_t, std::vector<bool>> from_;
  std::map<std::pair<std::string, size_t>, std::vector<bool>> after_;
};

// The words inside the stretch of kana [first, last) of a composition, where
// it does not read as Japanese (see EnglishSpans()). `ends` says whether the
// stretch ends the composition; `letter_before` and `letter_after`, whether a
// letter is right beside it, which no word may touch.
std::vector<EnglishSpan> SpansInKana(const std::vector<std::string>& raws,
                                     const std::vector<std::string>& query,
                                     size_t first, size_t last, bool ends,
                                     bool letter_before, bool letter_after,
                                     const EnglishWords& words,
                                     const KanaSearch& search) {
  constexpr int kMaxWords = 3;
  const size_t n = last - first;
  KanaReader reader(absl::MakeConstSpan(query).subspan(first, n),
                    search.lookup);
  if (ends ? reader.ReadsSoFar(0, search.starts_word) : reader.Reads(0, n)) {
    return {};
  }
  const auto joined = [&](size_t i) {
    return i < n && Joined(query[first + i], raws[first + i]);
  };
  // A word of characters [begin, end); `lead` is the kana of `retyped`.
  struct Word {
    size_t begin, end;
    std::string word, retyped, lead;
  };
  std::vector<Word> found;
  for (size_t begin = 0; begin < n; ++begin) {
    if (joined(begin)) {
      continue;
    }
    std::string raw;
    size_t group = 0;  // The keys before the last kana of the span.
    for (size_t end = begin + 1; end <= n; ++end) {
      if (!joined(end - 1)) {
        group = raw.size();
      }
      raw += raws[first + end - 1];
      if (raw.size() > kMaxLetters + 4) {
        break;
      }
      if (joined(end)) {
        continue;  // Inside a kana.
      }
      // The word ends the span, or ends inside its last kana, whose other
      // keys start with a consonant (event|wo, never updat|e).
      for (size_t letters = std::min(raw.size(), kMaxLetters);
           letters > group && letters >= 3; --letters) {
        const absl::string_view word =
            absl::string_view(raw).substr(0, letters);
        if ((letters < raw.size() &&
             absl::StrContains("aiueo", raw[letters])) ||
            (letters == raw.size() && end == n && letter_after) ||
            (begin == 0 && letter_before) || !IsAsciiLetters(word) ||
            !words.Contains(word)) {
          continue;
        }
        std::string lead;
        if (letters < raw.size()) {
          if (search.kana_of_keys) {
            lead = search.kana_of_keys(absl::string_view(raw).substr(letters));
          }
          if (lead.empty()) {
            continue;
          }
        }
        found.push_back(
            {begin, end, std::string(word), raw.substr(letters), lead});
      }
    }
  }
  // The best way to reach each character with a word just before it, by the
  // kana waiting to be read after that word.
  struct Way {
    int words = 0;
    size_t letters = 0;
    std::vector<const Word*> spans;
  };
  const auto better = [](const Way& a, const Way& b) {
    return a.words != b.words ? a.words < b.words : a.letters > b.letters;
  };
  const auto reads = [&](size_t begin, const std::string& lead, size_t end) {
    return lead.empty() ? reader.Reads(begin, end)
                        : reader.ReadsAfter(lead, begin, end);
  };
  std::vector<std::map<std::string, Way>> ways(n + 1);
  ways[0][""] = Way();
  std::optional<Way> best;
  for (size_t at = 0; at <= n; ++at) {
    for (const auto& [lead, way] : ways[at]) {
      if (way.words > 0 && reads(at, lead, n) &&
          (!best.has_value() || better(way, *best))) {
        best = way;
      }
      if (way.words >= kMaxWords) {
        continue;
      }
      for (const Word& word : found) {
        // Kana lie between two words: letters do not run on unspaced.
        if (word.begin < at ||
            (way.words > 0 && lead.empty() && word.begin == at) ||
            !reads(at, lead, word.begin)) {
          continue;
        }
        Way next = way;
        ++next.words;
        next.letters += word.word.size();
        next.spans.push_back(&word);
        const auto [it, added] = ways[word.end].try_emplace(word.lead, next);
        if (!added && better(next, it->second)) {
          it->second = std::move(next);
        }
      }
    }
  }
  std::vector<EnglishSpan> spans;
  if (best.has_value()) {
    for (const Word* word : best->spans) {
      spans.push_back(
          {first + word->begin, first + word->end, word->word, word->retyped});
    }
  }
  return spans;
}

// EnglishSpans() without the words inside stretches of kana.
std::vector<EnglishSpan> SpansWithLetters(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    const std::function<bool(absl::string_view kana)>& reads_as_japanese,
    const KanaSearch* search) {
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
  // A run of kana with no letter beside it, typed with the keys of a word.
  if (reads_as_japanese) {
    const auto kana = [&](size_t i) { return IsKana(query[i]); };
    for (size_t begin = 0; begin < size;) {
      if (!kana(begin) || (begin > 0 && letter[begin - 1])) {
        ++begin;
        continue;
      }
      size_t end = begin;
      std::string raw, reading;
      while (end < size && kana(end)) {
        raw += raws[end];
        reading += query[end];
        ++end;
      }
      if ((end == size || !letter[end]) && raw.size() >= 3 &&
          raw.size() <= kMaxLetters &&
          std::all_of(raw.begin(), raw.end(),
                      [](char c) {
                        return absl::ascii_isalpha(
                            static_cast<unsigned char>(c));
                      }) &&
          words.Contains(raw) &&
          (words.ContainsUser(raw) || !reads_as_japanese(reading))) {
        candidates.push_back({begin, end, raw, ""});
      }
      begin = end;
    }
  }
  // Plain Japanese, the common case, has no other word to find.
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
      // Letters after the span would belong to it, but for a capital typed
      // with Shift, which starts the next word of a name (camelCase).
      const bool capital =
          end < size && raws[end].size() == 1 && preedit[end] == raws[end] &&
          absl::ascii_isupper(static_cast<unsigned char>(raws[end][0]));
      if ((end < size && !capital &&
           (letter[end] || Joined(query[end], raws[end]))) ||
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
          // ts starts つ, so the s of し (shi) after a t stays a letter and
          // the kana reads ひ: tests + ひます was typed as test + します.
          if (search != nullptr && letters < raw.size() &&
              word.ends_with('s') && raw[letters] == 'h' && letters >= 4 &&
              words.Contains(word.substr(0, letters - 1))) {
            candidates.push_back({begin, end,
                                  std::string(word.substr(0, letters - 1)),
                                  raw.substr(letters - 1)});
          }
        }
        break;  // The longest word for this span.
      }
    }
  }
  // With `search`, a candidate fits when the kana beside it read as Japanese:
  // those before it, and those after it with its keys typed again.
  // きょうはてsとぉかきます is 今日は + test + を書きます, not きょう + hates +
  // とぉ書きます.
  std::vector<bool> fits(candidates.size(), true);
  if (search != nullptr) {
    const absl::Span<const std::string> kana = absl::MakeConstSpan(query);
    for (size_t c = 0; c < candidates.size(); ++c) {
      const EnglishSpan& candidate = candidates[c];
      size_t first = candidate.begin;
      while (first > 0 && IsKana(query[first - 1])) {
        --first;
      }
      size_t last = candidate.end;
      while (last < size && IsKana(query[last])) {
        ++last;
      }
      if (first < candidate.begin) {
        KanaReader before(kana.subspan(first, candidate.begin - first),
                          search->lookup);
        fits[c] = before.Reads(0, before.size());
      }
      std::string lead;
      if (!candidate.retyped.empty()) {
        if (search->kana_of_keys) {
          lead = search->kana_of_keys(candidate.retyped);
        }
        if (lead.empty()) {
          fits[c] = false;  // No telling what the keys type.
        }
      }
      if (!fits[c] || (lead.empty() && last == candidate.end)) {
        continue;
      }
      KanaReader after(kana.subspan(candidate.end, last - candidate.end),
                       search->lookup);
      fits[c] = !lead.empty()
                    ? after.ReadsAfter(lead, 0, after.size())
                    : last == size
                          ? after.ReadsSoFar(0, search->starts_word)
                          : after.Reads(0, after.size());
    }
    // Leaves out the candidates for which `better` names a better one.
    const auto leave_out =
        [&](absl::FunctionRef<bool(const EnglishSpan& candidate,
                                   const EnglishSpan& other)>
                better) {
          std::vector<EnglishSpan> kept;
          std::vector<bool> kept_fits;
          for (size_t c = 0; c < candidates.size(); ++c) {
            bool out = false;
            for (size_t u = 0; u < candidates.size() && !out; ++u) {
              out = fits[u] && candidates[u].begin == candidates[c].begin &&
                    better(candidates[c], candidates[u]);
            }
            if (!out) {
              kept.push_back(candidates[c]);
              kept_fits.push_back(fits[c]);
            }
          }
          candidates = std::move(kept);
          fits = std::move(kept_fits);
        };
    // The s of し is not the word's: test + します, not tests + ひます.
    leave_out([](const EnglishSpan& candidate, const EnglishSpan& other) {
      return other.retyped.starts_with("sh") &&
             candidate.word.size() == other.word.size() + 1 &&
             candidate.word.ends_with('s') &&
             candidate.word.starts_with(other.word);
    });
    // A word that ends where a kana begins, and fits, keeps that kana whole,
    // rather than a longer word taking its first keys.
    leave_out([](const EnglishSpan& candidate, const EnglishSpan& other) {
      return !candidate.retyped.empty() && other.retyped.empty() &&
             other.end < candidate.end;
    });
  }
  // Those that fit first, then longer words; for the same word, the span
  // retyping least.
  std::vector<size_t> order(candidates.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(), [&](size_t i, size_t j) {
    const EnglishSpan& a = candidates[i];
    const EnglishSpan& b = candidates[j];
    return fits[i] != fits[j] ? fits[i]
           : a.word.size() != b.word.size()
               ? a.word.size() > b.word.size()
               : a.end - a.begin < b.end - b.begin;
  });
  std::vector<EnglishSpan> spans;
  for (const size_t i : order) {
    const EnglishSpan& candidate = candidates[i];
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

}  // namespace

std::vector<EnglishSpan> EnglishSpans(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    const std::function<bool(absl::string_view kana)>& reads_as_japanese,
    const KanaSearch* search) {
  std::vector<EnglishSpan> spans =
      SpansWithLetters(raws, query, preedit, words, reads_as_japanese, search);
  const size_t size = raws.size();
  if (search == nullptr || query.size() != size || preedit.size() != size) {
    return spans;
  }
  // 0: free. 1: in a span. 2: a loose letter in none, or the kana after a
  // span whose last keys are typed again, read with them.
  std::vector<int> taken(size + 1, 0);
  for (size_t i = 0; i < size; ++i) {
    if (IsLetter(query[i]) && preedit[i] != raws[i]) {
      taken[i] = 2;
    }
  }
  for (const EnglishSpan& span : spans) {
    std::fill(taken.begin() + span.begin, taken.begin() + span.end, 1);
    if (!span.retyped.empty() && taken[span.end] == 0) {
      taken[span.end] = 2;
    }
  }
  const auto in_stretch = [&](size_t i) {
    return i < size && taken[i] == 0 && IsKana(query[i]);
  };
  const size_t before = spans.size();
  for (size_t first = 0; first < size;) {
    if (!in_stretch(first)) {
      ++first;
      continue;
    }
    size_t last = first;
    std::string raw;
    while (in_stretch(last)) {
      raw += raws[last++];
    }
    // The kana a taken character goes on with are left alone, and so is a
    // run between other characters that is a word: it was looked at as a
    // whole. Beside the stretch is such a character, a letter kept as typed,
    // or a span.
    const bool beside = (first > 0 && taken[first - 1] == 2) ||
                        (last < size && taken[last] == 2);
    const auto other = [&](size_t i) {
      return taken[i] == 0 && !IsLetter(query[i]);
    };
    const bool letter_before = first > 0 && !other(first - 1);
    const bool letter_after = last < size && !other(last);
    if (!beside && !(reads_as_japanese && !letter_before && !letter_after &&
                     IsAsciiLetters(raw) && words.Contains(raw))) {
      for (EnglishSpan& span :
           SpansInKana(raws, query, first, last, last == size, letter_before,
                       letter_after, words, *search)) {
        spans.push_back(std::move(span));
      }
    }
    first = last;
  }
  if (spans.size() > before) {
    std::sort(spans.begin(), spans.end(),
              [](const EnglishSpan& a, const EnglishSpan& b) {
                return a.begin < b.begin;
              });
  }
  return spans;
}

std::optional<EnglishSpan> WholeEnglishWord(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    bool run, bool space) {
  const size_t size = raws.size();
  if ((!run && !space) || query.size() != size || preedit.size() != size) {
    return std::nullopt;
  }
  // The marks of prose after the word are not the word's.
  size_t end = size;
  while (end > 0 && raws[end - 1].size() == 1 &&
         absl::StrContains(".,!?;:", raws[end - 1])) {
    --end;
  }
  std::string word;
  bool shown = true, loose = false, kana = false;
  for (size_t i = 0; i < end; ++i) {
    word += raws[i];
    shown = shown && preedit[i] == raws[i];
    const bool letter = IsLetter(query[i]);
    loose = loose || (letter && preedit[i] != raws[i]);
    kana = kana || !letter;
  }
  if (word.empty() || shown) {
    return std::nullopt;
  }
  // -m, --help, -9: in a run, the option of a command.
  const size_t dashes = word.starts_with("--") ? 2 : word.starts_with("-");
  if (run && dashes > 0 && word.size() > dashes &&
      std::all_of(word.begin() + dashes, word.end(), [](char c) {
        return absl::ascii_isalnum(static_cast<unsigned char>(c));
      })) {
    return EnglishSpan{0, end, word, ""};
  }
  if (!IsAsciiLetters(word)) {
    return std::nullopt;
  }
  // The lists hold words of three letters or more.
  constexpr absl::string_view kShort =
      " a i am an as at be by do go he hi if in is it me my no of oh ok on or "
      "so to up us we ";
  const bool listed =
      word.size() <= 2
          ? absl::StrContains(
                kShort, absl::StrCat(" ", absl::AsciiStrToLower(word), " "))
          : words.Contains(word);
  if ((run && listed) || (space && listed && loose) ||
      (space && !kana && word.size() >= 2)) {
    return EnglishSpan{0, end, word, ""};
  }
  return std::nullopt;
}

std::optional<EnglishSpan> WordsRunTogether(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    const std::vector<EnglishSpan>& spans,
    const std::function<bool(absl::string_view kana)>& reads_as_japanese) {
  const size_t size = raws.size();
  if (query.size() != size || preedit.size() != size || !reads_as_japanese) {
    return std::nullopt;
  }
  std::string keys;
  // A letter the romaji left over with kana after it: no syllable still
  // being typed, so the keys are not plain Japanese.
  bool letter_inside = false, letter_seen = false;
  bool shown = true;
  for (size_t i = 0; i < size; ++i) {
    keys += raws[i];
    const bool stays = IsLetter(query[i]) && preedit[i] != raws[i];
    letter_inside = letter_inside || (letter_seen && !stays);
    letter_seen = letter_seen || stays;
    shown = shown && preedit[i] == raws[i];
  }
  constexpr size_t kMaxKeys = 40;
  if (shown || keys.size() < 6 || keys.size() > kMaxKeys ||
      !IsAsciiLetters(keys)) {
    return std::nullopt;
  }
  // What the words found so far leave: letters the romaji left over, but for
  // those at the end, which may yet make a kana, and stretches of kana.
  std::vector<bool> covered(size, false);
  for (const EnglishSpan& span : spans) {
    if (!span.retyped.empty() || span.end > size) {
      return std::nullopt;
    }
    std::fill(covered.begin() + span.begin, covered.begin() + span.end, true);
  }
  const auto loose = [&](size_t i) {
    return IsLetter(query[i]) && preedit[i] != raws[i];
  };
  bool japanese = true;
  for (size_t i = 0; i < size && japanese;) {
    if (covered[i] || preedit[i] == raws[i]) {
      ++i;
      continue;
    }
    if (loose(i)) {
      japanese = false;
      break;
    }
    // A stretch of kana: Japanese where it reads so, but for the keys of a
    // word the user added (frontぱげ is frontpage).
    std::string kana, typed;
    while (i < size && !covered[i] && IsKana(query[i])) {
      typed += raws[i];
      kana += query[i++];
    }
    japanese = !kana.empty() && reads_as_japanese(kana) &&
               !words.ContainsUser(typed);
  }
  if (japanese) {
    return std::nullopt;
  }
  // The fewest words that make up the keys: of three letters or more, and
  // where the keys are surely not plain Japanese, of two that leave a letter
  // when typed alone (is, it, of; not no, to, go, which are の, と, ご after
  // an English word). おきてねがおみてた is no ok + it + ...
  constexpr absl::string_view kShort =
      " am as at by if is it my of oh ok or up us ";
  const std::string lower = absl::AsciiStrToLower(keys);
  constexpr int kNone = 1 << 20;
  std::vector<int> fewest(lower.size() + 1, kNone);
  fewest[0] = 0;
  for (size_t at = 0; at < lower.size(); ++at) {
    if (fewest[at] == kNone) {
      continue;
    }
    for (size_t length = 2; at + length <= lower.size() && length <= 24;
         ++length) {
      const absl::string_view word =
          absl::string_view(lower).substr(at, length);
      if (length == 2 ? letter_inside && absl::StrContains(
                                             kShort, absl::StrCat(" ", word, " "))
                      : words.Contains(word)) {
        fewest[at + length] = std::min(fewest[at + length], fewest[at] + 1);
      }
    }
  }
  if (fewest.back() == kNone || fewest.back() < 2) {
    return std::nullopt;
  }
  return EnglishSpan{0, size, keys, ""};
}

std::vector<EnglishSpan> JoinedNames(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit, const EnglishWords& words,
    const std::vector<EnglishSpan>& spans) {
  const size_t size = raws.size();
  if (query.size() != size || preedit.size() != size) {
    return {};
  }
  std::vector<bool> covered(size, false);
  for (const EnglishSpan& span : spans) {
    if (span.retyped.empty() && span.end <= size) {
      std::fill(covered.begin() + span.begin, covered.begin() + span.end,
                true);
    }
  }
  const auto joins = [&](size_t i) {
    return raws[i].size() == 1 && absl::StrContains("._/@:=-", raws[i]) &&
           !covered[i] && (query[i] == "ー" || !IsKana(query[i]));
  };
  // The names of files and of hosts end in these.
  constexpr absl::string_view kEndings =
      " cc cpp hpp mm rs go py rb js ts tsx jsx mjs java kt swift sh zsh pl "
      "php lua sql md txt csv tsv json yaml yml toml xml html htm css scss svg "
      "png jpg jpeg gif pdf zip gz tar log lock conf cfg ini env proto bzl "
      "gguf nix vim tex com org net jp io dev ai app ";
  struct Piece {
    size_t begin, end;
    std::string keys;
    char before = 0, after = 0;  // The symbols beside it.
    bool english = true;   // Shows as typed, or will: letters kept, spans.
    bool plain = true;     // Kana and loose letters only, with keys a to z, 0 to 9.
    bool loose = false;    // Holds a letter the romaji left over.
  };
  std::vector<Piece> pieces;
  for (size_t i = 0; i < size;) {
    if (joins(i)) {
      ++i;
      continue;
    }
    Piece piece = {i, i, ""};
    piece.before = i > 0 ? raws[i - 1][0] : 0;
    for (; i < size && !joins(i); ++i) {
      piece.keys += raws[i];
      const bool as_typed = covered[i] || preedit[i] == raws[i];
      const bool letter = IsLetter(query[i]);
      piece.english = piece.english && as_typed;
      piece.plain = piece.plain && !covered[i] && (letter || IsKana(query[i]));
      piece.loose = piece.loose || (letter && !as_typed);
    }
    piece.end = i;
    piece.after = i < size ? raws[i][0] : 0;
    piece.plain = piece.plain &&
                  std::all_of(piece.keys.begin(), piece.keys.end(), [](char c) {
                    return absl::ascii_isalnum(static_cast<unsigned char>(c));
                  });
    // Digits alone tell nothing: 3。して is no 3.site.
    piece.english = piece.english &&
                    std::any_of(piece.keys.begin(), piece.keys.end(), [](char c) {
                      return absl::ascii_isalpha(static_cast<unsigned char>(c));
                    });
    pieces.push_back(std::move(piece));
  }
  if (pieces.size() < 2) {
    return {};
  }
  const auto listed = [&](const Piece& piece) {
    return piece.plain && piece.keys.size() >= 3 &&
           IsAsciiLetters(piece.keys) && words.Contains(piece.keys);
  };
  const auto ending = [&](const Piece& piece) {
    return piece.plain && piece.loose && piece.before == '.' &&
           absl::StrContains(
               kEndings,
               absl::StrCat(" ", absl::AsciiStrToLower(piece.keys), " "));
  };
  std::vector<EnglishSpan> names;
  const auto name = [&](Piece& piece) {
    piece.english = true;
    names.push_back({piece.begin, piece.end, piece.keys, ""});
  };
  // a.b.c: single letters, one of them no kana and not the last.
  if (pieces.size() >= 3 &&
      std::all_of(pieces.begin(), pieces.end(),
                  [](const Piece& piece) {
                    return piece.keys.size() == 1 &&
                           absl::ascii_isalpha(
                               static_cast<unsigned char>(piece.keys[0]));
                  }) &&
      std::any_of(pieces.begin(), pieces.end() - 1,
                  [](const Piece& piece) { return piece.loose; })) {
    for (Piece& piece : pieces) {
      if (!piece.english) {
        name(piece);
      }
    }
    return names;
  }
  // Twice, so a name reaches the piece after the next (usr/local/bin).
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t p = 0; p < pieces.size(); ++p) {
      Piece& piece = pieces[p];
      if (piece.english || !piece.plain) {
        continue;
      }
      Piece* const left = p > 0 ? &pieces[p - 1] : nullptr;
      Piece* const right = p + 1 < pieces.size() ? &pieces[p + 1] : nullptr;
      const bool after_english = left != nullptr && left->english;
      const bool before_english = right != nullptr && right->english;
      if (ending(piece) && left != nullptr &&
          (left->english || listed(*left))) {
        // main.cc: the ending tells that main is a name too.
        if (!left->english) {
          name(*left);
        }
        name(piece);
      } else if (listed(piece) && piece.after != '-' &&
                 ((after_english && piece.before != '.') ||
                  (before_english && piece.after != '.'))) {
        // node_modules, src/main; not それ。test or test。それ, which may be
        // two sentences, nor えらーlog, where ー is the long vowel of エラー.
        name(piece);
      } else if (before_english && piece.after == '@') {
        name(piece);  // taro@example.com
      } else if (piece.loose && right != nullptr &&
                 (after_english || before_english)) {
        name(piece);  // usr/local: no kana are typed so, and more follows.
      }
    }
  }
  return names;
}

std::vector<EnglishSpan> SymbolsAsTyped(
    const std::vector<std::string>& raws, const std::vector<std::string>& query,
    const std::vector<std::string>& preedit,
    const std::vector<EnglishSpan>& spans, bool prose) {
  const size_t size = raws.size();
  if (query.size() != size || preedit.size() != size) {
    return {};
  }
  std::vector<bool> covered(size, false);
  bool english = false;
  for (const EnglishSpan& span : spans) {
    if (!span.retyped.empty() || span.end > size) {
      return {};  // Kana follow the word.
    }
    std::fill(covered.begin() + span.begin, covered.begin() + span.end, true);
    english = true;
  }
  // The symbols at the end may close a Japanese sentence (test。).
  constexpr absl::string_view kClosing = ".,-~/:!?;";
  size_t closing = size;
  while (closing > 0 && !covered[closing - 1] && raws[closing - 1].size() == 1 &&
         absl::StrContains(kClosing, raws[closing - 1])) {
    --closing;
  }
  std::vector<EnglishSpan> symbols;
  for (size_t i = 0; i < size; ++i) {
    if (covered[i]) {
      continue;
    }
    if (raws[i].size() != 1) {
      return {};
    }
    const char c = raws[i][0];
    if (absl::ascii_isalpha(static_cast<unsigned char>(c))) {
      if (preedit[i] != raws[i]) {
        return {};  // A loose letter, or a kana of one key.
      }
      english = true;
      continue;
    }
    // With kana input a symbol's key types a kana. ー is the - key's.
    if (!absl::ascii_isgraph(static_cast<unsigned char>(c)) ||
        (query[i] == "ー" ? c != '-' : IsKana(query[i]))) {
      return {};
    }
    if (preedit[i] == raws[i]) {
      continue;
    }
    const bool joins = absl::ascii_isdigit(static_cast<unsigned char>(c)) ||
                       absl::StrContains("._/@:=-$#+&*%^\\|~<>", c);
    if (i >= closing ? prose && absl::StrContains(".,!?;:", c) : joins) {
      symbols.push_back({i, i + 1, raws[i], ""});
    }
  }
  return english ? symbols : std::vector<EnglishSpan>();
}

}  // namespace mozc::floatype
