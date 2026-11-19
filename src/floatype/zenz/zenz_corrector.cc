#include "floatype/zenz/zenz_corrector.h"

#ifdef __APPLE__
#include <Carbon/Carbon.h>  // IsSecureEventInputEnabled
#endif                      // __APPLE__

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/clock.h"
#include "base/file_util.h"
#include "base/number_util.h"
#include "base/strings/japanese.h"
#include "base/system_util.h"
#include "base/util.h"
#include "converter/attribute.h"
#include "converter/segments.h"
#include "floatype/scorer/protocol.h"
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

std::optional<Mapping> MapWithResegmentation(const Segments& segments,
                                             absl::string_view text,
                                             const Resegmenter& resegment) {
  if (std::optional<std::vector<int>> indices = MapToCandidates(segments, text);
      indices.has_value()) {
    return Mapping{*std::move(indices), std::nullopt};
  }
  const std::optional<Resegmentation> resegmentation =
      ResegmentationFor(segments, text);
  if (!resegment || !resegmentation.has_value()) {
    return std::nullopt;
  }
  std::optional<Segments> resegmented = resegment(resegmentation->sizes);
  if (!resegmented.has_value() || resegmented->conversion_segments_size() !=
                                      resegmentation->numbers.size()) {
    return std::nullopt;
  }
  // The digits of a number Mozc wrote in kanji numerals, one per segment,
  // are a candidate of the segment that now holds all of it.
  for (size_t i = 0; i < resegmentation->numbers.size(); ++i) {
    if (resegmentation->numbers[i].empty()) {
      continue;
    }
    Segment* segment = resegmented->mutable_conversion_segment(i);
    converter::Candidate* candidate = segment->add_candidate();
    candidate->key = candidate->content_key = segment->key();
    candidate->value = candidate->content_value = resegmentation->numbers[i];
    if (segment->candidates_size() > 1) {
      candidate->lid = segment->candidate(0).lid;
      candidate->rid = segment->candidate(0).rid;
    }
  }
  std::optional<std::vector<int>> indices = MapToCandidates(*resegmented, text);
  if (!indices.has_value()) {
    return std::nullopt;
  }
  return Mapping{*std::move(indices), *std::move(resegmented)};
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
