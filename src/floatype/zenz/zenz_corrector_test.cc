#include "floatype/zenz/zenz_corrector.h"

#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/clock.h"
#include "base/clock_mock.h"
#include "converter/attribute.h"
#include "converter/segments.h"
#include "floatype/zenz/scorer_client.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

using ::testing::ElementsAre;

void AddSegment(Segments& segments, absl::string_view key,
                std::initializer_list<absl::string_view> values) {
  Segment* segment = segments.add_segment();
  segment->set_key(key);
  for (const absl::string_view value : values) {
    converter::Candidate* candidate = segment->add_candidate();
    candidate->key = candidate->content_key = std::string(key);
    candidate->value = candidate->content_value = std::string(value);
  }
}

Segments KyouHaIshaNiIku() {
  Segments segments;
  AddSegment(segments, "きょうは", {"今日は", "強は", "教は"});
  AddSegment(segments, "いしゃに", {"医者に", "慰謝に"});
  AddSegment(segments, "いく", {"行く", "逝く"});
  return segments;
}

TEST(MapToCandidatesTest, FindsTheCandidatesThatSpellTheText) {
  const Segments segments = KyouHaIshaNiIku();
  EXPECT_THAT(MapToCandidates(segments, "今日は医者に行く").value(),
              ElementsAre(0, 0, 0));
  EXPECT_THAT(MapToCandidates(segments, "強は医者に逝く").value(),
              ElementsAre(1, 0, 1));
}

TEST(MapToCandidatesTest, RejectsTextMozcCannotWrite) {
  const Segments segments = KyouHaIshaNiIku();
  EXPECT_EQ(MapToCandidates(segments, "今日歯医者に行く"), std::nullopt);
  EXPECT_EQ(MapToCandidates(segments, "今日は医者に"), std::nullopt);
  EXPECT_EQ(MapToCandidates(segments, "今日は医者に行くよ"), std::nullopt);
  EXPECT_EQ(MapToCandidates(Segments(), ""), std::nullopt);
}

TEST(MapToCandidatesTest, PrefersHigherRankedCandidates) {
  Segments segments;
  AddSegment(segments, "いく", {"行く", "いく", "逝く", "いく"});
  EXPECT_THAT(MapToCandidates(segments, "いく").value(), ElementsAre(1));
}

TEST(MapToCandidatesTest, BacktracksWhenAShorterCandidateLeadsNowhere) {
  Segments segments;
  AddSegment(segments, "あい", {"あ", "あい"});
  AddSegment(segments, "う", {"う"});
  EXPECT_THAT(MapToCandidates(segments, "あいう").value(), ElementsAre(1, 0));
}

TEST(MapToCandidatesTest, KeepsATopCandidateLearnedFromTheUser) {
  Segments segments = KyouHaIshaNiIku();
  segments.mutable_conversion_segment(0)->mutable_candidate(0)->attributes |=
      converter::Attribute::USER_SEGMENT_HISTORY_REWRITER;
  EXPECT_EQ(MapToCandidates(segments, "強は医者に行く"), std::nullopt);
  EXPECT_THAT(MapToCandidates(segments, "今日は医者に逝く").value(),
              ElementsAre(0, 0, 1));
}

TEST(MapToCandidatesTest, IgnoresCandidatesForAnotherPartOfTheReading) {
  Segments segments = KyouHaIshaNiIku();
  converter::Candidate* partial =
      segments.mutable_conversion_segment(0)->add_candidate();
  partial->key = "きょう";
  partial->value = "今日";
  EXPECT_EQ(MapToCandidates(segments, "今日医者に行く"), std::nullopt);
}

// The fake scorer answers with the reading itself, in katakana.
Segments KyouHa() {
  Segments segments;
  AddSegment(segments, "きょう", {"今日", "キョウ"});
  AddSegment(segments, "は", {"は", "ハ"});
  return segments;
}

class ZenzCorrectorTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override { Clock::SetClockForUnitTest(&clock_); }
  void TearDown() override { Clock::SetClockForUnitTest(nullptr); }

  // Polls until the result is no longer kWait, for up to two seconds.
  static ZenzCorrector::PollResult PollUntilDone(ZenzCorrector& corrector,
                                                 const Segments& segments) {
    for (int i = 0; i < 2000; ++i) {
      ZenzCorrector::PollResult result = corrector.Poll(segments);
      if (result.kind != ZenzCorrector::PollResult::kWait) return result;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return {ZenzCorrector::PollResult::kWait, {}};
  }

  ClockMock clock_{absl::FromUnixSeconds(1000)};
  ScorerClient client_{
      {testing::GetSourceFileOrDie({"floatype", "zenz", "fake_scorer"})}};
};

TEST_F(ZenzCorrectorTest, AppliesAnAnswerThatDiffersFromMozc) {
  ZenzCorrector corrector(&client_);
  const Segments segments = KyouHa();
  ASSERT_TRUE(corrector.Request(segments, "熱が"));
  const ZenzCorrector::PollResult result = PollUntilDone(corrector, segments);
  EXPECT_EQ(result.kind, ZenzCorrector::PollResult::kApply);
  EXPECT_THAT(result.candidate_indices, ElementsAre(1, 1));
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kDone)
      << "one correction per conversion";
}

TEST_F(ZenzCorrectorTest, KeepsMozcWhenZenzAgrees) {
  ZenzCorrector corrector(&client_);
  Segments segments;
  AddSegment(segments, "あめ", {"アメ", "雨"});
  ASSERT_TRUE(corrector.Request(segments, ""));
  EXPECT_EQ(PollUntilDone(corrector, segments).kind,
            ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, DropsAnUnfinishedAnswer) {
  ZenzCorrector corrector(&client_);
  Segments segments;
  AddSegment(segments, "かっぷ", {"カップ"});
  ASSERT_TRUE(corrector.Request(segments, ""));  // The fake answers "cap".
  EXPECT_EQ(PollUntilDone(corrector, segments).kind,
            ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, GivesUpAfterTheTimeout) {
  ZenzCorrector corrector(&client_);
  Segments segments;
  AddSegment(segments, "すろう", {"スロウ"});  // The fake waits 200ms.
  ASSERT_TRUE(corrector.Request(segments, ""));
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kWait);
  clock_.Advance(ZenzCorrector::kTimeout);
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, DropsTheAnswerAfterCancelOrAChangedConversion) {
  ZenzCorrector corrector(&client_);
  const Segments segments = KyouHa();
  ASSERT_TRUE(corrector.Request(segments, ""));
  corrector.Cancel();
  EXPECT_EQ(PollUntilDone(corrector, segments).kind,
            ZenzCorrector::PollResult::kDone);

  ASSERT_TRUE(corrector.Request(segments, ""));
  Segments changed = KyouHa();
  changed.mutable_conversion_segment(1)->move_candidate(1, 0);
  EXPECT_EQ(PollUntilDone(corrector, changed).kind,
            ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, DoesNothingWithoutAScorer) {
  ZenzCorrector corrector(nullptr);
  const Segments segments = KyouHa();
  EXPECT_FALSE(corrector.Request(segments, ""));
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kDone);
}

}  // namespace
}  // namespace mozc::floatype
