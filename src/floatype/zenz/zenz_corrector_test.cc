#include "floatype/zenz/zenz_corrector.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/clock.h"
#include "base/clock_mock.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "config/character_form_manager.h"
#include "converter/attribute.h"
#include "converter/segments.h"
#include "floatype/zenz/scorer_client.h"
#include "protocol/config.pb.h"
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

TEST(MapToCandidatesTest, KeepsMozcsWidthOfSymbols) {
  Segments segments;
  AddSegment(segments, "(", {"（", "("});
  AddSegment(segments, "ごじつ", {"後日", "ご実"});
  AddSegment(segments, ")", {"）", ")"});
  // zenz writes half-width parentheses; Mozc's full-width ones stay.
  EXPECT_THAT(MapToCandidates(segments, "(ご実)").value(),
              ElementsAre(0, 1, 0));
  // And the other way round, when Mozc ranks half width first.
  Segments half;
  AddSegment(half, "1", {"1", "１"});
  EXPECT_THAT(MapToCandidates(half, "１").value(), ElementsAre(0));
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

TEST(ResegmentationForTest, JoinsKanjiNumeralsThatReadAsTheDigits) {
  Segments segments;
  AddSegment(segments, "せん", {"線", "千"});
  AddSegment(segments, "きゅうひゃく", {"九百"});
  AddSegment(segments, "なな", {"七"});
  AddSegment(segments, "じゅう", {"重", "十"});
  AddSegment(segments, "に", {"二"});
  AddSegment(segments, "ねんの", {"年の"});
  const std::optional<Resegmentation> resegmentation =
      ResegmentationFor(segments, "1972年の");
  ASSERT_TRUE(resegmentation.has_value());
  EXPECT_THAT(resegmentation->sizes, ElementsAre(14, 3));
  EXPECT_THAT(resegmentation->numbers, ElementsAre("1972", ""));
  // Other digits are not the number's.
  EXPECT_THAT(ResegmentationFor(segments, "1973年の")->numbers,
              ElementsAre("", ""));
}

TEST(ResegmentationForTest, WritesTheDigitsInMozcsWidth) {
  Segments segments;
  AddSegment(segments, "ひゃく", {"百", "１００", "100"});
  AddSegment(segments, "なな", {"ナナ", "七"});
  EXPECT_THAT(ResegmentationFor(segments, "107")->numbers,
              ElementsAre("１０７"));
}

TEST(ResegmentationForTest, JoinsARunOfSegmentsThatCannotSpellTheirPart) {
  Segments segments;
  AddSegment(segments, "かんぱん", {"甲板"});
  AddSegment(segments, "たい", {"タイ", "対"});
  AddSegment(segments, "くうき", {"空気"});
  AddSegment(segments, "じゅうぐん", {"従軍"});
  const std::optional<Resegmentation> resegmentation =
      ResegmentationFor(segments, "甲板対空機銃群");
  ASSERT_TRUE(resegmentation.has_value());
  EXPECT_THAT(resegmentation->sizes, ElementsAre(4, 2, 8));
  EXPECT_THAT(resegmentation->numbers, ElementsAre("", "", ""));
}

TEST(ResegmentationForTest, ARunOfOneSegmentTakesInANeighbor) {
  Segments segments;
  AddSegment(segments, "かみを", {"髪を"});
  AddSegment(segments, "き", {"気", "木"});
  AddSegment(segments, "る", {"る"});
  EXPECT_THAT(ResegmentationFor(segments, "髪を切る")->sizes,
              ElementsAre(3, 2));
  // At the end, the one before.
  Segments last;
  AddSegment(last, "かみを", {"髪を"});
  AddSegment(last, "き", {"気", "木"});
  EXPECT_THAT(ResegmentationFor(last, "髪を切")->sizes, ElementsAre(4));
}

TEST(ResegmentationForTest, ReturnsNulloptWhenNothingWouldChange) {
  const Segments segments = KyouHaIshaNiIku();
  EXPECT_EQ(ResegmentationFor(segments, "今日は医者に行く"), std::nullopt);
  // A run has no more characters than its reading.
  EXPECT_EQ(ResegmentationFor(segments, "今日は医者に行きたいです"),
            std::nullopt);
  EXPECT_EQ(ResegmentationFor(Segments(), "今日"), std::nullopt);
}

TEST(ResegmentationForTest, KeepsATopCandidateLearnedFromTheUser) {
  Segments segments = KyouHaIshaNiIku();
  segments.mutable_conversion_segment(2)->mutable_candidate(0)->attributes |=
      converter::Attribute::USER_SEGMENT_HISTORY_REWRITER;
  EXPECT_EQ(ResegmentationFor(segments, "今日は医者に生く"), std::nullopt);
}

// The fake scorer's answers file (see ZenzCorrectorTest) turns キョウハ into
// 京歯, which these candidates can write.
TEST(ZenzSegmentSizesTest, CutsWhereTheTextLeavesHiragana) {
  EXPECT_THAT(*ZenzSegmentSizes("がかんせいどたかい", "が完成度高い"),
              ElementsAre(1, 8));
  EXPECT_THAT(*ZenzSegmentSizes("つくえにものをおく", "机に物を置く"),
              ElementsAre(4, 3, 2));
  // Katakana starts a word; ー belongs to what it follows.
  EXPECT_THAT(*ZenzSegmentSizes("それはかれーです", "それはカレーです"),
              ElementsAre(3, 5));
}

TEST(ZenzSegmentSizesTest, CutsAroundANumberSaidInKana) {
  EXPECT_THAT(*ZenzSegmentSizes("せつりつごじゅっしゅうねんをむかえる",
                                "設立50周年を迎える"),
              ElementsAre(4, 4, 6, 4));
  EXPECT_THAT(*ZenzSegmentSizes("せんきゅうひゃくきゅうじゅういちねんに",
                                "１９９１年に"),
              ElementsAre(16, 3));
}

TEST(ZenzSegmentSizesTest, DigitsTheUserTypedStayWhereTheyAre) {
  EXPECT_THAT(*ZenzSegmentSizes("2026ねんのよてい", "2026年の予定"),
              ElementsAre(7, 3));
}

TEST(ZenzSegmentSizesTest, MatchesNothingWhenTheTextReadsOtherwise) {
  // Digits zenz made up.
  EXPECT_FALSE(
      ZenzSegmentSizes("いちにちさいちょうはちじかん", "1日最長0時間")
          .has_value());
  // Kana the reading does not have.
  EXPECT_FALSE(
      ZenzSegmentSizes("きょうはいしゃにいく", "今日が医者に行く").has_value());
  // Nowhere to cut.
  EXPECT_FALSE(ZenzSegmentSizes("あめ", "雨").has_value());
}

// Answers a request for segments of `sizes` with `segments`, and any other
// with nullopt; `asked` collects every request.
Resegmenter ResegmenterFor(std::vector<uint8_t> sizes, Segments segments,
                           std::vector<std::vector<uint8_t>>* asked) {
  return [sizes = std::move(sizes), segments = std::move(segments),
          asked](absl::Span<const uint8_t> request) -> std::optional<Segments> {
    asked->emplace_back(request.begin(), request.end());
    if (!std::equal(request.begin(), request.end(), sizes.begin(),
                    sizes.end())) {
      return std::nullopt;
    }
    return segments;
  };
}

TEST(MapWithResegmentationTest, CutsAsZenzCutsWhereMozcsSegmentsCannotWrite) {
  Segments segments;
  AddSegment(segments, "がかんせいど", {"掛かんせいど", "がかんせいど"});
  AddSegment(segments, "たかい", {"高い", "他界"});
  Segments cut;
  AddSegment(cut, "が", {"が", "我"});
  AddSegment(cut, "かんせいどたかい", {"かんせいどたかい", "完成度高い"});
  std::vector<std::vector<uint8_t>> asked;
  const std::optional<Mapping> mapping = MapWithResegmentation(
      segments, "が完成度高い", ResegmenterFor({1, 8}, cut, &asked));
  ASSERT_TRUE(mapping.has_value());
  EXPECT_THAT(mapping->candidate_indices, ElementsAre(0, 1));
  ASSERT_TRUE(mapping->segments.has_value());
  EXPECT_EQ(mapping->segments->conversion_segment(1).key(), "かんせいどたかい");
}

TEST(MapWithResegmentationTest, CutsWhereMozcCutTooWhenZenzsCutsDoNotWrite) {
  Segments segments;
  AddSegment(segments, "がかんせいど", {"掛かんせいど", "がかんせいど"});
  AddSegment(segments, "たかい", {"高い", "他界"});
  Segments cut;
  AddSegment(cut, "が", {"が", "我"});
  AddSegment(cut, "かんせいど", {"肝制度", "完成度"});
  AddSegment(cut, "たかい", {"高い", "他界"});
  std::vector<std::vector<uint8_t>> asked;
  const std::optional<Mapping> mapping = MapWithResegmentation(
      segments, "が完成度高い", ResegmenterFor({1, 5, 3}, cut, &asked));
  ASSERT_TRUE(mapping.has_value());
  EXPECT_THAT(mapping->candidate_indices, ElementsAre(0, 1, 0));
  EXPECT_THAT(asked, ::testing::Contains(ElementsAre(1, 8)))
      << "zenz's own cuts come first";
}

TEST(MapWithResegmentationTest, AddsTheDigitsOfANumberSaidInKana) {
  Segments segments;
  AddSegment(segments, "せつりつご", {"設立後"});
  AddSegment(segments, "じゅっ", {"十"});
  AddSegment(segments, "しゅうねんを", {"周年を"});
  AddSegment(segments, "むかえる", {"迎える"});
  Segments cut;
  AddSegment(cut, "せつりつ", {"設立"});
  AddSegment(cut, "ごじゅっ", {"後十", "五十"});
  AddSegment(cut, "しゅうねんを", {"周年を"});
  AddSegment(cut, "むかえる", {"迎える"});
  config::CharacterFormManager* forms =
      config::CharacterFormManager::GetCharacterFormManager();
  for (const auto& [form, digits] :
       {std::pair{config::Config::HALF_WIDTH, "50"},
        std::pair{config::Config::FULL_WIDTH, "５０"}}) {
    forms->SetDefaultRule();
    forms->AddConversionRule("0", form);
    std::vector<std::vector<uint8_t>> asked;
    const std::optional<Mapping> mapping = MapWithResegmentation(
        segments, "設立50周年を迎える",
        ResegmenterFor({4, 4, 6, 4}, cut, &asked));
    ASSERT_TRUE(mapping.has_value());
    EXPECT_THAT(mapping->candidate_indices, ElementsAre(0, 2, 0, 0));
    EXPECT_EQ(mapping->segments->conversion_segment(1).candidate(2).value,
              digits)
        << "in the width the settings give digits";
  }
  forms->SetDefaultRule();
}

TEST(MapWithResegmentationTest, DoesNotCutWhatTheUsersHistoryDecided) {
  Segments segments;
  AddSegment(segments, "がかんせいど", {"掛かんせいど", "がかんせいど"});
  AddSegment(segments, "たかい", {"高い", "他界"});
  segments.mutable_conversion_segment(0)->mutable_candidate(0)->attributes |=
      converter::Attribute::USER_SEGMENT_HISTORY_REWRITER;
  Segments cut;
  AddSegment(cut, "が", {"が"});
  AddSegment(cut, "かんせいどたかい", {"完成度高い"});
  std::vector<std::vector<uint8_t>> asked;
  EXPECT_FALSE(MapWithResegmentation(segments, "が完成度高い",
                                     ResegmenterFor({1, 8}, cut, &asked))
                   .has_value());
  EXPECT_THAT(asked, ::testing::Not(::testing::Contains(ElementsAre(1, 8))));
}

Segments KyouHa() {
  Segments segments;
  AddSegment(segments, "きょう", {"今日", "京"});
  AddSegment(segments, "は", {"は", "歯"});
  return segments;
}

class ZenzCorrectorTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    Clock::SetClockForUnitTest(&clock_);
    // The lock screen turns secure input on, which keeps zenz out.
    ZenzCorrector::IgnoreSecureInputForTesting(true);
    answers_ = FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(),
                                  "answers.tsv");
    ASSERT_OK(FileUtil::SetContents(answers_,
                                    "キョウハ\t京歯\nアメ\t雨\nカップ\t河童\n"
                                    "３カイ\t三回\nサンカイ\t3回\n５カイ\t5回\n"
                                    "ヒャクナナ\t107\n"));
    client_ = std::make_unique<ScorerClient>(std::vector<std::string>{
        testing::GetSourceFileOrDie({"floatype", "zenz", "fake_scorer"}),
        answers_});
  }
  void TearDown() override {
    client_.reset();
    Clock::SetClockForUnitTest(nullptr);
    ZenzCorrector::IgnoreSecureInputForTesting(false);
  }

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
  // The fake scorer reads it when the first request starts it.
  std::string answers_;
  std::unique_ptr<ScorerClient> client_;
};

TEST_F(ZenzCorrectorTest, AppliesAnAnswerThatDiffersFromMozc) {
  ZenzCorrector corrector(client_.get());
  const Segments segments = KyouHa();
  ASSERT_TRUE(corrector.Request(segments, "熱が"));
  const ZenzCorrector::PollResult result = PollUntilDone(corrector, segments);
  EXPECT_EQ(result.kind, ZenzCorrector::PollResult::kApply);
  EXPECT_THAT(result.candidate_indices, ElementsAre(1, 1));
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kDone)
      << "one correction per conversion";
}

TEST_F(ZenzCorrectorTest, AppliesAnAnswerOnTheConversionSegmentedAgain) {
  ZenzCorrector corrector(client_.get());
  Segments segments;
  AddSegment(segments, "ひゃく", {"百", "１００", "100"});
  AddSegment(segments, "なな", {"ナナ", "七"});
  // The fake answers 107, which Mozc's segments cannot write.
  ASSERT_TRUE(corrector.Request(segments, ""));
  EXPECT_EQ(PollUntilDone(corrector, segments).kind,
            ZenzCorrector::PollResult::kDone)
      << "without a way to segment again";

  std::vector<uint8_t> asked;
  const Resegmenter resegment = [&](absl::Span<const uint8_t> sizes) {
    asked.assign(sizes.begin(), sizes.end());
    Segments resegmented;
    AddSegment(resegmented, "ひゃくなな", {"ひゃくなな", "ヒャクナナ"});
    return std::optional<Segments>(std::move(resegmented));
  };
  ASSERT_TRUE(corrector.Request(segments, ""));
  ZenzCorrector::PollResult result;
  for (int i = 0; i < 2000; ++i) {
    result = corrector.Poll(segments, resegment);
    if (result.kind != ZenzCorrector::PollResult::kWait) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(result.kind, ZenzCorrector::PollResult::kApply);
  EXPECT_THAT(asked, ElementsAre(5));
  ASSERT_TRUE(result.segments.has_value());
  EXPECT_THAT(result.candidate_indices, ElementsAre(2));
  EXPECT_EQ(result.segments->conversion_segment(0).candidate(2).value, "１０７")
      << "the digits in Mozc's width, added as a candidate";
}

TEST_F(ZenzCorrectorTest, KeepsMozcWhenZenzAgrees) {
  ZenzCorrector corrector(client_.get());
  Segments segments;
  AddSegment(segments, "あめ", {"雨", "飴"});
  ASSERT_TRUE(corrector.Request(segments, ""));
  EXPECT_EQ(PollUntilDone(corrector, segments).kind,
            ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, IgnoresTheReadingHandedBackUnconverted) {
  ZenzCorrector corrector(client_.get());
  Segments segments;
  AddSegment(segments, "いぬ", {"犬", "イヌ", "いぬ"});
  ASSERT_TRUE(corrector.Request(segments, ""));  // The fake answers イヌ.
  EXPECT_EQ(PollUntilDone(corrector, segments).kind,
            ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, KeepsTheDigitsTheUserTyped) {
  ZenzCorrector corrector(client_.get());
  Segments typed;
  AddSegment(typed, "３かい", {"3回", "三回"});
  ASSERT_TRUE(corrector.Request(typed, ""));  // The fake answers 三回.
  EXPECT_EQ(PollUntilDone(corrector, typed).kind,
            ZenzCorrector::PollResult::kDone);

  // Digits zenz writes from kana are its to choose.
  Segments spoken;
  AddSegment(spoken, "さんかい", {"三回", "3回"});
  ASSERT_TRUE(corrector.Request(spoken, ""));
  const ZenzCorrector::PollResult result = PollUntilDone(corrector, spoken);
  EXPECT_EQ(result.kind, ZenzCorrector::PollResult::kApply);
  EXPECT_THAT(result.candidate_indices, ElementsAre(1));

  // Where Mozc lost a typed digit, zenz may bring it back.
  Segments lost;
  AddSegment(lost, "５かい", {"五回", "5回"});
  ASSERT_TRUE(corrector.Request(lost, ""));
  EXPECT_EQ(PollUntilDone(corrector, lost).kind,
            ZenzCorrector::PollResult::kApply);
}

TEST_F(ZenzCorrectorTest, DropsAnUnfinishedAnswer) {
  ZenzCorrector corrector(client_.get());
  Segments segments;
  AddSegment(segments, "かっぷ", {"カップ", "河童"});
  // The fake answers 河童, stopped by the length cap.
  ASSERT_TRUE(corrector.Request(segments, ""));
  EXPECT_EQ(PollUntilDone(corrector, segments).kind,
            ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, GivesUpAfterTheTimeout) {
  ZenzCorrector corrector(client_.get());
  Segments segments;
  AddSegment(segments, "すろう", {"スロウ"});  // The fake waits 200ms.
  ASSERT_TRUE(corrector.Request(segments, ""));
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kWait);
  clock_.Advance(ZenzCorrector::kTimeout);
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, TheTimeoutCanBeSet) {
  ZenzCorrector corrector(client_.get());
  corrector.set_timeout(absl::Milliseconds(1000));
  Segments segments;
  AddSegment(segments, "すろう", {"スロウ"});
  ASSERT_TRUE(corrector.Request(segments, ""));
  clock_.Advance(ZenzCorrector::kTimeout);
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kWait);
  clock_.Advance(absl::Milliseconds(700));
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, DropsTheAnswerAfterCancelOrAChangedConversion) {
  ZenzCorrector corrector(client_.get());
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

TEST_F(ZenzCorrectorTest, LeavesLongReadingsToMozc) {
  ZenzCorrector corrector(client_.get());
  std::string reading;
  for (size_t i = 0; i <= ZenzCorrector::kMaxReadingChars; ++i) reading += "あ";
  Segments segments;
  AddSegment(segments, reading, {"亜"});
  EXPECT_FALSE(corrector.Request(segments, ""));
}

TEST_F(ZenzCorrectorTest, DoesNothingWithoutAScorer) {
  ZenzCorrector corrector(nullptr);
  const Segments segments = KyouHa();
  EXPECT_FALSE(corrector.Request(segments, ""));
  EXPECT_EQ(corrector.Poll(segments).kind, ZenzCorrector::PollResult::kDone);
}

// KyouHa() with the candidates `text` names on top, as the session shows a
// correction.
Segments KyouHaShowing(absl::string_view text) {
  Segments segments = KyouHa();
  const std::vector<int> indices = MapToCandidates(segments, text).value();
  for (size_t i = 0; i < indices.size(); ++i) {
    segments.mutable_conversion_segment(i)->move_candidate(indices[i], 0);
  }
  return segments;
}

TEST_F(ZenzCorrectorTest, NotThisCyclesThroughZenzsNextBest) {
  ASSERT_OK(FileUtil::SetContents(answers_, "キョウハ\t京歯\t今日歯\t京は\n"));
  ZenzCorrector corrector(client_.get());
  ASSERT_TRUE(corrector.Request(KyouHa(), "熱が"));
  ASSERT_EQ(PollUntilDone(corrector, KyouHa()).text, "京歯");

  const Segments first = KyouHaShowing("京歯");
  ASSERT_TRUE(corrector.Request(first, "熱が", /*not_this=*/true));
  const ZenzCorrector::PollResult second = PollUntilDone(corrector, first);
  EXPECT_EQ(second.kind, ZenzCorrector::PollResult::kApply);
  EXPECT_EQ(second.text, "今日歯");

  const Segments shown = KyouHaShowing("今日歯");
  ASSERT_TRUE(corrector.Request(shown, "熱が", /*not_this=*/true));
  EXPECT_EQ(PollUntilDone(corrector, shown).text, "京は");

  const Segments last = KyouHaShowing("京は");
  ASSERT_TRUE(corrector.Request(last, "熱が", /*not_this=*/true));
  EXPECT_EQ(PollUntilDone(corrector, last).kind,
            ZenzCorrector::PollResult::kDone)
      << "nothing is left";
}

TEST_F(ZenzCorrectorTest, NotThisOnMozcsConversionWhenZenzAgreed) {
  ASSERT_OK(FileUtil::SetContents(answers_, "アメ\t雨\t飴\n"));
  ZenzCorrector corrector(client_.get());
  Segments segments;
  AddSegment(segments, "あめ", {"雨", "飴"});
  ASSERT_TRUE(corrector.Request(segments, ""));
  ASSERT_EQ(PollUntilDone(corrector, segments).kind,
            ZenzCorrector::PollResult::kDone);
  ASSERT_TRUE(corrector.Request(segments, "", /*not_this=*/true));
  const ZenzCorrector::PollResult result = PollUntilDone(corrector, segments);
  EXPECT_EQ(result.kind, ZenzCorrector::PollResult::kApply);
  EXPECT_THAT(result.candidate_indices, ElementsAre(1));
}

TEST_F(ZenzCorrectorTest, NotThisSkipsAnswersThatDidNotShow) {
  // zenz's first answer cannot be written with Mozc's candidates.
  ASSERT_OK(FileUtil::SetContents(answers_, "キョウハ\t今日葉\t京歯\n"));
  ZenzCorrector corrector(client_.get());
  ASSERT_TRUE(corrector.Request(KyouHa(), ""));
  ASSERT_EQ(PollUntilDone(corrector, KyouHa()).kind,
            ZenzCorrector::PollResult::kDone);
  ASSERT_TRUE(corrector.Request(KyouHa(), "", /*not_this=*/true));
  EXPECT_EQ(PollUntilDone(corrector, KyouHa()).text, "京歯");
}

TEST_F(ZenzCorrectorTest, NotThisAsksAgainWhileAnswersDoNotShow) {
  // No earlier answer is known, as after a timeout, and zenz's next two
  // cannot be written with Mozc's candidates.
  // After them, kMaxAsks answers that cannot either: asking stops there.
  std::string ima_answers = "イマ";
  for (int i = 0; i < ZenzCorrector::kMaxAsks; ++i) {
    absl::StrAppend(&ima_answers, "\t今", i);
  }
  ASSERT_OK(FileUtil::SetContents(
      answers_, absl::StrCat("キョウハ\t今日ハ\t今日葉\t今日歯\n", ima_answers,
                             "\t居間\n")));
  ZenzCorrector corrector(client_.get());
  const Segments segments = KyouHa();
  ASSERT_TRUE(corrector.Request(segments, "", /*not_this=*/true));
  EXPECT_EQ(PollUntilDone(corrector, segments).text, "今日歯")
      << "the third ask";

  Segments ima;
  AddSegment(ima, "いま", {"今", "居間"});
  ASSERT_TRUE(corrector.Request(ima, "", /*not_this=*/true));
  EXPECT_EQ(PollUntilDone(corrector, ima).kind,
            ZenzCorrector::PollResult::kDone);
}

TEST_F(ZenzCorrectorTest, NotThisEndsWhereTheRequestWouldGrowTooLong) {
  ZenzCorrector corrector(client_.get());
  std::string value;
  for (int i = 0; i < 2000; ++i) value += "亜";  // 6000 bytes.
  Segments segments;
  AddSegment(segments, "あ", {value, "阿"});
  EXPECT_TRUE(corrector.Request(segments, ""));
  EXPECT_FALSE(corrector.Request(segments, "", /*not_this=*/true));
}

TEST_F(ZenzCorrectorTest, NotThisForgetsWithAnotherReadingOrAPlainRequest) {
  ASSERT_OK(FileUtil::SetContents(answers_, "アメ\t雨\t飴\nキョウハ\t京歯\n"));
  ZenzCorrector corrector(client_.get());
  Segments ame;
  AddSegment(ame, "あめ", {"雨", "飴"});
  ASSERT_TRUE(corrector.Request(ame, "", /*not_this=*/true));
  ASSERT_EQ(PollUntilDone(corrector, ame).text, "飴");
  // 飴 is shown now; turning it down too leaves nothing.
  Segments candy;
  AddSegment(candy, "あめ", {"飴", "雨"});
  ASSERT_TRUE(corrector.Request(candy, "", /*not_this=*/true));
  ASSERT_EQ(PollUntilDone(corrector, candy).kind,
            ZenzCorrector::PollResult::kDone);
  // Another context is another conversion.
  ASSERT_TRUE(corrector.Request(candy, "空から", /*not_this=*/true));
  EXPECT_EQ(PollUntilDone(corrector, candy).text, "雨");
  // So is a plain request, as for a new conversion of the same reading.
  ASSERT_TRUE(corrector.Request(ame, ""));
  PollUntilDone(corrector, ame);
  ASSERT_TRUE(corrector.Request(ame, "", /*not_this=*/true));
  EXPECT_EQ(PollUntilDone(corrector, ame).text, "飴");
}

}  // namespace
}  // namespace mozc::floatype
