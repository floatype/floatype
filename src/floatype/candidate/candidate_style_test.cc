#include "floatype/candidate/candidate_style.h"

#include "protocol/renderer_style.pb.h"
#include "renderer/renderer_style_handler.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

TEST(CandidateStyleTest, ScalesEveryFontSize) {
  renderer::RendererStyle style =
      renderer::RendererStyleHandler::GetRendererStyle();
  ASSERT_EQ(style.candidate_style().font_size(), 14);
  ScaleFonts(style, 150);
  EXPECT_EQ(style.candidate_style().font_size(), 21);
  EXPECT_EQ(style.shortcut_style().font_size(), 21);
  EXPECT_EQ(style.description_style().font_size(), 18);
  EXPECT_EQ(style.footer_style().font_size(), 21);
  EXPECT_EQ(style.infolist_style().title_style().font_size(), 22.5);
  // Nothing else changes.
  EXPECT_EQ(
      style.row_rect_padding(),
      renderer::RendererStyleHandler::GetRendererStyle().row_rect_padding());
}

TEST(CandidateStyleTest, PutsTheSetColorsInAndLeavesTheRest) {
  renderer::RendererStyle style =
      renderer::RendererStyleHandler::GetRendererStyle();
  const renderer::RendererStyle mozc = style;
  config::Config::CandidateStyle colors;
  colors.set_text_color(0x112233);
  colors.set_focused_background_color(0x445566);
  ApplyColors(style, colors);
  EXPECT_EQ(style.candidate_style().foreground_color().r(), 0x11);
  EXPECT_EQ(style.candidate_style().foreground_color().g(), 0x22);
  EXPECT_EQ(style.candidate_style().foreground_color().b(), 0x33);
  EXPECT_EQ(style.focused_background_color().b(), 0x66);
  EXPECT_EQ(style.border_color().SerializeAsString(),
            mozc.border_color().SerializeAsString());
  EXPECT_EQ(style.focused_border_color().SerializeAsString(),
            mozc.focused_border_color().SerializeAsString());
}

TEST(CandidateStyleTest, ScalesNothingAtAHundredPercent) {
  renderer::RendererStyle style =
      renderer::RendererStyleHandler::GetRendererStyle();
  ScaleFonts(style, 100);
  EXPECT_EQ(
      style.SerializeAsString(),
      renderer::RendererStyleHandler::GetRendererStyle().SerializeAsString());
}

}  // namespace
}  // namespace mozc::floatype
