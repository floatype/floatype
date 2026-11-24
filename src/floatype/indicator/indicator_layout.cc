#include "floatype/indicator/indicator_layout.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

IndicatorRect LayoutRuby(double width, double height, double line_left,
                         double line_top,
                         const IndicatorRect* indicator_above) {
  constexpr double kGap = 2;
  IndicatorRect rect;
  rect.width = width;
  rect.height = height;
  rect.x = line_left;
  rect.y = indicator_above != nullptr
               ? indicator_above->y + indicator_above->height + kGap
               : line_top + kGap;
  return rect;
}

IndicatorRect LayoutRubyVertical(double width, double height,
                                 double column_right, double column_top,
                                 const IndicatorRect* indicator_beside) {
  constexpr double kGap = 2;
  IndicatorRect rect;
  rect.width = width;
  rect.height = height;
  rect.x = indicator_beside != nullptr
               ? indicator_beside->x + indicator_beside->width + kGap
               : column_right + kGap;
  rect.y = column_top - height;
  return rect;
}

IndicatorRect LayoutIndicatorVertical(const Style& style, double label_width,
                                      double column_left, double column_right,
                                      double column_top, double screen_right) {
  IndicatorRect rect = Size(style, label_width);
  const double right = column_right + style.gap();
  rect.x = right + rect.width <= screen_right
               ? right
               : column_left - style.gap() - rect.width;
  rect.x += style.offset_x();
  rect.y = column_top - rect.height + style.offset_y();
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

namespace {

bool Inside(const IndicatorRect& rect, const IndicatorRect& area) {
  return rect.x >= area.x && rect.y >= area.y &&
         rect.x + rect.width <= area.x + area.width &&
         rect.y + rect.height <= area.y + area.height;
}

bool Overlaps(const IndicatorRect& a, const IndicatorRect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
         b.y < a.y + a.height;
}

}  // namespace

std::optional<IndicatorRect> PlaceIndicator(const Style& style,
                                            double label_width,
                                            double line_left,
                                            double line_bottom,
                                            double line_height,
                                            const IndicatorRect& screen,
                                            const IndicatorRect* obstacle) {
  const IndicatorRect size = Size(style, label_width);
  const double x = line_left + style.offset_x();
  const double above =
      line_bottom + line_height + style.gap() + style.offset_y();
  const double below =
      line_bottom - style.gap() - size.height + style.offset_y();
  struct Spot {
    double x;
    double y;
    bool keep_on_screen;  // Moved sideways into the screen.
  };
  std::vector<Spot> spots;
  switch (style.placement()) {
    case Style::SIDE:
      spots = {{line_left - style.gap() - size.width + style.offset_x(),
                line_bottom + (line_height - size.height) / 2 +
                    style.offset_y(),
                false},
               {x, above, true},
               {x, below, true}};
      break;
    case Style::BELOW:
      spots = {{x, below, true}, {x, above, true}};
      break;
    default:
      spots = {{x, above, true}, {x, below, true}};
      break;
  }
  const auto fits = [&](const IndicatorRect& rect) {
    return Inside(rect, screen) &&
           (obstacle == nullptr || !Overlaps(rect, *obstacle));
  };
  std::vector<IndicatorRect> tried;
  for (const Spot& spot : spots) {
    IndicatorRect rect = size;
    rect.x = spot.x;
    rect.y = spot.y;
    if (spot.keep_on_screen) {
      rect.x = std::clamp(
          rect.x, screen.x,
          std::max(screen.x, screen.x + screen.width - rect.width));
    }
    if (fits(rect)) {
      return rect;
    }
    tried.push_back(rect);
  }
  // In the suggestion window's way: beside it, at the heights tried.
  if (obstacle != nullptr) {
    for (IndicatorRect rect : tried) {
      for (const double beside : {obstacle->x + obstacle->width + style.gap(),
                                  obstacle->x - style.gap() - rect.width}) {
        rect.x = beside;
        if (fits(rect)) {
          return rect;
        }
      }
    }
  }
  return std::nullopt;
}

bool ChoosingCandidates(const commands::CandidateWindow& window) {
  return window.candidate_size() > 0 &&
         (window.category() != commands::SUGGESTION ||
          window.has_focused_index());
}

}  // namespace mozc::floatype
