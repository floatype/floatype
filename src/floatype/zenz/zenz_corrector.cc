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

bool KeepsTopCandidate(const Segment& segment) {
  return segment.candidates_size() > 0 &&
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
std::vector<std::vector<std::string>> CandidateValues(
    const Segments& segments) {
  // Full-width and half-width ASCII compare equal, so that among candidates
  // differing only in width, the one Mozc ranks higher wins: symbols, digits
  // and letters keep Mozc's width (spec section 4).
  std::vector<std::vector<std::string>> values;
  for (const Segment& segment : segments.conversion_segments()) {
    std::vector<std::string>& folded = values.emplace_back();
    const int limit = KeepsTopCandidate(segment)
                          ? 1
                          : static_cast<int>(segment.candidates_size());
    for (int i = 0; i < limit; ++i) {
      const converter::Candidate& candidate = segment.candidate(i);
      // A candidate with another key covers a different part of the reading.
      folded.push_back(
          candidate.key == segment.key()
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
                                                absl::string_view text) {
  const std::vector<std::vector<std::string>> values =
      CandidateValues(segments);
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
                                                absl::string_view text) {
  const std::vector<std::vector<std::string>> values =
      CandidateValues(segments);
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
        if (KeepsTopCandidate(segment) || numeral == values[j].end()) {
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
          if (KeepsTopCandidate(segments.conversion_segment(j - 1))) {
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
           !KeepsTopCandidate(segments.conversion_segment(k));
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
                                             const Resegmenter& resegment) {
  if (std::optional<std::vector<int>> indices = MapToCandidates(segments, text);
      indices.has_value()) {
    return Mapping{*std::move(indices), std::nullopt};
  }
  if (!resegment) {
    return std::nullopt;
  }
  // Converts again with segments of `sizes` and, where given, digits added
  // to each new segment's candidates, and maps `text` onto them.
  const auto map_resized =
      [&](absl::Span<const uint8_t> sizes,
          absl::Span<const std::string> numbers) -> std::optional<Mapping> {
    std::optional<Segments> resegmented = resegment(sizes);
    if (!resegmented.has_value() ||
        resegmented->conversion_segments_size() != sizes.size()) {
      return std::nullopt;
    }
    // The digits of a number Mozc wrote in kanji numerals, one per segment,
    // are a candidate of the segment that now holds all of it.
    for (size_t i = 0; i < numbers.size(); ++i) {
      if (numbers[i].empty()) {
        continue;
      }
      Segment* segment = resegmented->mutable_conversion_segment(i);
      converter::Candidate* candidate = segment->add_candidate();
      candidate->key = candidate->content_key = segment->key();
      candidate->value = candidate->content_value = numbers[i];
      if (segment->candidates_size() > 1) {
        candidate->lid = segment->candidate(0).lid;
        candidate->rid = segment->candidate(0).rid;
      }
    }
    std::optional<std::vector<int>> indices =
        MapToCandidates(*resegmented, text);
    if (!indices.has_value()) {
      return std::nullopt;
    }
    return Mapping{*std::move(indices), *std::move(resegmented)};
  };
  if (const std::optional<Resegmentation> resegmentation =
          ResegmentationFor(segments, text);
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
                  [](const Segment& segment) {
                    return KeepsTopCandidate(segment);
                  })) {
    return std::nullopt;
  }
  const std::string reading = Reading(segments);
  const std::optional<ZenzCutting> cutting = ZenzCuttingFor(reading, text);
  if (!cutting.has_value() || cutting->cuts.empty()) {
    return std::nullopt;
  }
  const size_t length = Util::CharsLen(reading);
  if (const std::optional<std::vector<uint8_t>> sizes =
          SizesAt(cutting->cuts, length);
      sizes.has_value()) {
    if (std::optional<Mapping> mapping = map_resized(
            *sizes, NumbersOfSegments(cutting->cuts, length, cutting->numbers));
        mapping.has_value()) {
      return mapping;
    }
  }
  // A segment zenz did not cut may still hold words Mozc cut apart. A number
  // stays whole.
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
  if (both == cutting->cuts) {
    return std::nullopt;
  }
  const std::optional<std::vector<uint8_t>> sizes = SizesAt(both, length);
  if (!sizes.has_value()) {
    return std::nullopt;
  }
  return map_resized(*sizes,
                     NumbersOfSegments(both, length, cutting->numbers));
}

void ZenzCorrector::Configure(bool stop_when_idle, bool low_memory) {
  if (client_ == nullptr) {
    return;
  }
  std::vector<std::string> arguments;
  if (stop_when_idle) {
    arguments.insert(arguments.end(),
                     {"--idle_exit_seconds", std::to_string(kIdleExitSeconds)});
  }
  if (low_memory) {
    arguments.push_back("--no_repack");
  }
  client_->SetArguments(std::move(arguments));
}

void ZenzCorrector::Warm() {
  if (client_ != nullptr) {
    client_->Warm();
  }
}

bool ZenzCorrector::Request(const Segments& segments,
                            absl::string_view left_context, bool not_this) {
  pending_.reset();
  if (client_ == nullptr || segments.conversion_segments_size() == 0) {
    return false;
  }
#ifdef __APPLE__
  // What is typed while secure input is on stays out of zenz (spec section 4).
  if (!ignore_secure_input_for_testing && IsSecureEventInputEnabled()) {
    return false;
  }
#endif  // __APPLE__
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
                                              const Resegmenter& resegment) {
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
  PollResult result = Judge(segments, *response, resegment);
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

ZenzCorrector::PollResult ZenzCorrector::Judge(
    const Segments& segments, const Response& response,
    const Resegmenter& resegment) const {
  // A length cap means zenz did not finish its answer.
  if (response.stop != "eog" && response.stop != "tag") {
    return {PollResult::kDone, {}};
  }
  // zenz hands the reading back unconverted when it does not know the words,
  // where Mozc's dictionary usually does. On Mozc's regression set this halves
  // the conversions zenz makes worse (20 to 10), at the cost of 2 of 48 wins.
  if (japanese::FullWidthAsciiToHalfWidthAscii(
          japanese::KatakanaToHiragana(response.conversion)) ==
      japanese::FullWidthAsciiToHalfWidthAscii(Reading(segments))) {
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
  std::optional<Mapping> mapping =
      MapWithResegmentation(segments, response.conversion, resegment);
  if (!mapping.has_value() ||
      (!mapping->segments.has_value() &&
       std::all_of(mapping->candidate_indices.begin(),
                   mapping->candidate_indices.end(),
                   [](int index) { return index == 0; }))) {
    return {PollResult::kDone, {}};
  }
  // A correction the user stopped before is not made again.
  std::string reading = Reading(segments);
  if (learning_ &&
      ZenzMemory::Stopped().Contains(reading, response.conversion)) {
    return {PollResult::kDone, {}};
  }
  return {PollResult::kApply, std::move(mapping->candidate_indices),
          std::move(reading), response.conversion,
          std::move(mapping->segments)};
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
    return new ScorerClient({
        FileUtil::JoinPath(dir, "floatype_scorer"),
        "--model",
        FileUtil::JoinPath(dir, "zenz-v3.2-small-Q5_K_M.gguf"),
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

void ZenzCorrector::IgnoreSecureInputForTesting(bool ignore) {
  ignore_secure_input_for_testing = ignore;
}

}  // namespace mozc::floatype
