#include "rewriter/pinned_word_rewriter.h"

#include <string>

#include "converter/candidate.h"
#include "converter/segments.h"
#include "floatype/zenz/zenz_memory.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

class PinnedWordRewriterTest : public testing::TestWithTempUserProfile {
 protected:
  // A segment for `key` with candidates of (content key, value) in order.
  static void AddSegment(
      Segments& segments, const std::string& key,
      std::initializer_list<std::pair<std::string, std::string>> words) {
    Segment* segment = segments.add_segment();
    segment->set_key(key);
    for (const auto& [content_key, value] : words) {
      converter::Candidate* candidate = segment->add_candidate();
      candidate->key = key;
      candidate->content_key = content_key;
      candidate->value = value;
      candidate->content_value = value;
    }
  }

  bool Rewrite(Segments& segments) {
    return PinnedWordRewriter().Rewrite(ConversionRequest(), &segments);
  }
};

TEST_F(PinnedWordRewriterTest, PutsThePinnedWordFirstInAnySegment) {
  floatype::ZenzMemory::Pinned().Add("よい", "よい");
  Segments segments;
  AddSegment(segments, "すると", {{"すると", "すると"}});
  AddSegment(segments, "よい", {{"よい", "良い"}, {"よい", "よい"}, {"よい", "善い"}});
  ASSERT_TRUE(Rewrite(segments));
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "すると");
  const Segment& segment = segments.conversion_segment(1);
  EXPECT_EQ(segment.candidate(0).value, "よい");
  EXPECT_EQ(segment.candidate(0).description, "固定");
  EXPECT_EQ(segment.candidate(1).value, "良い");
  EXPECT_TRUE(segment.candidate(1).description.empty());
}

TEST_F(PinnedWordRewriterTest, TheLatestPinOfAReadingCounts) {
  floatype::ZenzMemory::Pinned().Add("さいとう", "斉藤");
  floatype::ZenzMemory::Pinned().Add("さいとう", "齋藤");
  Segments segments;
  AddSegment(segments, "さいとう",
             {{"さいとう", "斉藤"}, {"さいとう", "斎藤"}, {"さいとう", "齋藤"}});
  ASSERT_TRUE(Rewrite(segments));
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "齋藤");
}

TEST_F(PinnedWordRewriterTest, LeavesSegmentsWithoutAPinnedWord) {
  floatype::ZenzMemory::Pinned().Add("さいとう", "齋藤");
  Segments segments;
  AddSegment(segments, "よい", {{"よい", "良い"}, {"よい", "よい"}});
  EXPECT_FALSE(Rewrite(segments));
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "良い");
}

TEST_F(PinnedWordRewriterTest, LeavesASegmentTheUserFixed) {
  floatype::ZenzMemory::Pinned().Add("よい", "よい");
  Segments segments;
  AddSegment(segments, "よい", {{"よい", "良い"}, {"よい", "よい"}});
  segments.mutable_conversion_segment(0)->set_segment_type(
      Segment::FIXED_VALUE);
  EXPECT_FALSE(Rewrite(segments));
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "良い");
}

TEST_F(PinnedWordRewriterTest, MovesNothingWithoutPins) {
  // A candidate with no content value (a date, say) is not taken for a pin.
  Segments segments;
  AddSegment(segments, "きょう", {{"きょう", "今日"}, {"きょう", ""}});
  EXPECT_FALSE(Rewrite(segments));
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "今日");
}

TEST(PinnedWordRewriterMarkTest, PutsOnAndTakesOffTheMark) {
  converter::Candidate candidate;
  candidate.description = "[全]";
  PinnedWordRewriter::Mark(candidate, true);
  EXPECT_EQ(candidate.description, "固定 [全]");
  PinnedWordRewriter::Mark(candidate, true);
  EXPECT_EQ(candidate.description, "固定 [全]");
  PinnedWordRewriter::Mark(candidate, false);
  EXPECT_EQ(candidate.description, "[全]");
}

}  // namespace
}  // namespace mozc
