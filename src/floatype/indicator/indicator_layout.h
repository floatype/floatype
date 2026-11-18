// Where and how big floatype's indicator is drawn, apart from Cocoa so that it
// can be tested.

#ifndef MOZC_FLOATYPE_INDICATOR_INDICATOR_LAYOUT_H_
#define MOZC_FLOATYPE_INDICATOR_INDICATOR_LAYOUT_H_

#include <string>

#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {

// A rectangle in Cocoa screen coordinates: origin at the bottom left, y up.
struct IndicatorRect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};

// The text the indicator shows for `state`; empty for COLOR_ONLY and NONE.
std::string IndicatorLabel(commands::Output::Indicator::State state,
                           config::Config::IndicatorStyle::Label label);

// The fill color, 0xRRGGBB, for `state`.
uint32_t IndicatorColor(commands::Output::Indicator::State state,
                        const config::Config::IndicatorStyle& style);

// The indicator's frame for a composition whose line starts at `line_left`
// with its bottom at `line_bottom` and the given height. `label_width` is the
// rendered label's width; the frame grows to fit it. When the indicator would
// leave the screen below `screen_top`, it goes to the other side of the line.
IndicatorRect LayoutIndicator(const config::Config::IndicatorStyle& style,
                              double label_width, double line_left,
                              double line_bottom, double line_height,
                              double screen_top);

// The frame used when the app does not tell where the composition is: at the
// bottom center of the screen area `screen`.
IndicatorRect LayoutIndicatorWithoutCaret(
    const config::Config::IndicatorStyle& style, double label_width,
    const IndicatorRect& screen);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_INDICATOR_INDICATOR_LAYOUT_H_
