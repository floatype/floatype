// floatype: the indicator next to the composition that tells whose result is
// on screen. Its layout lives in floatype/indicator/indicator_layout.h.

#ifndef MOZC_RENDERER_MAC_INDICATOR_WINDOW_H_
#define MOZC_RENDERER_MAC_INDICATOR_WINDOW_H_

#import <Cocoa/Cocoa.h>

#include "protocol/renderer_command.pb.h"

@class FloatypeIndicatorView;

namespace mozc {
namespace renderer {
namespace mac {

class IndicatorWindow {
 public:
  IndicatorWindow();
  IndicatorWindow(const IndicatorWindow &) = delete;
  IndicatorWindow &operator=(const IndicatorWindow &) = delete;
  ~IndicatorWindow();

  // Shows the indicator of the command's output next to its composition, or
  // hides it when there is none.
  // `base_screen_height` is the height of the screen at the origin, which the
  // preedit rectangle counts down from.
  void Update(const commands::RendererCommand &command, double base_screen_height);
  void Hide();
  // The drawn indicator and reading, for tests to look at.
  NSView *ViewForTesting() const;
  NSView *RubyViewForTesting() const;

 private:
  // Shows the indicator; returns whether it did, and where in `frame`.
  bool UpdateIndicator(const commands::RendererCommand &command, double base_screen_height,
                       NSRect *frame);
  // Shows the reading above a live conversion, above `indicator` if given.
  void UpdateRuby(const commands::RendererCommand &command, double base_screen_height,
                  const NSRect *indicator);

  NSPanel *window_;
  FloatypeIndicatorView *view_;
  // floatype: the reading above a live conversion.
  NSPanel *ruby_window_;
  FloatypeIndicatorView *ruby_view_;
};

}  // namespace mac
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_MAC_INDICATOR_WINDOW_H_
