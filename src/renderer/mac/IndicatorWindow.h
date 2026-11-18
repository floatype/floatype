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
  void Update(const commands::RendererCommand &command);
  void Hide();

 private:
  NSPanel *window_;
  FloatypeIndicatorView *view_;
};

}  // namespace mac
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_MAC_INDICATOR_WINDOW_H_
