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

TEST(WindowLookTest, LightIsMozcsLightPalette) {
  const WindowLook look =
      ResolveWindowLook(Theme(Config::CandidateStyle::LIGHT), true);
  EXPECT_FALSE(look.dark);
  // renderer/renderer_style.textproto, and the handler's white background.
  EXPECT_EQ(look.background, 0xffffff);
  EXPECT_EQ(look.text, 0x000000);
  EXPECT_EQ(look.secondary_text, 0x616161);
  EXPECT_EQ(look.number_background, 0xf3f4ff);
  EXPECT_EQ(look.focused_background, 0xd1eaff);
  EXPECT_EQ(look.focused_border, 0x7facdd);
  EXPECT_EQ(look.border, 0x969696);
  EXPECT_EQ(look.footer_top, 0xffffff);
  EXPECT_EQ(look.footer_bottom, 0xeeeeee);
}

TEST(WindowLookTest, DarkIsMozcsDarkPalette) {
  const WindowLook look =
      ResolveWindowLook(Theme(Config::CandidateStyle::DARK), false);
  EXPECT_TRUE(look.dark);
  // renderer/renderer_style_dark.textproto.
  EXPECT_EQ(look.background, 0x2c2c2c);
  EXPECT_EQ(look.text, 0xe5e5e5);
  EXPECT_EQ(look.focused_background, 0x3d3d3d);
  EXPECT_EQ(look.border, 0x4a4a4a);
}

TEST(WindowLookTest, AutoFollowsTheSystem) {
  const Config::CandidateStyle style;
  EXPECT_EQ(style.theme(), Config::CandidateStyle::AUTO);
  EXPECT_FALSE(ResolveWindowLook(style, false).dark);
  EXPECT_TRUE(ResolveWindowLook(style, true).dark);
  EXPECT_EQ(ResolveWindowLook(style, true).background, 0x2c2c2c);
}

TEST(WindowLookTest, ColorsCountOnlyWithCustom) {
  Config::CandidateStyle style = Theme(Config::CandidateStyle::LIGHT);
  style.set_text_color(0x112233);
  EXPECT_EQ(ResolveWindowLook(style, false).text, 0x000000);
  style.set_theme(Config::CandidateStyle::CUSTOM);
  const WindowLook look = ResolveWindowLook(style, true);
  EXPECT_FALSE(look.dark) << "custom builds on the light palette";
  EXPECT_EQ(look.text, 0x112233);
  // Unset ones are the light palette's.
  EXPECT_EQ(look.background, 0xffffff);
  EXPECT_EQ(look.number_background, 0xf3f4ff);
  EXPECT_EQ(look.focused_background, 0xd1eaff);
  EXPECT_EQ(look.focused_border, 0x7facdd);
  EXPECT_EQ(look.border, 0x969696);
}

TEST(WindowLookTest, ACustomBackgroundFillsTheWholeWindow) {
  Config::CandidateStyle style = Theme(Config::CandidateStyle::CUSTOM);
  style.set_background_color(0x202830);
  style.set_focused_background_color(0x405060);
  style.set_focused_border_color(0x708090);
  style.set_border_color(0x010203);
  const WindowLook look = ResolveWindowLook(style, false);
  EXPECT_EQ(look.background, 0x202830);
  EXPECT_EQ(look.number_background, 0x202830);
  EXPECT_EQ(look.footer_top, 0x202830);
  EXPECT_EQ(look.footer_bottom, 0x202830);
  EXPECT_EQ(look.scrollbar_background, 0x202830);
  EXPECT_EQ(look.focused_background, 0x405060);
  EXPECT_EQ(look.focused_border, 0x708090);
  EXPECT_EQ(look.border, 0x010203);
}

TEST(WindowLookTest, TheDefaultsAreTodaysLook) {
  const WindowLook look = ResolveWindowLook(Config::CandidateStyle(), false);
  EXPECT_EQ(look.size_percent, 100);
  EXPECT_EQ(look.corner_radius, 0);
  EXPECT_EQ(look.opacity, 1);
  EXPECT_EQ(look.shadow, Config::CandidateStyle::SOFT);
  EXPECT_TRUE(look.font_family.empty());
  // The light style the renderer drew before, color for color.
  const RendererStyle style = RendererStyleFor(look);
  const RendererStyle mozc = RendererStyleHandler::GetRendererStyle();
  for (const auto& [mine, theirs] :
       {std::pair{style.candidate_style(), mozc.candidate_style()},
        std::pair{style.shortcut_style(), mozc.shortcut_style()},
        std::pair{style.description_style(), mozc.description_style()},
        std::pair{style.footer_style(), mozc.footer_style()}}) {
    EXPECT_EQ(mine.font_size(), theirs.font_size());
    EXPECT_FALSE(mine.has_font_name());
    EXPECT_EQ(mine.foreground_color().r(), theirs.foreground_color().r());
    EXPECT_EQ(mine.foreground_color().b(), theirs.foreground_color().b());
    EXPECT_EQ(mine.has_background_color(), theirs.has_background_color());
    EXPECT_EQ(mine.background_color().g(), theirs.background_color().g());
  }
  EXPECT_EQ(style.focused_background_color().b(),
            mozc.focused_background_color().b());
  EXPECT_EQ(style.border_color().r(), mozc.border_color().r());
  EXPECT_EQ(style.footer_bottom_color().r(), mozc.footer_bottom_color().r());
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
