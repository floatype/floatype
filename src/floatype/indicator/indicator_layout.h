// Where and how big floatype's indicator is drawn, apart from Cocoa so that it
// can be tested.

#ifndef MOZC_FLOATYPE_INDICATOR_INDICATOR_LAYOUT_H_
#define MOZC_FLOATYPE_INDICATOR_INDICATOR_LAYOUT_H_

#include <optional>
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

// Whether `window` is a candidate list being chosen from (after Space, or
// Tab or Down into the suggestions), which hides the indicator and the ruby,
// as Mozkey does. Suggestions shown while typing are not.
bool ChoosingCandidates(const commands::CandidateWindow& window);

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

// LayoutIndicator on a real screen: the frame inside `screen`, the visible
// area, and clear of `obstacle` (the suggestion window) where given. The
// placement is a preference, as a reader at the screen's edge still needs to
// see the state: above goes below when it does not fit, below goes above, and
// the side goes above, then below (right of the line would cover the
// composition). Above and below stay inside the screen sideways. Where the
// suggestion window is in the way, the frame goes beside it. Nothing where
// nothing fits, rather than a frame over the text.
std::optional<IndicatorRect> PlaceIndicator(
    const config::Config::IndicatorStyle& style, double label_width,
    double line_left, double line_bottom, double line_height,
    const IndicatorRect& screen, const IndicatorRect* obstacle);

// The frame for vertical writing, where the composition runs down a column
// from `column_left` to `column_right` with its top at `column_top`: right of
// the column at its top, or left of it when the right side leaves the screen
// at `screen_right`. The placement setting is for horizontal writing.
IndicatorRect LayoutIndicatorVertical(
    const config::Config::IndicatorStyle& style, double label_width,
    double column_left, double column_right, double column_top,
    double screen_right);

// The frame of the reading shown above a live conversion: at the start of the
// line, just above it or, when the indicator sits above the line, above that.
IndicatorRect LayoutRuby(double width, double height, double line_left,
                         double line_top, const IndicatorRect* indicator_above);

// The same beside a vertical column: right of it at its top or, when the
// indicator sits there, right of that.
IndicatorRect LayoutRubyVertical(double width, double height,
                                 double column_right, double column_top,
                                 const IndicatorRect* indicator_beside);

// The frame used when the app does not tell where the composition is: at the
// bottom center of the screen area `screen`.
IndicatorRect LayoutIndicatorWithoutCaret(
    const config::Config::IndicatorStyle& style, double label_width,
    const IndicatorRect& screen);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_INDICATOR_INDICATOR_LAYOUT_H_
