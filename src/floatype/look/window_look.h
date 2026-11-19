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
  // Mozc's dark palette under the colors below, else its light one.
  bool dark = false;
  // Colors are 0xRRGGBB.
  uint32_t background = 0;  // The window and the candidates' cells.
  uint32_t text = 0;
  uint32_t secondary_text = 0;     // The shortcut numbers.
  uint32_t number_background = 0;  // The shortcut numbers' column.
  uint32_t focused_background = 0;
  uint32_t focused_border = 0;
  uint32_t border = 0;
  uint32_t footer_top = 0;  // The footer's gradient.
  uint32_t footer_bottom = 0;
  uint32_t scrollbar_background = 0;
  uint32_t size_percent = 100;  // Fonts and rows, in percent of Mozc's.
  double corner_radius = 0;
  double opacity = 1;  // Of the whole window, 0 to 1.
  config::Config::CandidateStyle::Shadow shadow =
      config::Config::CandidateStyle::SOFT;
  // The shadow to draw. SOFT's numbers come near the system's window shadow,
  // which the macOS renderer uses instead; STRONG's are drawn on top of it.
  double shadow_radius = 0;
  double shadow_offset_y = 0;  // Downward.
  double shadow_opacity = 0;
  std::string font_family;  // Empty for the system font.
};

// The look for `style`; `system_dark` is the system appearance, for AUTO.
WindowLook ResolveWindowLook(const config::Config::CandidateStyle& style,
                             bool system_dark);

// Mozc's style for the look's palette with its colors, sizes and font family
// in (font_name, which a renderer must fall back from if it is not
// installed).
renderer::RendererStyle RendererStyleFor(const WindowLook& look);

// Scales every font size in `style` (candidates, shortcuts, descriptions,
// footer, information list) by `percent`; 100 leaves it as it is.
void ScaleFonts(renderer::RendererStyle& style, uint32_t percent);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_LOOK_WINDOW_LOOK_H_
