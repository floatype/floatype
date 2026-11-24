#include "rewriter/pinned_word_rewriter.h"

#include <cstddef>
#include <string>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "floatype/zenz/zenz_memory.h"
#include "request/conversion_request.h"

namespace mozc {
namespace {

constexpr absl::string_view kMark = "固定";

}  // namespace

bool PinnedWordRewriter::Rewrite(const ConversionRequest& request,
                                 Segments* segments) const {
  const floatype::ZenzMemory& pinned = floatype::ZenzMemory::Pinned();
  bool modified = false;
  for (size_t i = 0; i < segments->conversion_segments_size(); ++i) {
    Segment* segment = segments->mutable_conversion_segment(i);
    for (size_t j = 0; j < segment->candidates_size(); ++j) {
      const converter::Candidate& candidate = segment->candidate(j);
      // Latest() is empty for a reading without a pin.
      if (candidate.content_key.empty() || candidate.content_value.empty() ||
          pinned.Latest(candidate.content_key) != candidate.content_value) {
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
