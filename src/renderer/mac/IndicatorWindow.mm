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

namespace {

NSPanel *MakePanel(NSView *content) {
  NSPanel *panel = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0, 0, 1, 1)
                styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                  backing:NSBackingStoreBuffered
                    defer:YES];
  [panel setOpaque:NO];
  [panel setBackgroundColor:[NSColor clearColor]];
  [panel setHasShadow:YES];
  [panel setIgnoresMouseEvents:YES];
  [panel setLevel:NSPopUpMenuWindowLevel];
  [panel setContentView:content];
  return panel;
}

}  // namespace

IndicatorWindow::IndicatorWindow() {
  view_ = [[FloatypeIndicatorView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
  window_ = MakePanel(view_);
  ruby_view_ = [[FloatypeIndicatorView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
  ruby_window_ = MakePanel(ruby_view_);
}

IndicatorWindow::~IndicatorWindow() {
  [window_ close];
  [ruby_window_ close];
}

void IndicatorWindow::Hide() {
  [window_ orderOut:nil];
  [ruby_window_ orderOut:nil];
}

NSView *IndicatorWindow::ViewForTesting() const { return view_; }

NSView *IndicatorWindow::RubyViewForTesting() const { return ruby_view_; }

void IndicatorWindow::Update(const commands::RendererCommand &command, double base_screen_height) {
  NSRect frame;
  const bool shown = UpdateIndicator(command, base_screen_height, &frame);
  UpdateRuby(command, base_screen_height, shown ? &frame : nullptr);
}

void IndicatorWindow::UpdateRuby(const commands::RendererCommand &command,
                                 double base_screen_height, const NSRect *indicator) {
  const commands::RendererCommand::Rectangle &line = command.composition_rectangle();
  const double line_width = line.right() - line.left();
  const double line_height = line.bottom() - line.top();
  const std::string &reading = command.output().live_reading();
  // Horizontal writing with a known caret only.
  if (!command.visible() || reading.empty() || !command.has_composition_rectangle() ||
      line_height < line_width) {
    [ruby_window_ orderOut:nil];
    return;
  }
  NSDictionary *attributes = @{
    NSFontAttributeName : [NSFont systemFontOfSize:11],
    NSForegroundColorAttributeName : [NSColor colorWithWhite:0.25 alpha:1.0],
  };
  ruby_view_.label = [[NSAttributedString alloc] initWithString:@(reading.c_str())
                                                     attributes:attributes];
  ruby_view_.fillColor = [NSColor colorWithWhite:1.0 alpha:0.92];
  const NSSize size = ruby_view_.label.size;
  const double width = size.width + 8;
  const double height = size.height + 2;
  ruby_view_.cornerRadius = 4;
  const double baseline = base_screen_height - line.bottom();
  // Only an indicator above the line pushes the reading up.
  const bool above = indicator != nullptr && NSMinY(*indicator) >= baseline + line_height;
  const floatype::IndicatorRect indicator_rect =
      above ? floatype::IndicatorRect{NSMinX(*indicator), NSMinY(*indicator),
                                      NSWidth(*indicator), NSHeight(*indicator)}
            : floatype::IndicatorRect();
  const floatype::IndicatorRect rect = floatype::LayoutRuby(
      width, height, line.left(), baseline + line_height, above ? &indicator_rect : nullptr);
  [ruby_window_ setFrame:NSMakeRect(rect.x, rect.y, rect.width, rect.height) display:NO];
  [ruby_view_ setNeedsDisplay:YES];
  [ruby_window_ orderFront:nil];
}

bool IndicatorWindow::UpdateIndicator(const commands::RendererCommand &command,
                                      double base_screen_height, NSRect *frame) {
  const commands::Output::Indicator &indicator = command.output().indicator();
  if (!command.visible() || indicator.state() == commands::Output::Indicator::NONE) {
    [window_ orderOut:nil];
    return false;
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

  // The preedit rectangle counts y down from the top of the base screen, as
  // CandidateController::AlignWindows reads it; Cocoa counts y up.
  const commands::RendererCommand::Rectangle &line = command.composition_rectangle();
  const bool has_caret = command.has_composition_rectangle() &&
                         (line.left() != 0 || line.top() != 0 || line.bottom() != 0);
  const NSPoint origin = NSMakePoint(line.left(), base_screen_height - line.bottom());
  NSScreen *screen = [NSScreen mainScreen];
  for (NSScreen *candidate in [NSScreen screens]) {
    if (has_caret && NSPointInRect(origin, candidate.frame)) {
      screen = candidate;
      break;
    }
  }
  const NSRect visible = screen.visibleFrame;
  // Mozc's own test for vertical writing (CandidateController::AlignWindows):
  // a line wider than it is tall.
  const double line_width = line.right() - line.left();
  const double line_height = line.bottom() - line.top();
  floatype::IndicatorRect rect;
  if (!has_caret) {
    rect = floatype::LayoutIndicatorWithoutCaret(
        style, view_.label.size.width,
        {NSMinX(visible), NSMinY(visible), NSWidth(visible), NSHeight(visible)});
  } else if (line_height < line_width) {
    rect = floatype::LayoutIndicatorVertical(style, view_.label.size.width, origin.x,
                                             origin.x + line_width, origin.y + line_height,
                                             NSMaxX(visible));
  } else {
    rect = floatype::LayoutIndicator(style, view_.label.size.width, origin.x, origin.y,
                                     line_height, NSMaxY(visible));
  }
  view_.cornerRadius = style.shape() == Config::IndicatorStyle::ROUNDED_RECT
                           ? std::min<double>(style.corner_radius(), rect.height / 2)
                           : rect.height / 2;
  *frame = NSMakeRect(rect.x, rect.y, rect.width, rect.height);
  [window_ setFrame:*frame display:NO];
  [view_ setNeedsDisplay:YES];
  [window_ orderFront:nil];
  return true;
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
