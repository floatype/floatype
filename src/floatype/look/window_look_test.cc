#include "floatype/look/window_look.h"

#include "protocol/config.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/renderer_style_handler.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

using ::mozc::config::Config;
using ::mozc::renderer::RendererStyle;
using ::mozc::renderer::RendererStyleHandler;

Config::CandidateStyle Theme(Config::CandidateStyle::Theme theme) {
  Config::CandidateStyle style;
  style.set_theme(theme);
  return style;
}

TEST(WindowLookTest, TheLightPalette) {
  const WindowLook look =
      ResolveWindowLook(Theme(Config::CandidateStyle::LIGHT), true);
  EXPECT_FALSE(look.dark);
  EXPECT_EQ(look.background, 0xffffff);
  EXPECT_EQ(look.text, 0x17191e);
  EXPECT_EQ(look.secondary_text, 0x989ca5);
  EXPECT_EQ(look.focused_background, 0xc4d0e1);
  EXPECT_EQ(look.focused_text, 0x111318);
  // rgba(17, 19, 24, 0.62) over the focused row.
  EXPECT_EQ(look.focused_secondary_text, 0x555b64);
  EXPECT_EQ(look.focused_border_alpha, 0) << "no focused border";
  EXPECT_EQ(look.border, 0x111827);
  EXPECT_DOUBLE_EQ(look.border_alpha, 0.07);
  EXPECT_EQ(look.shadow_color, 0x111827);
  EXPECT_DOUBLE_EQ(look.shadow_opacity, 0.16);
}

TEST(WindowLookTest, TheDarkPalette) {
  const WindowLook look =
      ResolveWindowLook(Theme(Config::CandidateStyle::DARK), false);
  EXPECT_TRUE(look.dark);
  EXPECT_EQ(look.background, 0x26272b);
  EXPECT_EQ(look.text, 0xeceef2);
  EXPECT_EQ(look.secondary_text, 0x80848d);
  // The same focused row, ink on blue, as in the light.
  EXPECT_EQ(look.focused_background, 0xc4d0e1);
  EXPECT_EQ(look.focused_text, 0x111318);
  EXPECT_EQ(look.border, 0xffffff);
  EXPECT_DOUBLE_EQ(look.border_alpha, 0.09);
  EXPECT_DOUBLE_EQ(look.shadow_opacity, 0.55);
}

TEST(WindowLookTest, AutoFollowsTheSystem) {
  const Config::CandidateStyle style;
  EXPECT_EQ(style.theme(), Config::CandidateStyle::AUTO);
  EXPECT_FALSE(ResolveWindowLook(style, false).dark);
  EXPECT_TRUE(ResolveWindowLook(style, true).dark);
  EXPECT_EQ(ResolveWindowLook(style, true).background, 0x26272b);
}

TEST(WindowLookTest, ColorsCountOnlyWithCustom) {
  Config::CandidateStyle style = Theme(Config::CandidateStyle::LIGHT);
  style.set_text_color(0x112233);
  EXPECT_EQ(ResolveWindowLook(style, false).text, 0x17191e);
  style.set_theme(Config::CandidateStyle::CUSTOM);
  const WindowLook look = ResolveWindowLook(style, true);
  EXPECT_FALSE(look.dark) << "no background: the light palette";
  EXPECT_EQ(look.text, 0x112233);
  // Unset ones are the light palette's.
  EXPECT_EQ(look.background, 0xffffff);
  EXPECT_EQ(look.focused_background, 0xc4d0e1);
  EXPECT_EQ(look.focused_border_alpha, 0);
  EXPECT_EQ(look.border, 0x111827);
}

TEST(WindowLookTest, CustomColorsOverride) {
  Config::CandidateStyle style = Theme(Config::CandidateStyle::CUSTOM);
  style.set_background_color(0xf4f0e6);
  style.set_focused_background_color(0x405060);
  style.set_focused_border_color(0x708090);
  style.set_border_color(0x010203);
  const WindowLook look = ResolveWindowLook(style, false);
  EXPECT_EQ(look.background, 0xf4f0e6);
  EXPECT_EQ(look.focused_background, 0x405060);
  EXPECT_EQ(look.focused_text, 0xffffff) << "white on a dark focused row";
  EXPECT_EQ(look.focused_border, 0x708090);
  EXPECT_EQ(look.focused_border_alpha, 1);
  EXPECT_EQ(look.border, 0x010203);
  EXPECT_EQ(look.border_alpha, 1);
  const RendererStyle rendered = RendererStyleFor(look);
  EXPECT_EQ(rendered.focused_border_color().a(), 1);
  EXPECT_EQ(rendered.border_color().b(), 3);
}

TEST(WindowLookTest, ADarkCustomBackgroundBuildsOnTheDarkPalette) {
  Config::CandidateStyle style = Theme(Config::CandidateStyle::CUSTOM);
  style.set_background_color(0x202830);
  const WindowLook look = ResolveWindowLook(style, false);
  EXPECT_TRUE(look.dark);
  EXPECT_EQ(look.background, 0x202830);
  // Unset ones and the parts without a setting are the dark palette's.
  EXPECT_EQ(look.text, 0xeceef2);
  EXPECT_EQ(look.secondary_text, 0x80848d);
  EXPECT_EQ(look.border, 0xffffff);

  // A light one stays on the light palette, whatever the system is.
  style.set_background_color(0xf4f0e6);
  EXPECT_FALSE(ResolveWindowLook(style, true).dark);
}

TEST(WindowLookTest, TheDefaultMeasures) {
  const WindowLook look = ResolveWindowLook(Config::CandidateStyle(), false);
  EXPECT_EQ(look.size_percent, 100);
  EXPECT_EQ(look.corner_radius, 11);
  EXPECT_EQ(look.opacity, 1);
  EXPECT_EQ(look.shadow, Config::CandidateStyle::SOFT);
  EXPECT_TRUE(look.font_family.empty());
  EXPECT_EQ(look.window_padding, 4);
  EXPECT_EQ(look.row_height, 22);
  EXPECT_EQ(look.row_padding, 9);
  EXPECT_EQ(look.row_corner_radius, 7);
  EXPECT_EQ(look.number_width, 14);
  const RendererStyle style = RendererStyleFor(look);
  EXPECT_EQ(style.candidate_style().font_size(), 14);
  EXPECT_EQ(style.shortcut_style().font_size(), 12);
  EXPECT_EQ(style.description_style().font_size(), 11);
  EXPECT_EQ(style.footer_style().font_size(), 11);
  EXPECT_FALSE(style.shortcut_style().has_background_color())
      << "no number column";
  EXPECT_EQ(style.footer_border_colors_size(), 0) << "no line over the footer";
  EXPECT_EQ(style.window_border(), 4);
  EXPECT_EQ(style.row_rect_padding(), 0);
  EXPECT_EQ(style.shortcut_style().left_padding(), 9);
  EXPECT_EQ(style.description_style().right_padding(), 9);
  EXPECT_EQ(style.description_style().foreground_color().r(), 0x98);
}

TEST(WindowLookTest, MeasuresScaleWithTheSize) {
  Config::CandidateStyle style;
  style.set_size_percent(200);
  const WindowLook look = ResolveWindowLook(style, false);
  EXPECT_EQ(look.row_height, 44);
  EXPECT_EQ(look.window_padding, 8);
  const RendererStyle rendered = RendererStyleFor(look);
  EXPECT_EQ(rendered.window_border(), 8);
  EXPECT_EQ(rendered.shortcut_style().left_padding(), 18);
  EXPECT_EQ(rendered.shortcut_style().font_size(), 24);
}

TEST(WindowLookTest, ShadowPresets) {
  Config::CandidateStyle style;
  const WindowLook soft = ResolveWindowLook(style, false);
  style.set_shadow(Config::CandidateStyle::NONE);
  const WindowLook none = ResolveWindowLook(style, false);
  style.set_shadow(Config::CandidateStyle::STRONG);
  const WindowLook strong = ResolveWindowLook(style, false);
  EXPECT_EQ(none.shadow_opacity, 0);
  EXPECT_EQ(none.shadow_radius, 0);
  EXPECT_GT(soft.shadow_opacity, 0);
  EXPECT_GT(strong.shadow_opacity, soft.shadow_opacity);
  EXPECT_GT(strong.shadow_radius, soft.shadow_radius);
  EXPECT_GT(strong.shadow_offset_y, soft.shadow_offset_y);
}

TEST(WindowLookTest, TheRestGoesThrough) {
  Config::CandidateStyle style;
  style.set_size_percent(150);
  style.set_corner_radius(8);
  style.set_opacity_percent(85);
  style.set_font_family("Hiragino Sans");
  const WindowLook look = ResolveWindowLook(style, false);
  EXPECT_EQ(look.corner_radius, 8);
  EXPECT_DOUBLE_EQ(look.opacity, 0.85);
  const RendererStyle rendered = RendererStyleFor(look);
  EXPECT_EQ(rendered.candidate_style().font_size(), 21);
  EXPECT_EQ(rendered.candidate_style().font_name(), "Hiragino Sans");
  EXPECT_EQ(rendered.footer_style().font_name(), "Hiragino Sans");
}

TEST(WindowLookTest, ScalesEveryFontSize) {
  RendererStyle style = RendererStyleHandler::GetRendererStyle();
  ASSERT_EQ(style.candidate_style().font_size(), 14);
  ScaleFonts(style, 150);
  EXPECT_EQ(style.candidate_style().font_size(), 21);
  EXPECT_EQ(style.shortcut_style().font_size(), 21);
  EXPECT_EQ(style.description_style().font_size(), 18);
  EXPECT_EQ(style.footer_style().font_size(), 21);
  EXPECT_EQ(style.infolist_style().title_style().font_size(), 22.5);
  // Nothing else changes.
  EXPECT_EQ(style.row_rect_padding(),
            RendererStyleHandler::GetRendererStyle().row_rect_padding());
}

TEST(WindowLookTest, ScalesNothingAtAHundredPercent) {
  RendererStyle style = RendererStyleHandler::GetRendererStyle();
  ScaleFonts(style, 100);
  EXPECT_EQ(style.SerializeAsString(),
            RendererStyleHandler::GetRendererStyle().SerializeAsString());
}

}  // namespace
}  // namespace mozc::floatype
