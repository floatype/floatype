#include "floatype/indicator/indicator_layout.h"

#include <optional>

#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

using Indicator = commands::Output::Indicator;
using Style = config::Config::IndicatorStyle;

TEST(IndicatorLayoutTest, ChoosingCandidatesHidesTheIndicator) {
  commands::CandidateWindow window;
  EXPECT_FALSE(ChoosingCandidates(window)) << "no candidates";
  window.add_candidate()->set_value("機械");
  window.set_category(commands::SUGGESTION);
  EXPECT_FALSE(ChoosingCandidates(window)) << "suggestions while typing";
  window.set_focused_index(0);
  EXPECT_TRUE(ChoosingCandidates(window)) << "Tab or Down into them";
  window.clear_focused_index();
  window.set_category(commands::PREDICTION);
  EXPECT_TRUE(ChoosingCandidates(window));
  window.set_category(commands::CONVERSION);
  EXPECT_TRUE(ChoosingCandidates(window)) << "after Space";
}

TEST(IndicatorLayoutTest, LabelsFollowTheStyle) {
  EXPECT_EQ(IndicatorLabel(Indicator::READING, Style::FULL_LABEL), "入力中");
  EXPECT_EQ(IndicatorLabel(Indicator::ZENZ_PENDING, Style::FULL_LABEL),
            "Zenz…");
  EXPECT_EQ(IndicatorLabel(Indicator::ZENZ, Style::SYMBOL), "Z");
  EXPECT_EQ(IndicatorLabel(Indicator::MOZC, Style::COLOR_ONLY), "");
  EXPECT_EQ(IndicatorLabel(Indicator::NONE, Style::FULL_LABEL), "");
}

TEST(IndicatorLayoutTest, ColorsDefaultToMozkeys) {
  const Style style;
  EXPECT_EQ(IndicatorColor(Indicator::READING, style), 0x747d8cu);
  EXPECT_EQ(IndicatorColor(Indicator::MOZC, style), 0x586b85u);
  EXPECT_EQ(IndicatorColor(Indicator::ZENZ_PENDING, style), 0x847438u);
  EXPECT_EQ(IndicatorColor(Indicator::ZENZ, style), 0x338d85u);
}

TEST(IndicatorLayoutTest, SitsAboveTheLineWhenItFits) {
  const Style style;  // 56x22, 5pt gap, above.
  const IndicatorRect rect = LayoutIndicator(style, 30, 100, 500, 20, 1000);
  EXPECT_EQ(rect.x, 100);
  EXPECT_EQ(rect.y, 500 + 20 + 5);
  EXPECT_EQ(rect.width, 56);
  EXPECT_EQ(rect.height, 22);
}

TEST(IndicatorLayoutTest, GoesBelowAtTheTopOfTheScreenOrWhenAsked) {
  const Style style;
  EXPECT_EQ(LayoutIndicator(style, 30, 100, 980, 20, 1000).y, 980 - 5 - 22);
  Style below;
  below.set_placement(Style::BELOW);
  EXPECT_EQ(LayoutIndicator(below, 30, 100, 500, 20, 1000).y, 500 - 5 - 22);
}

// A 1440x1000 screen whose visible area starts at the origin.
constexpr IndicatorRect kScreen = {0, 0, 1440, 1000};

void ExpectSame(const std::optional<IndicatorRect>& placed,
                const IndicatorRect& laid_out) {
  ASSERT_TRUE(placed.has_value());
  EXPECT_EQ(placed->x, laid_out.x);
  EXPECT_EQ(placed->y, laid_out.y);
  EXPECT_EQ(placed->width, laid_out.width);
  EXPECT_EQ(placed->height, laid_out.height);
}

TEST(PlaceIndicatorTest, KeepsWhereItFitsAsBefore) {
  const Style above;
  ExpectSame(PlaceIndicator(above, 30, 100, 500, 20, 500, kScreen, nullptr),
             LayoutIndicator(above, 30, 100, 500, 20, 1000));
  ExpectSame(PlaceIndicator(above, 30, 100, 980, 20, 980, kScreen, nullptr),
             LayoutIndicator(above, 30, 100, 980, 20, 1000));
  Style below;
  below.set_placement(Style::BELOW);
  ExpectSame(PlaceIndicator(below, 30, 100, 500, 20, 500, kScreen, nullptr),
             LayoutIndicator(below, 30, 100, 500, 20, 1000));
  Style side;
  side.set_placement(Style::SIDE);
  ExpectSame(PlaceIndicator(side, 30, 100, 500, 20, 500, kScreen, nullptr),
             LayoutIndicator(side, 30, 100, 500, 20, 1000));
}

TEST(PlaceIndicatorTest, BelowGoesAboveAtTheBottomOfTheScreen) {
  Style below;
  below.set_placement(Style::BELOW);
  const std::optional<IndicatorRect> rect =
      PlaceIndicator(below, 30, 100, 10, 20, 10, kScreen, nullptr);
  ASSERT_TRUE(rect.has_value());
  EXPECT_EQ(rect->y, 10 + 20 + 5);
}

TEST(PlaceIndicatorTest, TheSideGoesAboveAtTheLeftOfTheScreen) {
  Style side;
  side.set_placement(Style::SIDE);
  const std::optional<IndicatorRect> rect =
      PlaceIndicator(side, 30, 20, 500, 20, 500, kScreen, nullptr);
  ASSERT_TRUE(rect.has_value());
  EXPECT_EQ(rect->x, 20);
  EXPECT_EQ(rect->y, 500 + 20 + 5);
}

TEST(PlaceIndicatorTest, StaysInsideTheScreenSideways) {
  const Style style;
  const std::optional<IndicatorRect> rect =
      PlaceIndicator(style, 30, 1420, 500, 20, 500, kScreen, nullptr);
  ASSERT_TRUE(rect.has_value());
  EXPECT_EQ(rect->x, 1440 - 56);
}

TEST(PlaceIndicatorTest, GoesBesideTheSuggestionWindowInTheWay) {
  const Style style;
  // At the top of the screen the frame goes below the line, where the
  // suggestion window is.
  const IndicatorRect suggestions = {100, 800, 300, 175};
  const std::optional<IndicatorRect> rect =
      PlaceIndicator(style, 30, 100, 980, 20, 980, kScreen, &suggestions);
  ASSERT_TRUE(rect.has_value());
  EXPECT_EQ(rect->x, 100 + 300 + 5);
  EXPECT_EQ(rect->y, 980 - 5 - 22);
  // Above the line, where it fits, the suggestions below do not matter.
  ExpectSame(PlaceIndicator(style, 30, 100, 500, 20, 500, kScreen, &suggestions),
             LayoutIndicator(style, 30, 100, 500, 20, 1000));
}

TEST(PlaceIndicatorTest, GoesBelowTheLastLineOfAWrappedComposition) {
  const Style style;
  // At the top of the screen, the composition wraps from the line at 980 to
  // one at 950: below goes under the second line, not onto it.
  const std::optional<IndicatorRect> rect =
      PlaceIndicator(style, 30, 100, 980, 20, 950, kScreen, nullptr);
  ASSERT_TRUE(rect.has_value());
  EXPECT_EQ(rect->y, 950 - 5 - 22);
  // Above is still above the first line.
  ExpectSame(PlaceIndicator(style, 30, 100, 500, 20, 470, kScreen, nullptr),
             LayoutIndicator(style, 30, 100, 500, 20, 1000));
}

TEST(PlaceIndicatorTest, NothingWhereNothingFits) {
  const Style style;
  // A screen just the line's height: neither above nor below fits.
  EXPECT_FALSE(PlaceIndicator(style, 30, 100, 0, 20, 0, {0, 0, 1440, 20}, nullptr)
                   .has_value());
}

TEST(IndicatorLayoutTest, GrowsToFitTheLabelAndCirclesAreRound) {
  const Style style;
  EXPECT_EQ(LayoutIndicator(style, 60, 0, 0, 20, 1000).width, 60 + 16);
  Style circle;
  circle.set_shape(Style::CIRCLE);
  EXPECT_EQ(LayoutIndicator(circle, 60, 0, 0, 20, 1000).width, 22);
}

TEST(IndicatorLayoutTest, SitsLeftOfTheLineOnTheSide) {
  Style side;
  side.set_placement(Style::SIDE);
  const IndicatorRect rect = LayoutIndicator(side, 30, 100, 500, 20, 1000);
  EXPECT_EQ(rect.x, 100 - 5 - 56);
  EXPECT_EQ(rect.y, 500 + (20 - 22) / 2.0);
}

// #41: the candidate window goes below the last line of a wrapped
// composition, not over its second line.
TEST(IndicatorLayoutTest, CandidatesGoBelowTheLastLine) {
  EXPECT_EQ(CandidatesBelow(718, 740, false), 740);
  EXPECT_EQ(CandidatesBelow(718, 718, false), 718) << "one line";
  EXPECT_EQ(CandidatesBelow(740, 718, false), 740) << "the segment on line 2";
  EXPECT_EQ(CandidatesBelow(718, 0, false), 718) << "unknown";
  EXPECT_EQ(CandidatesBelow(718, 740, true), 718) << "vertical";
}

TEST(IndicatorLayoutTest, SitsRightOfAVerticalColumnAtItsTop) {
  const Style style;  // 56x22, 5pt gap.
  const IndicatorRect rect =
      LayoutIndicatorVertical(style, 30, 400, 420, 700, 1000);
  EXPECT_EQ(rect.x, 420 + 5);
  EXPECT_EQ(rect.y, 700 - 22);
  // Left of the column at the right edge of the screen.
  EXPECT_EQ(LayoutIndicatorVertical(style, 30, 940, 960, 700, 1000).x,
            940 - 5 - 56);
}

TEST(IndicatorLayoutTest, TheRubySitsAboveTheLineOrAboveTheIndicator) {
  const IndicatorRect alone = LayoutRuby(40, 14, 100, 520, nullptr);
  EXPECT_EQ(alone.x, 100);
  EXPECT_EQ(alone.y, 522);
  const IndicatorRect indicator{100, 525, 56, 22};
  EXPECT_EQ(LayoutRuby(40, 14, 100, 520, &indicator).y, 525 + 22 + 2);
}

TEST(IndicatorLayoutTest, TheRubySitsBesideAVerticalColumn) {
  const IndicatorRect alone = LayoutRubyVertical(40, 14, 300, 700, nullptr);
  EXPECT_EQ(alone.x, 302);
  EXPECT_EQ(alone.y, 700 - 14);
  const IndicatorRect indicator{305, 678, 56, 22};
  EXPECT_EQ(LayoutRubyVertical(40, 14, 300, 700, &indicator).x, 305 + 56 + 2);
}

TEST(IndicatorLayoutTest, GoesToTheBottomCenterWithoutACaret) {
  const IndicatorRect screen{0, 40, 1000, 800};
  const IndicatorRect rect = LayoutIndicatorWithoutCaret(Style(), 30, screen);
  EXPECT_EQ(rect.x, (1000 - 56) / 2.0);
  EXPECT_EQ(rect.y, 40 + 48);
}

}  // namespace
}  // namespace mozc::floatype
