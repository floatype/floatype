#include "rewriter/pinned_word_rewriter.h"

#include <cstddef>
#include <string>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "floatype/zenz/zenz_memory.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"

namespace mozc {
namespace {

// A pin, as the user asked for an icon over the word 固定.
constexpr absl::string_view kMark = "📌";

}  // namespace

int PinnedWordRewriter::capability(const ConversionRequest& request) const {
  return RewriterInterface::ALL;
}

bool PinnedWordRewriter::Rewrite(const ConversionRequest& request,
                                 Segments* segments) const {
  const floatype::ZenzMemory& pinned = floatype::ZenzMemory::Pinned();
  const floatype::ZenzMemory& hidden = floatype::ZenzMemory::Hidden();
  const bool predicting =
      request.request_type() != ConversionRequest::CONVERSION;
  const bool pins = !predicting || request.config().pinned_words_in_predictions();
  bool modified = false;
  for (size_t i = 0; i < segments->conversion_segments_size(); ++i) {
    Segment* segment = segments->mutable_conversion_segment(i);
    // A segment the user has fixed keeps what was chosen there.
    if (segment->segment_type() == Segment::FIXED_VALUE) {
      continue;
    }
    // From the end, so that the indices ahead stay; the last one stays, as a
    // segment needs a candidate.
    for (size_t j = segment->candidates_size(); j-- > 0;) {
      const converter::Candidate& candidate = segment->candidate(j);
      // A word hidden from a conversion, or a row from a prediction list.
      if (segment->candidates_size() > 1 &&
          (hidden.Contains(candidate.content_key, candidate.content_value) ||
           hidden.Contains(candidate.key, candidate.value))) {
        segment->erase_candidate(j);
        modified = true;
      }
    }
    if (!pins) {
      continue;
    }
    for (size_t j = 0; j < segment->candidates_size(); ++j) {
      const converter::Candidate& candidate = segment->candidate(j);
      // Latest() is empty for a reading without a pin.
      if (candidate.content_key.empty() || candidate.content_value.empty() ||
          pinned.Latest(candidate.content_key) != candidate.content_value) {
        continue;
      }
      // A prediction moves only for the word itself, typed in full: not
      // 齋藤さん, nor 齋藤 while さい is all that is typed.
      if (predicting && (candidate.value != candidate.content_value ||
                         candidate.content_key != segment->key())) {
        continue;
      }
      if (j != 0) {
        segment->move_candidate(j, 0);
      }
      Mark(*segment->mutable_candidate(0), true);
      modified = true;
      break;
    }
  }
  return modified;
}

void PinnedWordRewriter::Mark(converter::Candidate& candidate, bool pinned) {
  std::string& description = candidate.description;
  const bool marked = absl::StartsWith(description, kMark);
  if (pinned && !marked) {
    description = description.empty() ? std::string(kMark)
                                      : absl::StrCat(kMark, " ", description);
  } else if (!pinned && marked) {
    description.erase(0, kMark.size());
    if (absl::StartsWith(description, " ")) {
      description.erase(0, 1);
    }
  }
}

}  // namespace mozc
