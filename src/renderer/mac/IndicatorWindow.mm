#import "renderer/mac/IndicatorWindow.h"

#include <string>

#include "floatype/indicator/indicator_layout.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_command.pb.h"

namespace {

NSColor *ColorFromRGB(uint32_t rgb, CGFloat alpha) {
  return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xff) / 255.0
                             green:((rgb >> 8) & 0xff) / 255.0
                              blue:(rgb & 0xff) / 255.0
                             alpha:alpha];
}

}  // namespace

@interface FloatypeIndicatorView : NSView
@property(nonatomic) NSColor *fillColor;
@property(nonatomic, copy) NSAttributedString *label;
@property(nonatomic) CGFloat cornerRadius;
@end

@implementation FloatypeIndicatorView
- (void)drawRect:(NSRect)dirtyRect {
  [[NSColor clearColor] set];
  NSRectFill(self.bounds);
  [self.fillColor set];
  [[NSBezierPath bezierPathWithRoundedRect:self.bounds
                                   xRadius:self.cornerRadius
                                   yRadius:self.cornerRadius] fill];
  if (self.label.length > 0) {
    const NSSize size = self.label.size;
    [self.label drawAtPoint:NSMakePoint((NSWidth(self.bounds) - size.width) / 2,
                                        (NSHeight(self.bounds) - size.height) / 2)];
  }
}
@end

namespace mozc {
namespace renderer {
namespace mac {

using ::mozc::config::Config;

IndicatorWindow::IndicatorWindow() {
  window_ = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0, 0, 1, 1)
                styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                  backing:NSBackingStoreBuffered
                    defer:YES];
  [window_ setOpaque:NO];
  [window_ setBackgroundColor:[NSColor clearColor]];
  [window_ setHasShadow:YES];
  [window_ setIgnoresMouseEvents:YES];
  [window_ setLevel:NSPopUpMenuWindowLevel];
  view_ = [[FloatypeIndicatorView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
  [window_ setContentView:view_];
}

IndicatorWindow::~IndicatorWindow() { [window_ close]; }

void IndicatorWindow::Hide() { [window_ orderOut:nil]; }

void IndicatorWindow::Update(const commands::RendererCommand &command) {
  const commands::Output::Indicator &indicator = command.output().indicator();
  if (!command.visible() || indicator.state() == commands::Output::Indicator::NONE ||
      !command.has_preedit_rectangle()) {
    Hide();
    return;
  }
  const Config::IndicatorStyle &style = indicator.style();
  const std::string label = floatype::IndicatorLabel(indicator.state(), style.label());
  NSFont *font = [NSFont systemFontOfSize:style.font_size() weight:NSFontWeightSemibold];
  NSDictionary *attributes = @{
    NSFontAttributeName : font,
    NSForegroundColorAttributeName : ColorFromRGB(style.text_color(), 1.0),
  };
  view_.label = [[NSAttributedString alloc] initWithString:@(label.c_str()) attributes:attributes];
  view_.fillColor = ColorFromRGB(floatype::IndicatorColor(indicator.state(), style),
                                 style.opacity_percent() / 100.0);

  // The preedit rectangle holds Cocoa screen coordinates of the composition's
  // line: its bottom is the baseline, its top is the bottom minus the height.
  const commands::RendererCommand::Rectangle &line = command.preedit_rectangle();
  const NSPoint origin = NSMakePoint(line.left(), line.bottom());
  NSScreen *screen = [NSScreen mainScreen];
  for (NSScreen *candidate in [NSScreen screens]) {
    if (NSPointInRect(origin, candidate.frame)) {
      screen = candidate;
      break;
    }
  }
  const floatype::IndicatorRect rect =
      floatype::LayoutIndicator(style, view_.label.size.width, line.left(), line.bottom(),
                                line.bottom() - line.top(), NSMaxY(screen.visibleFrame));
  view_.cornerRadius = style.shape() == Config::IndicatorStyle::ROUNDED_RECT
                           ? std::min<double>(style.corner_radius(), rect.height / 2)
                           : rect.height / 2;
  [window_ setFrame:NSMakeRect(rect.x, rect.y, rect.width, rect.height) display:NO];
  [view_ setNeedsDisplay:YES];
  [window_ orderFront:nil];
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
