#include "gui/config_dialog/look_preview.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QPainterPath>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "floatype/indicator/indicator_layout.h"
#include "floatype/look/window_look.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_style.pb.h"

namespace mozc::gui {
namespace {

using ::mozc::renderer::RendererStyle;
using State = ::mozc::commands::Output::Indicator::State;
using IndicatorStyle = ::mozc::config::Config::IndicatorStyle;

QColor Rgb(uint32_t rgb, double alpha = 1) {
  QColor color((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
  color.setAlphaF(alpha);
  return color;
}

QFont Font(const RendererStyle::TextStyle& style) {
  QFont font = style.font_name().empty()
                   ? QApplication::font()
                   : QFont(QString::fromStdString(style.font_name()));
  font.setPixelSize(std::max<int>(1, std::lround(style.font_size())));
  return font;
}

QFont SystemFont(uint32_t size, QFont::Weight weight = QFont::Normal) {
  QFont font = QApplication::font();
  font.setPixelSize(std::max<int>(1, size));
  font.setWeight(weight);
  return font;
}

// The window shadow: rings that fade out, under the window's shape.
void PaintShadow(QPainter& painter, const QRectF& window, double corner,
                 const floatype::WindowLook& look) {
  const int radius = std::lround(look.shadow_radius);
  if (radius <= 0 || look.shadow_opacity <= 0) {
    return;
  }
  painter.save();
  painter.setPen(Qt::NoPen);
  for (int i = radius; i >= 1; --i) {
    const double fade = 1.0 - static_cast<double>(i) / (radius + 1);
    painter.setBrush(
        Rgb(look.shadow_color, look.shadow_opacity * fade * 2.0 / radius));
    const QRectF ring =
        window.adjusted(-i, -i, i, i).translated(0, look.shadow_offset_y);
    painter.drawRoundedRect(ring, corner + i, corner + i);
  }
  painter.restore();
}

// A pill as IndicatorWindow.mm draws one: lighter at the top.
void PaintPill(QPainter& painter, const QRectF& rect, double corner,
               uint32_t fill, double opacity) {
  const QColor bottom = Rgb(fill, opacity);
  QColor top = bottom;
  top.setRgbF(top.redF() + (1 - top.redF()) * 0.11,
              top.greenF() + (1 - top.greenF()) * 0.11,
              top.blueF() + (1 - top.blueF()) * 0.11, opacity);
  // The panel's system shadow, faintly.
  painter.setPen(Qt::NoPen);
  for (int i = 3; i >= 1; --i) {
    painter.setBrush(QColor(0, 0, 0, 14));
    painter.drawRoundedRect(rect.adjusted(-i, -i + 1, i, i + 1), corner + i,
                            corner + i);
  }
  QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
  gradient.setColorAt(0, top);
  gradient.setColorAt(1, bottom);
  painter.setPen(Qt::NoPen);
  painter.setBrush(gradient);
  painter.drawRoundedRect(rect, corner, corner);
}

}  // namespace

void LookPreview::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  paint_(painter, QRectF(rect()));
}

void PaintBackdrop(QPainter& painter, const QRectF& area, bool dark) {
  painter.setPen(dark ? QColor(0x48, 0x48, 0x4a) : QColor(0xd0, 0xd0, 0xd4));
  painter.setBrush(dark ? QColor(0x1e, 0x1e, 0x20) : QColor(0xf5, 0xf5, 0xf7));
  painter.drawRoundedRect(area.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

void PaintCandidateWindow(QPainter& painter, const QRectF& area,
                          const floatype::WindowLook& look, bool suggestion) {
  const RendererStyle style = floatype::RendererStyleFor(look);
  const QString candidates[] = {QString::fromUtf8("今日はいい天気です"),
                                QString::fromUtf8("今日もいい天気です"),
                                QString::fromUtf8("キョウハイイテンキ")};
  const QString descriptions[] = {QString(), QString(),
                                  QString::fromUtf8("カタカナ")};
  const QFont candidate_font = Font(style.candidate_style());
  const QFont number_font = Font(style.shortcut_style());
  const QFont description_font = Font(style.description_style());
  const QFont footer_font = Font(style.footer_style());
  const QFontMetricsF candidate_metrics(candidate_font);
  const QFontMetricsF number_metrics(number_font);
  const QFontMetricsF description_metrics(description_font);
  const QFontMetricsF footer_metrics(footer_font);

  // Sizes as CandidateView.mm lays the rows out.
  const double padding = look.window_padding;
  const double row_height = look.row_height;
  const double number_width =
      suggestion
          ? 0
          : look.row_padding +
                std::max(look.number_width,
                         number_metrics.horizontalAdvance(QStringLiteral("9")));
  const double gap = look.row_padding;
  // The candidates end in two spaces, as the renderer appends them.
  const QString spaces = QStringLiteral("  ");
  double text_width = candidate_metrics.horizontalAdvance(
      QString::fromStdString(style.column_minimum_width_string()) + spaces);
  for (const QString& candidate : candidates) {
    text_width = std::max(
        text_width, candidate_metrics.horizontalAdvance(candidate + spaces));
  }
  const double description_width =
      description_metrics.horizontalAdvance(descriptions[2]) + look.row_padding;
  const double row_width = number_width + gap + text_width + description_width;
  const QString footer_text =
      suggestion ? QString::fromUtf8("Tabキーで選択") : QStringLiteral("1 / 3");
  const double footer_height = footer_metrics.height() +
                               look.footer_top_padding +
                               look.footer_bottom_padding;
  const double width = padding * 2 + row_width;
  const double height = padding * 2 + row_height * 3 + footer_height;
  const QRectF window(area.center().x() - width / 2,
                      area.center().y() - height / 2, width, height);
  const double corner = std::min(look.corner_radius, height / 2);

  PaintShadow(painter, window, corner, look);
  // Drawn whole first, then once with the opacity, as a window is: drawn
  // directly, the fills over fills would come out more opaque than the rest.
  const double ratio = painter.device()->devicePixelRatioF();
  QImage image((window.size() * ratio).toSize(),
               QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(ratio);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);
  p.translate(-window.topLeft());
  QPainterPath shape;
  shape.addRoundedRect(window, corner, corner);
  p.setClipPath(shape);
  p.fillRect(window, Rgb(look.background));
  const QRectF inner = window.adjusted(padding, padding, -padding, -padding);
  for (int row = 0; row < 3; ++row) {
    const QRectF cells(inner.left(), inner.top() + row * row_height, row_width,
                       row_height);
    const bool focused = row == 0;
    if (focused) {
      p.setPen(look.focused_border_alpha > 0
                   ? QPen(Rgb(look.focused_border, look.focused_border_alpha))
                   : QPen(Qt::NoPen));
      p.setBrush(Rgb(look.focused_background));
      p.drawRoundedRect(cells.adjusted(0.5, 0.5, -0.5, -0.5),
                        look.row_corner_radius, look.row_corner_radius);
    }
    const QColor secondary =
        Rgb(focused ? look.focused_secondary_text : look.secondary_text);
    if (!suggestion) {
      p.setFont(number_font);
      p.setPen(secondary);
      p.drawText(QRectF(cells.left() + look.row_padding, cells.top(),
                        number_width - look.row_padding, row_height),
                 Qt::AlignLeft | Qt::AlignVCenter, QString::number(row + 1));
    }
    p.setFont(candidate_font);
    p.setPen(Rgb(focused ? look.focused_text : look.text));
    p.drawText(QRectF(cells.left() + number_width + gap, cells.top(),
                      text_width, row_height),
               Qt::AlignLeft | Qt::AlignVCenter, candidates[row]);
    if (!descriptions[row].isEmpty()) {
      p.setFont(description_font);
      p.setPen(secondary);
      p.drawText(cells.adjusted(0, 0, -look.row_padding, 0),
                 Qt::AlignRight | Qt::AlignVCenter, descriptions[row]);
    }
  }
  const QRectF footer(inner.left(), inner.top() + row_height * 3, inner.width(),
                      footer_height);
  p.setFont(footer_font);
  p.setPen(Rgb(look.secondary_text));
  p.drawText(footer.adjusted(0, look.footer_top_padding, -look.row_padding,
                             -look.footer_bottom_padding),
             Qt::AlignRight | Qt::AlignTop, footer_text);
  p.setClipping(false);
  p.setPen(QPen(Rgb(look.border, look.border_alpha), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(window.adjusted(0.5, 0.5, -0.5, -0.5),
                    std::max(0.0, corner - 0.5), std::max(0.0, corner - 0.5));
  p.end();
  painter.save();
  painter.setOpacity(look.opacity);
  painter.drawImage(window.topLeft(), image);
  painter.restore();
}

void PaintRuby(QPainter& painter, const QRectF& area,
               const config::Config::RubyStyle& style, bool dark) {
  QFont line_font = SystemFont(22);
  line_font.setUnderline(true);
  const QFontMetricsF line_metrics(line_font);
  const QString line = QString::fromUtf8("今日は");
  const QFont ruby_font = SystemFont(style.font_size());
  const QFontMetricsF ruby_metrics(ruby_font);
  const QString reading = QString::fromUtf8("きょうは");
  // As IndicatorWindow::UpdateRuby sizes it and LayoutRuby places it.
  const double width = ruby_metrics.horizontalAdvance(reading) + 8;
  const double height = ruby_metrics.height() + 2;
  const QRectF line_rect(area.center().x() - 60,
                         area.center().y() - line_metrics.height() / 2 + 20,
                         line_metrics.horizontalAdvance(line),
                         line_metrics.height());
  const QRectF ruby(line_rect.left(), line_rect.top() - 2 - height, width,
                    height);
  PaintPill(painter, ruby, std::min<double>(style.corner_radius(), height / 2),
            style.background_color(), style.opacity_percent() / 100.0);
  painter.setFont(ruby_font);
  painter.setPen(Rgb(style.text_color()));
  painter.drawText(ruby, Qt::AlignCenter, reading);
  painter.setFont(line_font);
  painter.setPen(dark ? QColor(0xe8, 0xe8, 0xea) : QColor(0x1d, 0x1d, 0x1f));
  painter.drawText(line_rect, Qt::AlignLeft | Qt::AlignVCenter, line);
}

void PaintIndicators(QPainter& painter, const QRectF& area,
                     const IndicatorStyle& style) {
  // IndicatorWindow.mm's measures.
  constexpr double kDotSize = 5;
  constexpr double kDotLeft = 9;
  constexpr double kDotRoom = 12;
  constexpr double kDividerGap = 6;
  constexpr double kEndPadding = 8;
  struct Pill {
    State state;
    bool english;
  };
  const Pill pills[] = {{commands::Output::Indicator::READING, false},
                        {commands::Output::Indicator::MOZC, false},
                        {commands::Output::Indicator::ZENZ_PENDING, false},
                        {commands::Output::Indicator::ZENZ, false},
                        {commands::Output::Indicator::READING, true}};
  const QFont font = SystemFont(style.font_size(), QFont::Medium);
  const QFontMetricsF metrics(font);
  const double height = style.height();
  const double step = height + 12;
  double y = area.center().y() - (step * 5 - 12) / 2;
  for (const Pill& pill : pills) {
    const QString label = QString::fromStdString(
        floatype::IndicatorLabel(pill.state, style.label()));
    const QString mode =
        pill.english && !label.isEmpty() ? QStringLiteral("A") : QString();
    const bool dot = style.label() == IndicatorStyle::FULL_LABEL &&
                     style.shape() != IndicatorStyle::CIRCLE &&
                     label.size() > 1;
    const double mode_room =
        mode.isEmpty() ? 0
                       : kDividerGap * 2 + 1 + metrics.horizontalAdvance(mode);
    const double label_width =
        metrics.horizontalAdvance(label) + (dot ? kDotRoom : 0) + mode_room;
    // As LayoutIndicator sizes it.
    const double width =
        style.shape() == IndicatorStyle::CIRCLE
            ? height
            : std::max<double>(style.width(), label_width + 2 * 8);
    const QRectF rect(area.center().x() - width / 2, y, width, height);
    const double corner =
        style.shape() == IndicatorStyle::ROUNDED_RECT
            ? std::min<double>(style.corner_radius(), height / 2)
            : height / 2;
    PaintPill(painter, rect, corner,
              floatype::IndicatorColor(pill.state, style),
              style.opacity_percent() / 100.0);
    const QColor text = Rgb(style.text_color());
    const double label_right = rect.right() - mode_room;
    if (dot) {
      QColor dot_color = text;
      dot_color.setAlphaF(0.85);
      painter.setBrush(dot_color);
      painter.drawEllipse(QRectF(rect.left() + kDotLeft,
                                 rect.center().y() - kDotSize / 2, kDotSize,
                                 kDotSize));
    }
    painter.setFont(font);
    painter.setPen(text);
    const double room = dot ? kDotRoom : 0;
    painter.drawText(QRectF(rect.left() + room, rect.top(),
                            label_right - rect.left() - room, height),
                     Qt::AlignCenter, label);
    if (!mode.isEmpty()) {
      QColor divider = text;
      divider.setAlphaF(0.45);
      const double mode_left =
          rect.right() - kEndPadding - metrics.horizontalAdvance(mode);
      painter.fillRect(QRectF(mode_left - kDividerGap - 1,
                              rect.top() + height * 0.22, 1, height * 0.56),
                       divider);
      painter.drawText(QRectF(mode_left, rect.top(),
                              metrics.horizontalAdvance(mode), height),
                       Qt::AlignCenter, mode);
    }
    y += step;
  }
}

}  // namespace mozc::gui
