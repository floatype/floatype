#include "floatype/zenz/zenz_corrector.h"

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
// `dead_ends` remembers (segment, position) pairs that cannot be completed.
bool Search(const Segments& segments, absl::string_view text, size_t segment,
            size_t pos, std::vector<int>& chosen,
            absl::flat_hash_set<std::pair<size_t, size_t>>& dead_ends) {
  if (segment == segments.conversion_segments_size()) {
    return pos == text.size();
  }
  if (dead_ends.contains({segment, pos})) {
    return false;
  }
  const Segment& seg = segments.conversion_segment(segment);
  const int limit =
      KeepsTopCandidate(seg) ? 1 : static_cast<int>(seg.candidates_size());
  for (int i = 0; i < limit; ++i) {
    const converter::Candidate& candidate = seg.candidate(i);
    // A candidate with another key covers a different part of the reading.
    if (candidate.key != seg.key() || candidate.value.empty() ||
        !absl::StartsWith(text.substr(pos), candidate.value)) {
      continue;
    }
    chosen[segment] = i;
    if (Search(segments, text, segment + 1, pos + candidate.value.size(),
               chosen, dead_ends)) {
      return true;
    }
  }
  dead_ends.insert({segment, pos});
  return false;
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

std::optional<std::vector<int>> MapToCandidates(const Segments& segments,
                                                absl::string_view text) {
  std::vector<int> chosen(segments.conversion_segments_size(), 0);
  absl::flat_hash_set<std::pair<size_t, size_t>> dead_ends;
  if (chosen.empty() || !Search(segments, text, 0, 0, chosen, dead_ends)) {
    return std::nullopt;
  }
  return chosen;
}

bool ZenzCorrector::Request(const Segments& segments,
                            absl::string_view left_context) {
  pending_.reset();
  if (client_ == nullptr || segments.conversion_segments_size() == 0) {
    return false;
  }
  std::string reading;
  for (const Segment& segment : segments.conversion_segments()) {
    reading += segment.key();
  }
  static uint64_t last_id = 0;
  const uint64_t id = ++last_id;
  if (!client_->Send(
          ::floatype::scorer::Request{id, std::string(left_context),
                                      japanese::HiraganaToKatakana(reading)})) {
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
    if (Clock::GetAbslTime() - pending_->sent >= kTimeout) {
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
  std::optional<std::vector<int>> indices =
      MapToCandidates(segments, response->conversion);
  if (!indices.has_value() ||
      std::all_of(indices->begin(), indices->end(),
                  [](int index) { return index == 0; })) {
    return {PollResult::kDone, {}};
  }
  return {PollResult::kApply, *std::move(indices)};
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

void ZenzCorrector::EnableDefaultClient() { default_client_enabled = true; }

void ZenzCorrector::SetDefaultClientForTesting(ScorerClient* client) {
  default_client_for_testing = client;
}

}  // namespace mozc::floatype
