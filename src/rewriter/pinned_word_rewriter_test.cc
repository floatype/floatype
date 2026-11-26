#include "rewriter/pinned_word_rewriter.h"

#include <string>

#include "converter/candidate.h"
#include "converter/segments.h"
#include "floatype/zenz/zenz_memory.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"
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
  EXPECT_EQ(segment.candidate(0).description, "📌");
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

TEST_F(PinnedWordRewriterTest, KeepsEmojiAndEmoticonsOffTheFirstPlace) {
  Segments segments;
  AddSegment(
      segments, "にこにこ",
      {{"にこにこ", "😊"}, {"にこにこ", "ニコニコ"}, {"にこにこ", "にこにこ"}});
  segments.mutable_conversion_segment(0)->mutable_candidate(0)->description =
      "絵文字 笑顔";
  AddSegment(
      segments, "かおもじ",
      {{"かおもじ", "(^^)"}, {"かおもじ", "(^o^)"}, {"かおもじ", "顔文字"}});
  for (int i : {0, 1}) {
    segments.mutable_conversion_segment(1)->mutable_candidate(i)->description =
        "顔文字";
  }
  ASSERT_TRUE(Rewrite(segments));
  const Segment& emoji = segments.conversion_segment(0);
  EXPECT_EQ(emoji.candidate(0).value, "ニコニコ");
  EXPECT_EQ(emoji.candidate(1).value, "😊") << "it stays in the list";
  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "顔文字")
      << "past every emoticon";

  // A prediction keeps its order, as in Mozkey.
  Segments predicted;
  AddSegment(predicted, "にこにこ",
             {{"にこにこ", "😊"}, {"にこにこ", "ニコニコ"}});
  predicted.mutable_conversion_segment(0)->mutable_candidate(0)->description =
      "絵文字";
  EXPECT_FALSE(PinnedWordRewriter().Rewrite(
      ConversionRequestBuilder()
          .SetRequestType(ConversionRequest::PREDICTION)
          .Build(),
      &predicted));
  EXPECT_EQ(predicted.conversion_segment(0).candidate(0).value, "😊");
}

TEST_F(PinnedWordRewriterTest, APinnedEmojiStaysFirst) {
  floatype::ZenzMemory::Pinned().Add("にこにこ", "😊");
  Segments segments;
  AddSegment(segments, "にこにこ",
             {{"にこにこ", "😊"}, {"にこにこ", "ニコニコ"}});
  segments.mutable_conversion_segment(0)->mutable_candidate(0)->description =
      "絵文字";
  ASSERT_TRUE(Rewrite(segments));
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "😊");
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

TEST_F(PinnedWordRewriterTest, MovesAPredictionOnlyWithTheSetting) {
  floatype::ZenzMemory::Pinned().Add("さいとう", "齋藤");
  config::Config config;
  PinnedWordRewriter rewriter;
  const auto request = [&config](ConversionRequest::RequestType type) {
    return ConversionRequestBuilder()
        .SetConfig(config)
        .SetRequestType(type)
        .Build();
  };
  // Without the setting, a prediction keeps its order.
  Segments kept;
  AddSegment(kept, "さいとう", {{"さいとう", "斉藤"}, {"さいとう", "齋藤"}});
  EXPECT_FALSE(rewriter.Rewrite(request(ConversionRequest::PREDICTION), &kept));
  EXPECT_EQ(kept.conversion_segment(0).candidate(0).value, "斉藤");
  config.set_pinned_words_in_predictions(true);

  // The word itself, with its whole reading typed, comes first.
  Segments segments;
  AddSegment(segments, "さいとう",
             {{"さいとう", "斉藤"}, {"さいとう", "齋藤"}});
  ASSERT_TRUE(rewriter.Rewrite(request(ConversionRequest::PREDICTION),
                               &segments));
  EXPECT_EQ(segments.conversion_segment(0).candidate(0).value, "齋藤");

  // Not while only さい is typed, nor 齋藤さん for 齋藤.
  Segments partial;
  AddSegment(partial, "さい", {{"さい", "再"}, {"さいとう", "齋藤"}});
  EXPECT_FALSE(
      rewriter.Rewrite(request(ConversionRequest::PREDICTION), &partial));
  Segments longer;
  AddSegment(longer, "さいとうさん",
             {{"さいとうさん", "斉藤さん"}, {"さいとう", "齋藤"}});
  longer.mutable_conversion_segment(0)->mutable_candidate(1)->value =
      "齋藤さん";
  EXPECT_FALSE(
      rewriter.Rewrite(request(ConversionRequest::PREDICTION), &longer));
}

// #38: a hidden word goes from conversions and predictions, with its
// particles (its content word), but a segment keeps one candidate.
TEST_F(PinnedWordRewriterTest, TakesOutHiddenWords) {
  floatype::ZenzMemory::Hidden().Add("きかい", "機械");
  Segments segments;
  AddSegment(segments, "きかいが",
             {{"きかい", "機会"}, {"きかい", "機械"}, {"きかい", "器械"}});
  segments.mutable_conversion_segment(0)->mutable_candidate(1)->value = "機械が";
  AddSegment(segments, "きかい", {{"きかい", "機械"}});
  ASSERT_TRUE(Rewrite(segments));
  const Segment& segment = segments.conversion_segment(0);
  ASSERT_EQ(segment.candidates_size(), 2);
  EXPECT_EQ(segment.candidate(0).value, "機会");
  EXPECT_EQ(segment.candidate(1).value, "器械");
  EXPECT_EQ(segments.conversion_segment(1).candidates_size(), 1)
      << "the only candidate stays";

  // Predictions too, without pinned_words_in_predictions.
  Segments predicted;
  AddSegment(predicted, "きか", {{"きかい", "機械"}, {"きかい", "機会"}});
  ASSERT_TRUE(PinnedWordRewriter().Rewrite(
      ConversionRequestBuilder()
          .SetRequestType(ConversionRequest::PREDICTION)
          .Build(),
      &predicted));
  ASSERT_EQ(predicted.conversion_segment(0).candidates_size(), 1);
  EXPECT_EQ(predicted.conversion_segment(0).candidate(0).value, "機会");
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
  EXPECT_EQ(candidate.description, "📌 [全]");
  PinnedWordRewriter::Mark(candidate, true);
  EXPECT_EQ(candidate.description, "📌 [全]");
  PinnedWordRewriter::Mark(candidate, false);
  EXPECT_EQ(candidate.description, "[全]");
}

}  // namespace
}  // namespace mozc
