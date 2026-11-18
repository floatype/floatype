// floatype: the candidate window's look, adjusted from Mozc's built-in style.

#ifndef MOZC_FLOATYPE_CANDIDATE_CANDIDATE_STYLE_H_
#define MOZC_FLOATYPE_CANDIDATE_CANDIDATE_STYLE_H_

#include <cstdint>

#include "protocol/renderer_style.pb.h"

namespace mozc::floatype {

// Scales every font size in `style` (candidates, shortcuts, descriptions,
// footer, information list) by `percent`; 100 leaves it as it is.
void ScaleFonts(renderer::RendererStyle& style, uint32_t percent);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_CANDIDATE_CANDIDATE_STYLE_H_
