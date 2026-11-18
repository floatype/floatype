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
