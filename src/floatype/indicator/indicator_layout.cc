#include "floatype/indicator/indicator_layout.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace mozc::floatype {

using State = commands::Output::Indicator::State;
using Style = config::Config::IndicatorStyle;

std::string IndicatorLabel(State state, Style::Label label) {
  if (label == Style::COLOR_ONLY) {
    return "";
  }
  const bool full = label == Style::FULL_LABEL;
  switch (state) {
    case commands::Output::Indicator::READING:
      return full ? "入力中" : "あ";
    case commands::Output::Indicator::MOZC:
      return full ? "Mozc" : "M";
    case commands::Output::Indicator::ZENZ_PENDING:
      return full ? "Zenz…" : "…";
    case commands::Output::Indicator::ZENZ:
      return full ? "Zenz" : "Z";
    default:
      return "";
  }
}

uint32_t IndicatorColor(State state, const Style& style) {
  switch (state) {
    case commands::Output::Indicator::READING:
      return style.reading_color();
    case commands::Output::Indicator::ZENZ_PENDING:
      return style.zenz_pending_color();
    case commands::Output::Indicator::ZENZ:
      return style.zenz_color();
    default:
      return style.mozc_color();
  }
}

IndicatorRect LayoutIndicator(const Style& style, double label_width,
                              double line_left, double line_bottom,
                              double line_height, double screen_top) {
  constexpr double kLabelPadding = 8;  // On each side of the label.
  IndicatorRect rect;
  rect.height = style.height();
  rect.width =
      style.shape() == Style::CIRCLE
          ? rect.height
          : std::max<double>(style.width(), label_width + 2 * kLabelPadding);
  rect.x = line_left + style.offset_x();
  const double above = line_bottom + line_height + style.gap();
  const double below = line_bottom - style.gap() - rect.height;
  const bool fits_above = above + rect.height <= screen_top;
  rect.y = (style.placement() == Style::ABOVE && fits_above) ? above : below;
  rect.y += style.offset_y();
  return rect;
}

}  // namespace mozc::floatype
