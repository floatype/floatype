#include "floatype/zenz/zenz_corrector.h"

#ifdef __APPLE__
#include <Carbon/Carbon.h>  // IsSecureEventInputEnabled
#endif                      // __APPLE__

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/clock.h"
#include "base/file_util.h"
#include "base/number_util.h"
#include "base/strings/japanese.h"
#include "base/strings/unicode.h"
#include "base/system_util.h"
#include "base/util.h"
#include "config/character_form_manager.h"
#include "converter/attribute.h"
#include "converter/segments.h"
#include "floatype/scorer/protocol.h"
#include "floatype/zenz/letter_reading.h"
#include "floatype/zenz/number_reading.h"
#include "floatype/zenz/scorer_client.h"
#include "floatype/zenz/zenz_memory.h"

namespace mozc::floatype {
namespace {

using ::floatype::scorer::Request;
using ::floatype::scorer::Response;

bool default_client_enabled = false;
ScorerClient* default_client_for_testing = nullptr;
bool ignore_secure_input_for_testing = false;
bool pretend_secure_input_for_testing = false;
// See ZenzCorrector::SetLearningRulesForMeasuring().
constexpr int kKeepsDisplacedOut = 1;
constexpr int kKeepsOutOfRuns = 2;
constexpr int kKeepsCuts = 4;
std::optional<int> learning_rules_for_measuring;
// See ZenzCorrector::SetContextCharsForMeasuring().
std::optional<size_t> context_chars_for_measuring;

// Whether the user's history put another candidate first than Mozc's best,
// where `rule` about such a segment is on.
bool LearnedFromUser(const Segment& segment, int rule, bool keeps_learned) {
  return (learning_rules_for_measuring.value_or(keeps_learned ? 7 : 0) &
          rule) != 0 &&
         segment.candidates_size() > 0 &&
         (segment.candidate(0).attributes &
          converter::Attribute::USER_SEGMENT_HISTORY_REWRITER);
}

// Searches segment by segment for candidates that spell `text` from `pos` on.
// `values[i][j]` is candidate j of conversion segment i, or empty when it must
// not be used. `dead_ends` remembers (segment, position) pairs that fail.
bool Search(const std::vector<std::vector<std::string>>& values,
            absl::string_view text, size_t segment, size_t pos,
            std::vector<int>& chosen,
            absl::flat_hash_set<std::pair<size_t, size_t>>& dead_ends) {
  if (segment == values.size()) {
    return pos == text.size();
  }
  if (dead_ends.contains({segment, pos})) {
    return false;
  }
  for (int i = 0; i < static_cast<int>(values[segment].size()); ++i) {
    const std::string& value = values[segment][i];
    if (value.empty()) {
      continue;
    }
    if (!absl::StartsWith(text.substr(pos), value)) {
      continue;
    }
    chosen[segment] = i;
    if (Search(values, text, segment + 1, pos + value.size(), chosen,
               dead_ends)) {
      return true;
    }
  }
  dead_ends.insert({segment, pos});
  return false;
}

// The conversion's reading: its segment keys in hiragana.
std::string Reading(const Segments& segments) {
  std::string reading;
  for (const Segment& segment : segments.conversion_segments()) {
    reading += segment.key();
  }
  return reading;
}

// Runs of digits and letters, half width, as in 2026, gpt-5 or 3.14.
std::vector<std::string> AsciiTokens(absl::string_view text) {
  const std::string folded = japanese::FullWidthAsciiToHalfWidthAscii(text);
  std::vector<std::string> tokens;
  std::string token;
  for (const char c : folded) {
    if (absl::ascii_isalnum(static_cast<unsigned char>(c)) ||
        (!token.empty() && absl::StrContains("._+#-", c))) {
      token += c;
    } else if (!token.empty()) {
      tokens.push_back(std::move(token));
      token.clear();
    }
  }
  if (!token.empty()) {
    tokens.push_back(std::move(token));
  }
  return tokens;
}

// Whether zenz keeps the digits and letters the user typed where Mozc wrote
// them, as Mozkey requires; where Mozc lost them, zenz may bring them back.
// Numbers zenz writes from kana (さんがつ/３月) are its to choose.
bool KeepsTypedAscii(const Segments& segments, absl::string_view conversion) {
  std::string mozc;
  for (const Segment& segment : segments.conversion_segments()) {
    if (segment.candidates_size() > 0) {
      mozc += segment.candidate(0).value;
    }
  }
  const std::vector<std::string> mozc_tokens = AsciiTokens(mozc);
  const std::vector<std::string> zenz_tokens = AsciiTokens(conversion);
  for (const std::string& typed : AsciiTokens(Reading(segments))) {
    const auto mozc_count =
        std::count(mozc_tokens.begin(), mozc_tokens.end(), typed);
    if (mozc_count > 0 &&
        mozc_count !=
            std::count(zenz_tokens.begin(), zenz_tokens.end(), typed)) {
      return false;
    }
  }
  return true;
}

// Identifies the conversion an answer is for: its segment keys and the
// candidates shown at request time.
std::string Fingerprint(const Segments& segments) {
  std::string fingerprint;
  for (const Segment& segment : segments.conversion_segments()) {
    absl::StrAppend(
        &fingerprint, segment.key(), "\t",
        segment.candidates_size() > 0 ? segment.candidate(0).value : "", "\n");
  }
  return fingerprint;
}

uint64_t NextId() {
  static uint64_t last_id = 0;
  return ++last_id;
}

// A request goes in one write to a socket whose buffer holds 8KB (macOS's
// net.local.stream.sendspace), and a partial write stops the scorer, so
// "not this" ends where its exclusions would take more than this.
constexpr size_t kMaxRejectedBytes = 4096;

bool FitsRequest(const std::vector<std::string>& rejected) {
  size_t bytes = 0;
  for (const std::string& conversion : rejected) {
    bytes += conversion.size() + 1;
  }
  return bytes <= kMaxRejectedBytes;
}

}  // namespace

namespace {

// The candidates MapToCandidates() may use, per conversion segment.
std::vector<std::vector<std::string>> CandidateValues(const Segments& segments,
                                                      bool keeps_learned) {
  // Full-width and half-width ASCII compare equal, so that among candidates
  // differing only in width, the one Mozc ranks higher wins: symbols, digits
  // and letters keep Mozc's width (spec section 4).
  std::vector<std::vector<std::string>> values;
  for (const Segment& segment : segments.conversion_segments()) {
    std::vector<std::string>& folded = values.emplace_back();
    // Where the user's history put another candidate first, the user chose
    // it over Mozc's best, which zenz does not bring back (機械 learned over
    // 機会). Any other candidate zenz may choose: a word learned by mistake
    // does not shut zenz out of the segment.
    // The same writing may be several candidates (感じ of 感じる and the
    // noun), so it is the writing that is kept out.
    std::string displaced;
    for (size_t i = 1;
         LearnedFromUser(segment, kKeepsDisplacedOut, keeps_learned) &&
         displaced.empty() && i < segment.candidates_size();
         ++i) {
      if (segment.candidate(i).attributes &
          converter::Attribute::BEST_CANDIDATE) {
        displaced = segment.candidate(i).value;
      }
    }
    for (size_t i = 0; i < segment.candidates_size(); ++i) {
      const converter::Candidate& candidate = segment.candidate(i);
      // A candidate with another key covers a different part of the reading.
      folded.push_back(
          candidate.key == segment.key() &&
                  (displaced.empty() || candidate.value != displaced)
              ? japanese::FullWidthAsciiToHalfWidthAscii(candidate.value)
              : "");
    }
  }
  return values;
}

}  // namespace

std::string ConversionReading(const Segments& segments) {
  return Reading(segments);
}

std::optional<std::vector<int>> MapToCandidates(const Segments& segments,
                                                absl::string_view text,
                                                bool keeps_learned) {
  const std::vector<std::vector<std::string>> values =
      CandidateValues(segments, keeps_learned);
  std::vector<int> chosen(values.size(), 0);
  absl::flat_hash_set<std::pair<size_t, size_t>> dead_ends;
  if (values.empty() ||
      !Search(values, japanese::FullWidthAsciiToHalfWidthAscii(text), 0, 0,
              chosen, dead_ends)) {
    return std::nullopt;
  }
  return chosen;
}

std::optional<Resegmentation> ResegmentationFor(const Segments& segments,
                                                absl::string_view text,
                                                bool keeps_learned) {
  const std::vector<std::vector<std::string>> values =
      CandidateValues(segments, keeps_learned);
  const size_t n = values.size();
  const std::string folded = japanese::FullWidthAsciiToHalfWidthAscii(text);
  // chars[b] is the number of characters before byte b, or -1 inside one.
  std::vector<int> chars(folded.size() + 1, -1);
  int count = 0;
  for (size_t b = 0; b <= folded.size(); ++b) {
    if (b == folded.size() || (folded[b] & 0xC0) != 0x80) {
      chars[b] = count++;
    }
  }
  // Aligns the segments with `text`. A segment spells its part with one of
  // its candidates; a number, segments whose kanji numeral candidates
  // together read as the digits in `text` (千|九百|七|十|二 for 1972); and a
  // run, segments that do neither, any part of `text` that is not empty and
  // has no more characters than their reading. The alignment that spells
  // the most reading wins. best[i][b] is the reading spelled up to segment i
  // and byte b, where no run ends; from[i][b] is the state before and whether
  // a number led there.
  struct From {
    size_t segment = 0;
    size_t byte = 0;
    bool number = false;
  };
  std::vector<std::vector<int>> best(n + 1,
                                     std::vector<int>(folded.size() + 1, -1));
  std::vector<std::vector<From>> from(n + 1,
                                      std::vector<From>(folded.size() + 1));
  const auto relax = [&](size_t segment, size_t byte, int spelled,
                         From before) {
    if (spelled > best[segment][byte]) {
      best[segment][byte] = spelled;
      from[segment][byte] = before;
    }
  };
  // spells[j]: the parts of `text`, as [start, end) bytes in order, that a
  // candidate of segment j spells.
  std::vector<std::vector<std::pair<size_t, size_t>>> spells(n);
  for (size_t j = 0; j < n; ++j) {
    for (size_t start = 0; start < folded.size(); ++start) {
      if (chars[start] < 0) {
        continue;
      }
      for (const std::string& value : values[j]) {
        if (!value.empty() &&
            absl::StartsWith(absl::string_view(folded).substr(start), value)) {
          spells[j].emplace_back(start, start + value.size());
        }
      }
    }
  }
  int end_best = -1;
  From end_from;  // A run to the end starts here.
  best[0][0] = 0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t b = 0; b <= folded.size(); ++b) {
      if (best[i][b] < 0) {
        continue;
      }
      // Segments i..j-1 read as a number.
      std::string numerals;
      int number_chars = 0;
      for (size_t j = i; j < n; ++j) {
        const Segment& segment = segments.conversion_segment(j);
        const auto numeral = std::find_if(
            values[j].begin(), values[j].end(), [](const std::string& value) {
              std::string kanji, digits;
              return Util::IsScriptType(value, Util::KANJI) &&
                     NumberUtil::NormalizeNumbers(value, true, &kanji, &digits);
            });
        if (LearnedFromUser(segment, kKeepsOutOfRuns, keeps_learned) ||
            numeral == values[j].end()) {
          break;
        }
        numerals += *numeral;
        number_chars += static_cast<int>(segment.key_len());
        std::string kanji, digits;
        if (NumberUtil::NormalizeNumbers(numerals, true, &kanji, &digits) &&
            absl::StartsWith(absl::string_view(folded).substr(b), digits)) {
          relax(j + 1, b + digits.size(), best[i][b] + number_chars,
                {i, b, true});
        }
      }
      // Segments i..j-1 form the run, followed by segment j.
      size_t run_chars = 0;
      for (size_t j = i; j <= n; ++j) {
        if (j > i) {
          // A segment the user's history decided keeps its candidate.
          if (LearnedFromUser(segments.conversion_segment(j - 1),
                              kKeepsOutOfRuns, keeps_learned)) {
            break;
          }
          run_chars += segments.conversion_segment(j - 1).key_len();
        }
        if (j == n) {
          const int left = count - 1 - chars[b];
          if (j > i && left > 0 && left <= static_cast<int>(run_chars) &&
              best[i][b] > end_best) {
            end_best = best[i][b];
            end_from = {i, b, false};
          }
          break;
        }
        for (auto it = std::lower_bound(spells[j].begin(), spells[j].end(),
                                        std::make_pair(b, size_t{0}));
             it != spells[j].end(); ++it) {
          const int run_length = chars[it->first] - chars[b];
          if (run_length > static_cast<int>(run_chars) ||
              (j == i && run_length > 0)) {
            break;
          }
          if (j > i && run_length == 0) {
            continue;
          }
          relax(j + 1, it->second,
                best[i][b] +
                    static_cast<int>(segments.conversion_segment(j).key_len()),
                {i, b, false});
        }
      }
    }
  }
  // Marks the new segments: group[k] is where segment k's new segment
  // starts, and numbers[k] the digits of a number starting at segment k.
  std::vector<size_t> group(n);
  std::vector<std::string> numbers(n);
  std::vector<bool> in_run(n, false);
  for (size_t k = 0; k < n; ++k) {
    group[k] = k;
  }
  size_t segment = n;
  size_t byte = folded.size();
  if (end_best > best[n][folded.size()]) {
    for (size_t k = end_from.segment; k < n; ++k) {
      group[k] = end_from.segment;
      in_run[k] = true;
    }
    segment = end_from.segment;
    byte = end_from.byte;
  } else if (best[n][folded.size()] < 0) {
    return std::nullopt;
  }
  bool changed = segment < n;
  while (segment > 0) {
    const From before = from[segment][byte];
    if (before.number) {
      for (size_t k = before.segment; k < segment; ++k) {
        group[k] = before.segment;
      }
      numbers[before.segment] = folded.substr(before.byte, byte - before.byte);
      changed = true;
    } else if (before.segment + 1 < segment) {
      for (size_t k = before.segment; k + 1 < segment; ++k) {
        group[k] = before.segment;
        in_run[k] = true;
      }
      changed = true;
    }
    segment = before.segment;
    byte = before.byte;
  }
  if (!changed) {
    return std::nullopt;
  }
  // A run of one segment whose candidates do not spell its part has its
  // boundary wrong, so it takes in the segment after it, or the one before
  // at the end, when that one spells its own part.
  const auto alone = [&](size_t k) {
    return group[k] == k && (k + 1 == n || group[k + 1] != k) &&
           numbers[k].empty() &&
           !LearnedFromUser(segments.conversion_segment(k), kKeepsOutOfRuns,
                            keeps_learned);
  };
  for (size_t k = 0; k < n; ++k) {
    if (!in_run[k] || !alone(k)) {
      continue;
    }
    if (k + 1 < n && !in_run[k + 1] && alone(k + 1)) {
      group[k + 1] = k;
    } else if (k > 0 && !in_run[k - 1] && alone(k - 1)) {
      group[k] = k - 1;
    }
  }
  Resegmentation resegmentation;
  for (size_t k = 0; k < n; ++k) {
    const Segment& segment = segments.conversion_segment(k);
    if (k > 0 && group[k] == group[k - 1]) {
      const size_t size = resegmentation.sizes.back() + segment.key_len();
      if (size > std::numeric_limits<uint8_t>::max()) {
        return std::nullopt;
      }
      resegmentation.sizes.back() = static_cast<uint8_t>(size);
      continue;
    }
    resegmentation.sizes.push_back(static_cast<uint8_t>(segment.key_len()));
    std::string& number = resegmentation.numbers.emplace_back(numbers[k]);
    // In the width Mozc writes digits in for the number's first segment.
    for (int i = 0; i < segment.candidates_size() && !number.empty(); ++i) {
      const std::string& value = segment.candidate(i).value;
      if (Util::IsScriptType(value, Util::NUMBER)) {
        if (value != japanese::FullWidthAsciiToHalfWidthAscii(value)) {
          number = japanese::HalfWidthAsciiToFullWidthAscii(number);
        }
        break;
      }
    }
  }
  return resegmentation;
}

namespace {

// The hiragana a kana character of zenz's text reads as, or "" for others.
std::string KanaReading(absl::string_view ch) {
  if (ch == "ー") {
    return std::string(ch);
  }
  std::string hiragana = japanese::KatakanaToHiragana(ch);
  return Util::IsScriptType(hiragana, Util::HIRAGANA) ? hiragana : "";
}

bool IsHiragana(absl::string_view ch) {
  return Util::IsScriptType(ch, Util::HIRAGANA);
}

// Key lengths, in characters, of segments ending at `cuts` (character
// positions in a reading of `length` characters, increasing, inside it).
std::optional<std::vector<uint8_t>> SizesAt(absl::Span<const size_t> cuts,
                                            size_t length) {
  std::vector<uint8_t> sizes;
  size_t start = 0;
  for (const size_t cut : cuts) {
    sizes.push_back(static_cast<uint8_t>(cut - start));
    start = cut;
  }
  sizes.push_back(static_cast<uint8_t>(length - start));
  for (const size_t size : sizes) {
    if (size == 0) {
      return std::nullopt;
    }
  }
  if (length - start > std::numeric_limits<uint8_t>::max() ||
      std::any_of(cuts.begin(), cuts.end(), [&](size_t cut) {
        return cut > std::numeric_limits<uint8_t>::max();
      })) {
    return std::nullopt;
  }
  return sizes;
}

// Where zenz's text cuts a reading, and the numbers it writes in digits.
struct ZenzCutting {
  // Character positions in the reading, increasing, inside it.
  std::vector<size_t> cuts;
  // A number the reading says in kana, characters [begin, end), and `text`
  // writes in digits.
  struct Number {
    size_t begin;
    size_t end;
    std::string digits;
  };
  std::vector<Number> numbers;
};

// A piece of zenz's text: a kana, a run of digits, or a run of other
// characters (kanji, letters, symbols).
struct TextPiece {
  enum Kind { kKana, kDigits, kOther };
  Kind kind;
  // kKana: the hiragana it reads as. kDigits: the digits, half width.
  std::string text;
  // Written in hiragana; ー counts as what it follows.
  bool hiragana;
};

std::vector<TextPiece> TextPieces(absl::string_view text) {
  std::vector<TextPiece> pieces;
  const std::string folded = japanese::FullWidthAsciiToHalfWidthAscii(text);
  for (const absl::string_view ch : Utf8AsChars(folded)) {
    if (ch.size() == 1 && absl::ascii_isdigit(static_cast<unsigned char>(ch[0]))) {
      if (pieces.empty() || pieces.back().kind != TextPiece::kDigits) {
        pieces.push_back({TextPiece::kDigits, "", false});
      }
      pieces.back().text += ch;
    } else if (std::string kana = KanaReading(ch); !kana.empty()) {
      const bool hiragana = ch == "ー"
                                ? !pieces.empty() && pieces.back().hiragana
                                : IsHiragana(ch);
      pieces.push_back({TextPiece::kKana, std::move(kana), hiragana});
    } else if (pieces.empty() || pieces.back().kind != TextPiece::kOther) {
      pieces.push_back({TextPiece::kOther, "", false});
    }
  }
  return pieces;
}

// The value of a run of digits, or nullopt for one written with a leading
// zero (a code, not a number said aloud) or too long to say.
std::optional<uint64_t> ValueOfDigits(absl::string_view digits) {
  uint64_t value = 0;
  if (digits.size() > 12 || (digits.size() > 1 && digits[0] == '0') ||
      !absl::SimpleAtoi(digits, &value)) {
    return std::nullopt;
  }
  return value;
}

// Matches `text` to `reading` (hiragana): each kana of `text` reads as the
// next character; digits read as the same digits, typed, or as kana that say
// their number (ReadsAsNumber()), and as nothing else; a run of other
// characters reads as one or more characters. The first match found,
// shortest runs first, decides.
// Cuts where `text` goes from hiragana to another script, and around the
// numbers said in kana. Returns nullopt when nothing matches.
std::optional<ZenzCutting> ZenzCuttingFor(absl::string_view reading,
                                          absl::string_view text) {
  std::vector<std::string> r;
  const std::string folded = japanese::FullWidthAsciiToHalfWidthAscii(reading);
  for (const absl::string_view ch : Utf8AsChars(folded)) {
    r.emplace_back(ch);
  }
  const std::vector<TextPiece> pieces = TextPieces(text);
  ZenzCutting cutting;
  const auto cut_at = [&](size_t position) {
    if (position > 0 && position < r.size() &&
        (cutting.cuts.empty() || cutting.cuts.back() != position)) {
      cutting.cuts.push_back(position);
    }
  };
  absl::flat_hash_set<std::pair<size_t, size_t>> dead_ends;
  std::function<bool(size_t, size_t)> match = [&](size_t i, size_t j) -> bool {
    if (i == pieces.size()) {
      return j == r.size();
    }
    if (j == r.size() || dead_ends.contains({i, j})) {
      return false;
    }
    const size_t cuts_before = cutting.cuts.size();
    const size_t numbers_before = cutting.numbers.size();
    const TextPiece& piece = pieces[i];
    if (i > 0 && pieces[i - 1].hiragana && !piece.hiragana) {
      cut_at(j);
    }
    if (piece.kind == TextPiece::kKana) {
      if (r[j] == piece.text && match(i + 1, j + 1)) {
        return true;
      }
    } else if (piece.kind == TextPiece::kDigits) {
      // Digits read as nothing else, so those zenz made up (８時間 as ０時間)
      // match nowhere. Digits the user typed are in the reading as they are.
      size_t typed = 0;
      while (typed < piece.text.size() && j + typed < r.size() &&
             r[j + typed] == piece.text.substr(typed, 1)) {
        ++typed;
      }
      if (typed == piece.text.size() && match(i + 1, j + typed)) {
        return true;
      }
      if (const std::optional<uint64_t> value = ValueOfDigits(piece.text);
          value.has_value()) {
        const size_t cuts_here = cutting.cuts.size();
        std::string said;
        for (size_t m = j + 1; m <= r.size(); ++m) {
          said += r[m - 1];
          if (!ReadsAsNumber(said, *value)) {
            continue;
          }
          cut_at(j);
          cut_at(m);
          cutting.numbers.push_back({j, m, piece.text});
          if (match(i + 1, m)) {
            return true;
          }
          cutting.cuts.resize(cuts_here);
          cutting.numbers.resize(numbers_before);
        }
      }
    } else {
      for (size_t m = j + 1; m <= r.size(); ++m) {
        if (match(i + 1, m)) {
          return true;
        }
      }
    }
    cutting.cuts.resize(cuts_before);
    cutting.numbers.resize(numbers_before);
    dead_ends.insert({i, j});
    return false;
  };
  if (r.empty() || !match(0, 0)) {
    return std::nullopt;
  }
  return cutting;
}

// The digits to add to each segment of a reading cut at `cuts`: those of the
// number that is the whole segment, in the width the config gives digits, or
// "".
std::vector<std::string> NumbersOfSegments(
    absl::Span<const size_t> cuts, size_t length,
    absl::Span<const ZenzCutting::Number> numbers) {
  std::vector<std::string> digits;
  size_t begin = 0;
  for (size_t i = 0; i <= cuts.size(); ++i) {
    const size_t end = i < cuts.size() ? cuts[i] : length;
    const auto number = std::find_if(
        numbers.begin(), numbers.end(), [&](const ZenzCutting::Number& n) {
          return n.begin == begin && n.end == end;
        });
    digits.push_back(
        number == numbers.end()
            ? ""
            : config::CharacterFormManager::GetCharacterFormManager()
                  ->ConvertConversionString(number->digits));
    begin = end;
  }
  return digits;
}

// Whether Mozc wrote the whole reading as one word of the dictionary
// (ひともうけ as 一儲け): its segmentation then stands.
bool IsOneWord(const Segments& segments, const WordLookup& words) {
  if (!words || segments.conversion_segments_size() != 1 ||
      segments.conversion_segment(0).candidates_size() == 0) {
    return false;
  }
  const Segment& segment = segments.conversion_segment(0);
  const std::string value =
      japanese::FullWidthAsciiToHalfWidthAscii(segment.candidate(0).value);
  // The reading left in kana is no word Mozc chose (もうこんなじかん).
  if (japanese::KatakanaToHiragana(value) == segment.key()) {
    return false;
  }
  bool found = false;
  words(segment.key(),
        [&](absl::string_view reading, absl::string_view writing) {
          found = found ||
                  (reading == segment.key() &&
                   japanese::FullWidthAsciiToHalfWidthAscii(writing) == value);
        });
  return found;
}

size_t HiraganaCount(absl::string_view text) {
  size_t count = 0;
  for (const absl::string_view ch : Utf8AsChars(text)) {
    count += IsHiragana(ch) ? 1 : 0;
  }
  return count;
}

// ZenzCuttingFor(), with every character of `text` that is not kana, a typed
// character, a number or a letter said by its name read by a word of the
// dictionary: 不忠者 over ふちゅうしゃ as 不忠 and 者, 行き over ゆき as one
// word. Longer words first. Cuts fall between words only. `writings` are what
// `text` writes between the cuts.
// Returns nullopt when `text` does not read as `reading` this way.
struct CuttingByWords {
  ZenzCutting cutting;
  std::vector<std::string> writings;
};
std::optional<CuttingByWords> ZenzCuttingByWords(absl::string_view reading,
                                                 absl::string_view text,
                                                 const WordLookup& words) {
  std::vector<std::string> r, t;
  const std::string folded_reading =
      japanese::FullWidthAsciiToHalfWidthAscii(reading);
  for (const absl::string_view ch : Utf8AsChars(folded_reading)) {
    r.emplace_back(ch);
  }
  // offsets[j]: where character j of the reading starts.
  std::vector<size_t> offsets(r.size() + 1, 0);
  for (size_t j = 0; j < r.size(); ++j) {
    offsets[j + 1] = offsets[j] + r[j].size();
  }
  for (const absl::string_view ch : Utf8AsChars(text)) {
    t.emplace_back(ch);
  }
  // folded: `text` in half-width ASCII; starts[i]: where character i starts
  // in it; index_at: the character that starts at a byte.
  std::string folded;
  std::vector<size_t> starts(t.size() + 1, 0);
  absl::flat_hash_map<size_t, size_t> index_at;
  std::vector<bool> hiragana(t.size(), false);
  for (size_t i = 0; i < t.size(); ++i) {
    starts[i] = folded.size();
    index_at[folded.size()] = i;
    folded += japanese::FullWidthAsciiToHalfWidthAscii(t[i]);
    hiragana[i] = t[i] == "ー" ? i > 0 && hiragana[i - 1] : IsHiragana(t[i]);
  }
  starts[t.size()] = folded.size();
  index_at[folded.size()] = t.size();

  // The words that start at character j of the reading: their length in
  // characters and their writing in half-width ASCII, longest first.
  struct Word {
    size_t length;
    std::string writing;
  };
  absl::flat_hash_map<size_t, std::vector<Word>> words_at;
  const auto words_from = [&](size_t j) -> const std::vector<Word>& {
    const auto [it, inserted] = words_at.try_emplace(j);
    if (inserted) {
      words(absl::string_view(folded_reading).substr(offsets[j]),
            [&](absl::string_view key, absl::string_view writing) {
              it->second.push_back(
                  {Util::CharsLen(key),
                   japanese::FullWidthAsciiToHalfWidthAscii(writing)});
            });
      std::stable_sort(it->second.begin(), it->second.end(),
                       [](const Word& a, const Word& b) {
                         return a.length > b.length;
                       });
    }
    return it->second;
  };

  CuttingByWords result;
  ZenzCutting& cutting = result.cutting;
  // text_cuts[k]: the character of `text` that cutting.cuts[k] comes before.
  std::vector<size_t> text_cuts;
  const auto cut_at = [&](size_t position, size_t text_position) {
    if (position > 0 && position < r.size() &&
        (cutting.cuts.empty() || cutting.cuts.back() != position)) {
      cutting.cuts.push_back(position);
      text_cuts.push_back(text_position);
    }
  };
  absl::flat_hash_set<std::pair<size_t, size_t>> dead_ends;
  std::function<bool(size_t, size_t)> match = [&](size_t i, size_t j) -> bool {
    if (i == t.size()) {
      return j == r.size();
    }
    if (j == r.size() || dead_ends.contains({i, j})) {
      return false;
    }
    const size_t cuts_before = cutting.cuts.size();
    const size_t numbers_before = cutting.numbers.size();
    if (i > 0 && hiragana[i - 1] && !hiragana[i]) {
      cut_at(j, i);
    }
    const size_t cuts_here = cutting.cuts.size();
    const absl::string_view rest = absl::string_view(folded).substr(starts[i]);
    for (const Word& word : words_from(j)) {
      if (absl::StartsWith(rest, word.writing) &&
          match(index_at.at(starts[i] + word.writing.size()),
                j + word.length)) {
        return true;
      }
    }
    if (absl::ascii_isdigit(static_cast<unsigned char>(rest[0]))) {
      size_t k = i;
      std::string digits;
      while (k < t.size() && folded[starts[k]] >= '0' &&
             folded[starts[k]] <= '9') {
        digits += folded[starts[k]];
        ++k;
      }
      if (const std::optional<uint64_t> value = ValueOfDigits(digits);
          value.has_value()) {
        std::string said;
        for (size_t m = j + 1; m <= r.size(); ++m) {
          said += r[m - 1];
          if (!ReadsAsNumber(said, *value)) {
            continue;
          }
          cut_at(j, i);
          cut_at(m, k);
          cutting.numbers.push_back({j, m, digits});
          if (match(k, m)) {
            return true;
          }
          cutting.cuts.resize(cuts_here);
          text_cuts.resize(cuts_here);
          cutting.numbers.resize(numbers_before);
        }
      }
    }
    // A letter said by its name (じぇーあーる for JR).
    for (const absl::string_view name : LetterNames(rest[0])) {
      if (absl::StartsWith(
              absl::string_view(folded_reading).substr(offsets[j]), name) &&
          match(i + 1, j + Util::CharsLen(name))) {
        return true;
      }
    }
    // A hyphen written between letters and digits is not said (X-5 for
    // えっくすふぁいぶ).
    if (rest[0] == '-' && i > 0 && i + 1 < t.size() &&
        absl::ascii_isalnum(static_cast<unsigned char>(folded[starts[i - 1]])) &&
        absl::ascii_isalnum(static_cast<unsigned char>(folded[starts[i + 1]])) &&
        match(i + 1, j)) {
      return true;
    }
    // A kana, or a character typed as it is.
    if ((KanaReading(t[i]) == r[j] ||
         absl::string_view(folded).substr(starts[i],
                                          starts[i + 1] - starts[i]) == r[j]) &&
        match(i + 1, j + 1)) {
      return true;
    }
    cutting.cuts.resize(cuts_before);
    text_cuts.resize(cuts_before);
    cutting.numbers.resize(numbers_before);
    dead_ends.insert({i, j});
    return false;
  };
  if (r.empty() || t.empty() || !match(0, 0)) {
    return std::nullopt;
  }
  size_t begin = 0;
  for (size_t k = 0; k <= text_cuts.size(); ++k) {
    const size_t end = k < text_cuts.size() ? text_cuts[k] : t.size();
    std::string& writing = result.writings.emplace_back();
    for (size_t i = begin; i < end; ++i) {
      writing += t[i];
    }
    begin = end;
  }
  return result;
}

}  // namespace

std::optional<std::vector<uint8_t>> ZenzSegmentSizes(absl::string_view reading,
                                                     absl::string_view text) {
  const std::optional<ZenzCutting> cutting = ZenzCuttingFor(reading, text);
  if (!cutting.has_value() || cutting->cuts.empty()) {
    return std::nullopt;
  }
  return SizesAt(cutting->cuts, Util::CharsLen(reading));
}

std::optional<Mapping> MapWithResegmentation(const Segments& segments,
                                             absl::string_view text,
                                             const Resegmenter& resegment,
                                             const WordLookup& words,
                                             bool keeps_learned) {
  if (std::optional<std::vector<int>> indices =
          MapToCandidates(segments, text, keeps_learned);
      indices.has_value()) {
    return Mapping{*std::move(indices), std::nullopt};
  }
  if (!resegment || IsOneWord(segments, words)) {
    return std::nullopt;
  }
  // Converts again with segments of `sizes` and, where given, a writing
  // added to each new segment's candidates, and maps `text` onto them.
  const auto map_resized =
      [&](absl::Span<const uint8_t> sizes,
          absl::Span<const std::string> added) -> std::optional<Mapping> {
    std::optional<Segments> resegmented = resegment(sizes);
    if (!resegmented.has_value() ||
        resegmented->conversion_segments_size() != sizes.size()) {
      return std::nullopt;
    }
    // After Mozc's own, so Mozc's candidate shows where it writes the same.
    for (size_t i = 0; i < added.size(); ++i) {
      if (added[i].empty()) {
        continue;
      }
      Segment* segment = resegmented->mutable_conversion_segment(i);
      converter::Candidate* candidate = segment->add_candidate();
      candidate->key = candidate->content_key = segment->key();
      candidate->value = candidate->content_value = added[i];
      if (segment->candidates_size() > 1) {
        candidate->lid = segment->candidate(0).lid;
        candidate->rid = segment->candidate(0).rid;
      }
    }
    std::optional<std::vector<int>> indices =
        MapToCandidates(*resegmented, text, keeps_learned);
    if (!indices.has_value()) {
      return std::nullopt;
    }
    return Mapping{*std::move(indices), *std::move(resegmented)};
  };
  // The digits of a number Mozc wrote in kanji numerals, one per segment,
  // are a candidate of the segment that now holds all of it.
  if (const std::optional<Resegmentation> resegmentation =
          ResegmentationFor(segments, text, keeps_learned);
      resegmentation.has_value()) {
    if (std::optional<Mapping> mapping =
            map_resized(resegmentation->sizes, resegmentation->numbers);
        mapping.has_value()) {
      return mapping;
    }
  }
  // Cutting as zenz cuts lets Mozc's dictionary write what Mozc's own
  // boundaries hid (がかんせいど|たかい: が|完成度高い). The user's history
  // decided the segments it ranked, so those stay.
  if (std::any_of(segments.conversion_segments().begin(),
                  segments.conversion_segments().end(),
                  [keeps_learned](const Segment& segment) {
                    return LearnedFromUser(segment, kKeepsCuts, keeps_learned);
                  })) {
    return std::nullopt;
  }
  const std::string reading = Reading(segments);
  const size_t length = Util::CharsLen(reading);
  if (const std::optional<ZenzCutting> cutting =
          ZenzCuttingFor(reading, text);
      cutting.has_value() && !cutting->cuts.empty()) {
    if (const std::optional<std::vector<uint8_t>> sizes =
            SizesAt(cutting->cuts, length);
        sizes.has_value()) {
      if (std::optional<Mapping> mapping = map_resized(
              *sizes,
              NumbersOfSegments(cutting->cuts, length, cutting->numbers));
          mapping.has_value()) {
        return mapping;
      }
    }
    // A segment zenz did not cut may still hold words Mozc cut apart. A
    // number stays whole.
    std::vector<size_t> both = cutting->cuts;
    size_t end = 0;
    for (const Segment& segment : segments.conversion_segments()) {
      end += segment.key_len();
      if (end < length &&
          std::none_of(cutting->numbers.begin(), cutting->numbers.end(),
                       [&](const ZenzCutting::Number& number) {
                         return number.begin < end && end < number.end;
                       })) {
        both.push_back(end);
      }
    }
    std::sort(both.begin(), both.end());
    both.erase(std::unique(both.begin(), both.end()), both.end());
    if (both != cutting->cuts) {
      if (const std::optional<std::vector<uint8_t>> sizes =
              SizesAt(both, length);
          sizes.has_value()) {
        if (std::optional<Mapping> mapping = map_resized(
                *sizes, NumbersOfSegments(both, length, cutting->numbers));
            mapping.has_value()) {
          return mapping;
        }
      }
    }
  }
  // Where Mozc's candidates still do not write it, zenz's own writing shows
  // if the dictionary's words read it as the reading (千歳発羽田行き over
  // ちとせはつはねだゆき). An answer that writes in kana what Mozc wrote in
  // kanji is zenz not knowing a word more often than Mozc misreading one
  // (かたから for 方から), so it does not show this way.
  if (!words) {
    return std::nullopt;
  }
  std::string mozc;
  for (const Segment& segment : segments.conversion_segments()) {
    mozc += segment.candidate(0).value;
  }
  if (HiraganaCount(text) > HiraganaCount(mozc)) {
    return std::nullopt;
  }
  const std::optional<CuttingByWords> by_words =
      ZenzCuttingByWords(reading, text, words);
  if (!by_words.has_value()) {
    return std::nullopt;
  }
  const std::optional<std::vector<uint8_t>> sizes =
      SizesAt(by_words->cutting.cuts, length);
  if (!sizes.has_value()) {
    return std::nullopt;
  }
  std::vector<std::string> added = NumbersOfSegments(
      by_words->cutting.cuts, length, by_words->cutting.numbers);
  for (size_t i = 0; i < added.size(); ++i) {
    if (added[i].empty()) {
      added[i] = by_words->writings[i];
    }
  }
  return map_resized(*sizes, added);
}

void ZenzCorrector::Configure(bool stop_when_idle) {
  if (client_ == nullptr) {
    return;
  }
  std::vector<std::string> arguments;
  if (stop_when_idle) {
    arguments.insert(arguments.end(),
                     {"--idle_exit_seconds", std::to_string(kIdleExitSeconds)});
  }
  client_->SetArguments(std::move(arguments));
}

void ZenzCorrector::Warm() {
  if (client_ != nullptr) {
    client_->Warm();
  }
}

bool ZenzCorrector::Request(const Segments& segments,
                            absl::string_view text_before, bool not_this) {
  pending_.reset();
  // What looks private goes no further than here: not to the scorer, and
  // not into what is kept of the request.
  std::string left_context = SafeContext(text_before);
  if (context_chars_for_measuring.has_value()) {
    const size_t length = Util::CharsLen(left_context);
    const size_t kept = std::min(length, *context_chars_for_measuring);
    left_context =
        std::string(Util::Utf8SubString(left_context, length - kept, kept));
  }
  if (client_ == nullptr || segments.conversion_segments_size() == 0) {
    return false;
  }
  // What is typed while secure input is on stays out of zenz (spec section 4).
  if (SecureInput()) {
    return false;
  }
  const std::string reading = Reading(segments);
  // Longer readings are slow to convert and may not fit the scorer's context.
  if (Util::CharsLen(reading) > kMaxReadingChars) {
    return false;
  }
  if (std::string key = absl::StrCat(reading, "\t", left_context);
      !not_this || key != rejected_for_) {
    rejected_for_ = std::move(key);
    rejected_.clear();
  }
  // Mozc's conversion as the draft: the scorer checks it in one pass, which
  // takes AJIMEE-Bench's median from 24.6ms to 20.4ms with the same answers
  // but for 2 of 200 (batched arithmetic), both wrong either way.
  std::string draft;
  for (const Segment& segment : segments.conversion_segments()) {
    if (segment.candidates_size() > 0) {
      draft += segment.candidate(0).value;
    }
  }
  if (not_this) {
    Reject(draft);
    if (!FitsRequest(rejected_)) {
      return false;
    }
  }
  Pending pending{{NextId(), std::string(left_context),
                   japanese::HiraganaToKatakana(reading), std::move(draft),
                   not_this ? rejected_ : std::vector<std::string>()},
                  Clock::GetAbslTime(),
                  Fingerprint(segments),
                  not_this};
  if (!client_->Send(pending.request)) {
    return false;
  }
  pending_ = std::move(pending);
  return true;
}

ZenzCorrector::PollResult ZenzCorrector::Poll(const Segments& segments,
                                              const Resegmenter& resegment,
                                              const WordLookup& words) {
  if (!pending_.has_value() || Fingerprint(segments) != pending_->fingerprint) {
    Cancel();
    return {PollResult::kDone, {}};
  }
  const std::optional<Response> response = client_->Poll();
  if (!response.has_value() || response->id != pending_->request.id) {
    if (Clock::GetAbslTime() - pending_->sent >= timeout_) {
      Cancel();
      return {PollResult::kDone, {}};
    }
    return {PollResult::kWait, {}};
  }
  Pending pending = *std::move(pending_);
  Cancel();
  PollResult result =
      Judge(segments, *response, pending.request.left_context, resegment,
            words);
  if (result.kind == PollResult::kApply || response->conversion.empty()) {
    return result;
  }
  // An answer that shows nothing new is not asked for again.
  Reject(response->conversion);
  if (pending.not_this && pending.asks < kMaxAsks && FitsRequest(rejected_)) {
    pending.request.id = NextId();
    pending.request.excluded = rejected_;
    ++pending.asks;
    if (client_->Send(pending.request)) {
      pending_ = std::move(pending);
      return {PollResult::kWait, {}};
    }
  }
  return result;
}

namespace {

bool IsAsciiGraph(char c) { return c > 0x20 && c < 0x7f; }

// Whether a run of ASCII characters with no space in it looks private.
bool LooksPrivate(absl::string_view run) {
  size_t digits = 0;
  size_t letters = 0;
  size_t slashes = 0;
  size_t digits_in_a_row = 0;
  size_t most_in_a_row = 0;
  for (const char c : run) {
    digits_in_a_row = absl::ascii_isdigit(c) ? digits_in_a_row + 1 : 0;
    most_in_a_row = std::max(most_in_a_row, digits_in_a_row);
    digits += absl::ascii_isdigit(c);
    letters += absl::ascii_isalpha(c);
    slashes += c == '/';
  }
  // A URL, or a mail address.
  if (absl::StrContains(run, "://") ||
      absl::StartsWithIgnoreCase(run, "www.")) {
    return true;
  }
  if (const size_t at = run.find('@');
      at != absl::string_view::npos && at > 0 &&
      run.find('.', at) != absl::string_view::npos) {
    return true;
  }
  // A file path. Two slashes with no letter are a date (2026/11/20).
  if ((slashes >= 2 && letters > 0) || absl::StrContains(run, "\\") ||
      absl::StartsWith(run, "~/") || absl::StartsWith(run, "./") ||
      absl::StartsWith(run, "../")) {
    return true;
  }
  // A key or a token: long with digits among the letters, or very long.
  if ((run.size() >= 16 && digits > 0 && letters > 0) || run.size() >= 32) {
    return true;
  }
  // A long number: an account, a card, a telephone number with or without
  // hyphens. A date with hyphens has 8 digits (2026-11-20).
  return most_in_a_row >= 8 || digits >= 10;
}

}  // namespace

std::string SafeContext(absl::string_view context) {
  size_t keep = 0;
  // Digits of runs of digits a space apart: a card number in fours.
  size_t spaced_digits = 0;
  size_t last_end = 0;
  for (size_t i = 0; i < context.size();) {
    if (!IsAsciiGraph(context[i])) {
      ++i;
      continue;
    }
    size_t end = i;
    while (end < context.size() && IsAsciiGraph(context[end])) {
      ++end;
    }
    const absl::string_view run = context.substr(i, end - i);
    const bool all_digits = std::all_of(
        run.begin(), run.end(), [](char c) { return absl::ascii_isdigit(c); });
    spaced_digits =
        !all_digits ? 0
        : spaced_digits > 0 && i == last_end + 1 && context[last_end] == ' '
            ? spaced_digits + run.size()
            : run.size();
    if (LooksPrivate(run) || spaced_digits >= 10) {
      keep = end;
    }
    last_end = end;
    i = end;
  }
  return std::string(absl::StripLeadingAsciiWhitespace(context.substr(keep)));
}

bool UsesTurnedDown(const Segments& segments, const Mapping& mapping) {
  const Segments& shown =
      mapping.segments.has_value() ? *mapping.segments : segments;
  const ZenzMemory& memory = ZenzMemory::TurnedDown();
  for (size_t i = 0; i < mapping.candidate_indices.size() &&
                     i < shown.conversion_segments_size();
       ++i) {
    const int index = mapping.candidate_indices[i];
    const Segment& segment = shown.conversion_segment(i);
    if ((index == 0 && !mapping.segments.has_value()) ||
        !segment.is_valid_index(index)) {
      continue;
    }
    const converter::Candidate& candidate = segment.candidate(index);
    if (memory.Contains(candidate.content_key, candidate.content_value)) {
      return true;
    }
  }
  return false;
}

namespace {

bool IsPinned(const converter::Candidate& candidate) {
  // Latest() is empty for a reading without a pin.
  return !candidate.content_key.empty() && !candidate.content_value.empty() &&
         ZenzMemory::Pinned().Latest(candidate.content_key) ==
             candidate.content_value;
}

}  // namespace

bool KeepPinned(const Segments& segments, Mapping& mapping) {
  if (!mapping.segments.has_value()) {
    for (size_t i = 0; i < mapping.candidate_indices.size() &&
                       i < segments.conversion_segments_size();
         ++i) {
      const Segment& segment = segments.conversion_segment(i);
      if (segment.candidates_size() > 0 && IsPinned(segment.candidate(0))) {
        mapping.candidate_indices[i] = 0;
      }
    }
    return true;
  }
  // Every pinned word Mozc shows must be among the words written.
  for (const Segment& segment : segments.conversion_segments()) {
    if (segment.candidates_size() == 0 || !IsPinned(segment.candidate(0))) {
      continue;
    }
    const converter::Candidate& pinned = segment.candidate(0);
    bool written = false;
    for (size_t i = 0; i < mapping.candidate_indices.size() &&
                       i < mapping.segments->conversion_segments_size();
         ++i) {
      const Segment& shown = mapping.segments->conversion_segment(i);
      const int index = mapping.candidate_indices[i];
      if (shown.is_valid_index(index) &&
          shown.candidate(index).content_key == pinned.content_key &&
          shown.candidate(index).content_value == pinned.content_value) {
        written = true;
        break;
      }
    }
    if (!written) {
      return false;
    }
  }
  return true;
}

std::string MappedText(const Segments& segments, const Mapping& mapping) {
  const Segments& shown =
      mapping.segments.has_value() ? *mapping.segments : segments;
  std::string text;
  for (size_t i = 0; i < mapping.candidate_indices.size() &&
                     i < shown.conversion_segments_size();
       ++i) {
    const Segment& segment = shown.conversion_segment(i);
    if (segment.is_valid_index(mapping.candidate_indices[i])) {
      absl::StrAppend(&text,
                      segment.candidate(mapping.candidate_indices[i]).value);
    }
  }
  return text;
}

ZenzCorrector::PollResult ZenzCorrector::Judge(
    const Segments& segments, const Response& response,
    absl::string_view left_context, const Resegmenter& resegment,
    const WordLookup& words) const {
  // A length cap means zenz did not finish its answer.
  if (response.stop != "eog" && response.stop != "tag") {
    return {PollResult::kDone, {}};
  }
  // zenz hands the reading back unconverted when it does not know the words,
  // where Mozc's dictionary usually does. On Mozc's regression set this halves
  // the conversions zenz makes worse (20 to 10), at the cost of 2 of 48 wins.
  // After other text, hiragana is how zenz reads the sentence (なの, not 名の;
  // けますと, not 毛マスト), unless Mozc wrote the reading as one word of the
  // dictionary (けいさん for 計算). Katakana is still a word zenz does not
  // know (バウンシャ for 馬運車).
  if (const std::string reading =
          japanese::FullWidthAsciiToHalfWidthAscii(Reading(segments)),
      answer = japanese::FullWidthAsciiToHalfWidthAscii(response.conversion);
      japanese::KatakanaToHiragana(answer) == reading &&
      (left_context.empty() || answer != reading ||
       IsOneWord(segments, words))) {
    return {PollResult::kDone, {}};
  }
  if (!KeepsTypedAscii(segments, response.conversion)) {
    return {PollResult::kDone, {}};
  }
  // Rules measured and left out (floatype/eval/regression.py on Mozc's
  // regression set, ajimee.py --engine replay on AJIMEE-Bench), each keeping
  // Mozc's candidate in a segment where zenz would write:
  // - the same word in kana instead of kanji or back (ほう/方): 3 harms
  //   fewer, 5 wins lost;
  // - digits or letters Mozc did not (百均/１００均): 1 harm fewer, but
  //   AJIMEE-Bench drops from 82.0% to 81.0% (３年９月１５日, ＭＩ６);
  // - symbols or emoji Mozc did not: no harm fewer, AJIMEE-Bench 81.5%.
  // Nor does zenz's confidence (the lowest log probability among its tokens)
  // tell wins from harms: at the best threshold, -0.7, 1 harm fewer and 9
  // wins lost.
  // Proper nouns (by Mozc's part of speech) were tried too: keeping Mozc's
  // took the regression set from 254 to 252 right, and refusing zenz's
  // traded a harm for a win; both made AJIMEE-Bench's MinCER worse.
  std::optional<Mapping> mapping = MapWithResegmentation(
      segments, response.conversion, resegment, words, keeps_learned_);
  if (!mapping.has_value() ||
      (!mapping->segments.has_value() &&
       std::all_of(mapping->candidate_indices.begin(),
                   mapping->candidate_indices.end(),
                   [](int index) { return index == 0; }))) {
    return {PollResult::kDone, {}};
  }
  // A correction the user stopped before is not made again, nor one that
  // writes a word the user chose another candidate over.
  std::string reading = Reading(segments);
  if (learning_ &&
      (ZenzMemory::Stopped().Contains(reading, response.conversion) ||
       UsesTurnedDown(segments, *mapping))) {
    return {PollResult::kDone, {}};
  }
  // A word the user pinned stays, whether or not zenz learns.
  const std::vector<int> indices = mapping->candidate_indices;
  if (!KeepPinned(segments, *mapping) ||
      (!mapping->segments.has_value() &&
       std::all_of(mapping->candidate_indices.begin(),
                   mapping->candidate_indices.end(),
                   [](int index) { return index == 0; }))) {
    return {PollResult::kDone, {}};
  }
  std::string text = mapping->candidate_indices == indices
                         ? response.conversion
                         : MappedText(segments, *mapping);
  // Kept back to a pinned word, the text is not zenz's answer: it may be one
  // the user stopped (which stores the text shown) or turned away with Ctrl+z.
  if ((learning_ && ZenzMemory::Stopped().Contains(reading, text)) ||
      std::find(rejected_.begin(), rejected_.end(), text) != rejected_.end()) {
    return {PollResult::kDone, {}};
  }
  return {PollResult::kApply, std::move(mapping->candidate_indices),
          std::move(reading), std::move(text), std::move(mapping->segments)};
}

void ZenzCorrector::Reject(absl::string_view conversion) {
  if (std::find(rejected_.begin(), rejected_.end(), conversion) ==
      rejected_.end()) {
    rejected_.emplace_back(conversion);
  }
}

ScorerClient* ZenzCorrector::DefaultClient() {
  if (default_client_for_testing != nullptr) {
    return default_client_for_testing;
  }
  if (!default_client_enabled) {
    return nullptr;
  }
  // Never destroyed: the scorer exits when the converter process does.
  static ScorerClient* const client = [] {
    const std::string dir = SystemUtil::GetServerDirectory();
    // The repacked weights are mapped from their file, so the scorer holds
    // no copy of the model in memory of its own (scorer_main.cc).
    return new ScorerClient({
        FileUtil::JoinPath(dir, "floatype_scorer"),
        "--model",
        FileUtil::JoinPath(dir, "zenz-v3.2-small-Q5_K_M.gguf"),
        "--repacked",
        FileUtil::JoinPath(dir, "zenz-v3.2-small-Q5_K_M.repacked"),
    });
  }();
  return client;
}

namespace {
bool session_features_enabled = false;
}  // namespace

void EnableSessionFeatures(bool enabled) { session_features_enabled = enabled; }
bool SessionFeaturesEnabled() { return session_features_enabled; }

void ZenzCorrector::EnableDefaultClient() { default_client_enabled = true; }

void ZenzCorrector::SetDefaultClientForTesting(ScorerClient* client) {
  default_client_for_testing = client;
}

void ZenzCorrector::SetLearningRulesForMeasuring(std::optional<int> rules) {
  learning_rules_for_measuring = rules;
}

void ZenzCorrector::SetContextCharsForMeasuring(std::optional<size_t> chars) {
  context_chars_for_measuring = chars;
}

void ZenzCorrector::IgnoreSecureInputForTesting(bool ignore) {
  ignore_secure_input_for_testing = ignore;
}

void ZenzCorrector::PretendSecureInputForTesting(bool on) {
  pretend_secure_input_for_testing = on;
}

bool ZenzCorrector::SecureInput() {
  if (pretend_secure_input_for_testing) {
    return true;
  }
#ifdef __APPLE__
  return !ignore_secure_input_for_testing && IsSecureEventInputEnabled();
#else   // __APPLE__
  return false;
#endif  // __APPLE__
}

}  // namespace mozc::floatype
