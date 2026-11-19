// floatype: the preview in the settings screen's 外観 tab. It draws the
// candidate windows from floatype/look, as the renderer does, and the reading
// and indicator pills as renderer/mac/IndicatorWindow.mm does.

#ifndef MOZC_GUI_CONFIG_DIALOG_LOOK_PREVIEW_H_
#define MOZC_GUI_CONFIG_DIALOG_LOOK_PREVIEW_H_

#include <QPainter>
#include <QRectF>
#include <QWidget>
#include <functional>
#include <utility>

#include "floatype/look/window_look.h"
#include "protocol/config.pb.h"

namespace mozc::gui {

// A widget that draws with `paint`, over the whole of it.
class LookPreview : public QWidget {
 public:
  explicit LookPreview(std::function<void(QPainter&, const QRectF&)> paint)
      : paint_(std::move(paint)) {}

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  std::function<void(QPainter&, const QRectF&)> paint_;
};

// What the windows sit on: a light or a dark desktop.
void PaintBackdrop(QPainter& painter, const QRectF& area, bool dark);

// Three candidates, the first focused, centered in `area`. The conversion
// window has the numbers column and "1 / 3" in its footer; the suggestion
// window neither, and 予測候補 in its footer.
void PaintCandidateWindow(QPainter& painter, const QRectF& area,
                          const floatype::WindowLook& look, bool suggestion,
                          bool kind_mark = false);

// The reading over an underlined live conversion.
void PaintRuby(QPainter& painter, const QRectF& area,
               const config::Config::RubyStyle& style, bool dark);

// The indicator in each state, and once more with the mixed English mode.
void PaintIndicators(QPainter& painter, const QRectF& area,
                     const config::Config::IndicatorStyle& style);

}  // namespace mozc::gui

#endif  // MOZC_GUI_CONFIG_DIALOG_LOOK_PREVIEW_H_
