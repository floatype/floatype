// floatype: the look of the candidate windows, from the settings to the
// colors and sizes to draw. The renderer and the settings screen's preview
// both read it, so the two agree.

#ifndef MOZC_FLOATYPE_LOOK_WINDOW_LOOK_H_
#define MOZC_FLOATYPE_LOOK_WINDOW_LOOK_H_

#include <cstdint>
#include <string>

#include "protocol/config.pb.h"
#include "protocol/renderer_style.pb.h"

namespace mozc::floatype {

struct WindowLook {
  // The dark palette, else the light one.
  bool dark = false;
  // Colors are 0xRRGGBB.
  uint32_t background = 0;
  uint32_t text = 0;
  // The numbers, descriptions, footer and scrollbar.
  uint32_t secondary_text = 0;
  uint32_t focused_background = 0;  // The focused row, a rounded rectangle.
  uint32_t focused_text = 0;
  uint32_t focused_secondary_text = 0;  // Numbers and descriptions on it.
  // The focused row's border, drawn only with an alpha above 0.
  uint32_t focused_border = 0;
  double focused_border_alpha = 0;
  // The window's outline, a hairline.
  uint32_t border = 0;
  double border_alpha = 1;
  uint32_t size_percent = 100;  // Fonts and measures, in percent.
  // Measures in points, at size_percent.
  double window_padding = 0;  // Around the rows and the footer.
  double row_height = 0;
  double row_padding = 0;  // Left and right in a row, and after the number.
  double row_corner_radius = 0;
  double number_width = 0;  // At least, for the shortcut numbers.
  double footer_top_padding = 0;
  double footer_bottom_padding = 0;
  double scrollbar_width = 0;
  double corner_radius = 0;
  double opacity = 1;  // Of the whole window, 0 to 1.
  config::Config::CandidateStyle::Shadow shadow =
      config::Config::CandidateStyle::SOFT;
  // The shadow, as CSS's box-shadow: 0 offset_y radius color/opacity.
  double shadow_radius = 0;
  double shadow_offset_y = 0;  // Downward.
  double shadow_opacity = 0;
  uint32_t shadow_color = 0;
  std::string font_family;  // Empty for the system font.
};

// The look for `style`; `system_dark` is the system appearance, for AUTO.
WindowLook ResolveWindowLook(const config::Config::CandidateStyle& style,
                             bool system_dark);

// Mozc's style with the look's colors, font sizes, paddings and font family
// in (font_name, which a renderer must fall back from if it is not
// installed), for the parts Mozc's table layout and drawing read.
renderer::RendererStyle RendererStyleFor(const WindowLook& look);

// The footer's dot, 0xRRGGBB: `style`'s kind_mark_color where set, else blue
// for the prediction list and grey for conversion candidates.
uint32_t KindMarkColor(const config::Config::CandidateStyle& style,
                       bool prediction, bool dark);

// Scales every font size in `style` (candidates, shortcuts, descriptions,
// footer, information list) by `percent`; 100 leaves it as it is.
void ScaleFonts(renderer::RendererStyle& style, uint32_t percent);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_LOOK_WINDOW_LOOK_H_
