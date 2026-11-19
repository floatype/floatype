#include "floatype/zenz/zenz_corrector.h"

#ifdef __APPLE__
#include <Carbon/Carbon.h>  // IsSecureEventInputEnabled
#endif                      // __APPLE__

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/clock.h"
#include "base/file_util.h"
#include "base/strings/japanese.h"
#include "base/system_util.h"
#include "base/util.h"
#include "converter/attribute.h"
#include "converter/segments.h"
#include "floatype/scorer/protocol.h"
#include "floatype/zenz/scorer_client.h"

namespace mozc::floatype {
namespace {

using ::floatype::scorer::Request;
using ::floatype::scorer::Response;

bool default_client_enabled = false;
ScorerClient* default_client_for_testing = nullptr;

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

}  // namespace

namespace {

std::optional<std::vector<int>> Map(const Segments& segments,
                                    absl::string_view text) {
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
  std::vector<int> chosen(values.size(), 0);
  absl::flat_hash_set<std::pair<size_t, size_t>> dead_ends;
  if (values.empty() ||
      !Search(values, japanese::FullWidthAsciiToHalfWidthAscii(text), 0, 0,
              chosen, dead_ends)) {
    return std::nullopt;
  }
  return chosen;
}

}  // namespace

std::string ConversionReading(const Segments& segments) {
  return Reading(segments);
}

std::optional<std::vector<int>> MapToCandidates(const Segments& segments,
                                                absl::string_view text) {
  return Map(segments, text);
}

bool ZenzCorrector::Request(const Segments& segments,
                            absl::string_view left_context) {
  pending_.reset();
  if (client_ == nullptr || segments.conversion_segments_size() == 0) {
    return false;
  }
#ifdef __APPLE__
  // What is typed while secure input is on stays out of zenz (spec section 4).
  if (IsSecureEventInputEnabled()) {
    return false;
  }
#endif  // __APPLE__
  const std::string reading = Reading(segments);
  // Longer readings are slow to convert and may not fit the scorer's context.
  if (Util::CharsLen(reading) > kMaxReadingChars) {
    return false;
  }
  static uint64_t last_id = 0;
  const uint64_t id = ++last_id;
  // Mozc's conversion as the draft: the scorer checks it in one pass, which
  // takes AJIMEE-Bench's median from 24.6ms to 20.4ms with the same answers
  // but for 2 of 200 (batched arithmetic), both wrong either way.
  std::string draft;
  for (const Segment& segment : segments.conversion_segments()) {
    if (segment.candidates_size() > 0) {
      draft += segment.candidate(0).value;
    }
  }
  if (!client_->Send(::floatype::scorer::Request{
          id, std::string(left_context), japanese::HiraganaToKatakana(reading),
          std::move(draft)})) {
    return false;
  }
  pending_ = Pending{id, Clock::GetAbslTime(), Fingerprint(segments)};
  return true;
}

ZenzCorrector::PollResult ZenzCorrector::Poll(const Segments& segments) {
  if (!pending_.has_value() || Fingerprint(segments) != pending_->fingerprint) {
    Cancel();
    return {PollResult::kDone, {}};
  }
  const std::optional<Response> response = client_->Poll();
  if (!response.has_value() || response->id != pending_->id) {
    if (Clock::GetAbslTime() - pending_->sent >= timeout_) {
      Cancel();
      return {PollResult::kDone, {}};
    }
    return {PollResult::kWait, {}};
  }
  Cancel();
  // A length cap means zenz did not finish its answer.
  if (response->stop != "eog" && response->stop != "tag") {
    return {PollResult::kDone, {}};
  }
  // zenz hands the reading back unconverted when it does not know the words,
  // where Mozc's dictionary usually does. On Mozc's regression set this halves
  // the conversions zenz makes worse (20 to 10), at the cost of 2 of 48 wins.
  if (japanese::FullWidthAsciiToHalfWidthAscii(
          japanese::KatakanaToHiragana(response->conversion)) ==
      japanese::FullWidthAsciiToHalfWidthAscii(Reading(segments))) {
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
  std::optional<std::vector<int>> indices =
      MapToCandidates(segments, response->conversion);
  if (!indices.has_value() ||
      std::all_of(indices->begin(), indices->end(),
                  [](int index) { return index == 0; })) {
    return {PollResult::kDone, {}};
  }
  return {PollResult::kApply, *std::move(indices), Reading(segments),
          response->conversion};
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

}  // namespace mozc::floatype
