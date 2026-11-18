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

namespace {

IndicatorRect Size(const Style& style, double label_width) {
  constexpr double kLabelPadding = 8;  // On each side of the label.
  IndicatorRect rect;
  rect.height = style.height();
  rect.width =
      style.shape() == Style::CIRCLE
          ? rect.height
          : std::max<double>(style.width(), label_width + 2 * kLabelPadding);
  return rect;
}

}  // namespace

IndicatorRect LayoutIndicatorWithoutCaret(const Style& style,
                                          double label_width,
                                          const IndicatorRect& screen) {
  constexpr double kMargin = 48;  // Above the Dock's usual height.
  IndicatorRect rect = Size(style, label_width);
  rect.x = screen.x + (screen.width - rect.width) / 2;
  rect.y = screen.y + kMargin;
  return rect;
}

IndicatorRect LayoutIndicator(const Style& style, double label_width,
                              double line_left, double line_bottom,
                              double line_height, double screen_top) {
  IndicatorRect rect = Size(style, label_width);
  if (style.placement() == Style::SIDE) {
    rect.x = line_left - style.gap() - rect.width + style.offset_x();
    rect.y = line_bottom + (line_height - rect.height) / 2 + style.offset_y();
    return rect;
  }
  rect.x = line_left + style.offset_x();
  const double above = line_bottom + line_height + style.gap();
  const double below = line_bottom - style.gap() - rect.height;
  const bool fits_above = above + rect.height <= screen_top;
  rect.y = (style.placement() == Style::ABOVE && fits_above) ? above : below;
  rect.y += style.offset_y();
  return rect;
}

}  // namespace mozc::floatype
