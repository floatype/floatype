#include "floatype/indicator/indicator_layout.h"

#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

using Indicator = commands::Output::Indicator;
using Style = config::Config::IndicatorStyle;

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

TEST(IndicatorLayoutTest, GoesToTheBottomCenterWithoutACaret) {
  const IndicatorRect screen{0, 40, 1000, 800};
  const IndicatorRect rect = LayoutIndicatorWithoutCaret(Style(), 30, screen);
  EXPECT_EQ(rect.x, (1000 - 56) / 2.0);
  EXPECT_EQ(rect.y, 40 + 48);
}

}  // namespace
}  // namespace mozc::floatype
