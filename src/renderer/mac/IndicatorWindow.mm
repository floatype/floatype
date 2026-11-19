#import "renderer/mac/IndicatorWindow.h"

#include <algorithm>
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
// A dot left of the label, as Mozkey draws it, for labels of words.
@property(nonatomic) BOOL showsDot;
// Mixed English input: あ or A at the right end, after a thin divider; nil
// for none.
@property(nonatomic, copy) NSAttributedString *mode;
@end

namespace {
// The dot and the room it takes left of the label, as in Mozkey.
constexpr CGFloat kDotSize = 5;
constexpr CGFloat kDotLeft = 9;
constexpr CGFloat kDotRoom = 12;
// Space on each side of the divider before the mixed English mode, and the
// padding the layout leaves at each end (floatype/indicator/indicator_layout).
constexpr CGFloat kDividerGap = 6;
constexpr CGFloat kEndPadding = 8;

// The width the mixed English mode takes at the right end, divider included.
CGFloat ModeRoom(NSAttributedString *mode) {
  return mode.length > 0 ? kDividerGap * 2 + 1 + mode.size.width : 0;
}
}  // namespace

@implementation FloatypeIndicatorView
- (void)drawRect:(NSRect)dirtyRect {
  [[NSColor clearColor] set];
  NSRectFill(self.bounds);
  // Lighter at the top, as Mozkey's.
  NSColor *top = [self.fillColor blendedColorWithFraction:0.11 ofColor:NSColor.whiteColor];
  NSGradient *gradient = [[NSGradient alloc] initWithStartingColor:top
                                                       endingColor:self.fillColor];
  [gradient drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:self.bounds
                                                             xRadius:self.cornerRadius
                                                             yRadius:self.cornerRadius]
                       angle:270];
  const CGFloat mode_room = ModeRoom(self.mode);
  const CGFloat label_right = NSWidth(self.bounds) - mode_room;
  if (self.label.length > 0) {
    const NSSize size = self.label.size;
    const CGFloat room = self.showsDot ? kDotRoom : 0;
    if (self.showsDot) {
      NSColor *text = [self.label attribute:NSForegroundColorAttributeName
                                    atIndex:0
                             effectiveRange:nullptr];
      [[text colorWithAlphaComponent:0.85] set];
      [[NSBezierPath
          bezierPathWithOvalInRect:NSMakeRect(kDotLeft, (NSHeight(self.bounds) - kDotSize) / 2,
                                              kDotSize, kDotSize)] fill];
    }
    [self.label drawAtPoint:NSMakePoint(room + (label_right - room - size.width) / 2,
                                        (NSHeight(self.bounds) - size.height) / 2)];
  }
  if (mode_room > 0) {
    NSColor *text = [self.mode attribute:NSForegroundColorAttributeName
                                 atIndex:0
                          effectiveRange:nullptr];
    [[text colorWithAlphaComponent:0.45] set];
    const CGFloat height = NSHeight(self.bounds);
    const NSSize size = self.mode.size;
    const CGFloat mode_left = NSWidth(self.bounds) - kEndPadding - size.width;
    NSRectFill(NSMakeRect(mode_left - kDividerGap - 1, height * 0.22, 1, height * 0.56));
    [self.mode drawAtPoint:NSMakePoint(mode_left, (height - size.height) / 2)];
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
  const std::string &reading = command.output().live_reading();
  if (!command.visible() || reading.empty() || !command.has_composition_rectangle()) {
    [ruby_window_ orderOut:nil];
    return;
  }
  const Config::RubyStyle &style = command.output().ruby_style();
  NSDictionary *attributes = @{
    NSFontAttributeName : [NSFont systemFontOfSize:style.font_size()],
    NSForegroundColorAttributeName : ColorFromRGB(style.text_color(), 1.0),
  };
  ruby_view_.label = [[NSAttributedString alloc] initWithString:@(reading.c_str())
                                                     attributes:attributes];
  ruby_view_.fillColor = ColorFromRGB(style.background_color(), style.opacity_percent() / 100.0);
  const NSSize size = ruby_view_.label.size;
  const double width = size.width + 8;
  const double height = size.height + 2;
  ruby_view_.cornerRadius = std::min<double>(style.corner_radius(), height / 2);
  const auto to_rect = [](const NSRect &frame) {
    return floatype::IndicatorRect{NSMinX(frame), NSMinY(frame), NSWidth(frame),
                                   NSHeight(frame)};
  };
  const double line_width = line.right() - line.left();
  const double line_height = line.bottom() - line.top();
  floatype::IndicatorRect rect;
  // Vertical writing, by the indicator's test: a line wider than it is tall.
  if (line_height < line_width) {
    // Beside the column, past the indicator if it is there.
    const bool beside = indicator != nullptr && NSMinX(*indicator) >= line.right();
    const floatype::IndicatorRect indicator_rect =
        beside ? to_rect(*indicator) : floatype::IndicatorRect();
    rect = floatype::LayoutRubyVertical(width, height, line.right(),
                                        base_screen_height - line.top(),
                                        beside ? &indicator_rect : nullptr);
  } else {
    const double baseline = base_screen_height - line.bottom();
    // Only an indicator above the line pushes the reading up.
    const bool above = indicator != nullptr && NSMinY(*indicator) >= baseline + line_height;
    const floatype::IndicatorRect indicator_rect =
        above ? to_rect(*indicator) : floatype::IndicatorRect();
    rect = floatype::LayoutRuby(width, height, line.left(), baseline + line_height,
                                above ? &indicator_rect : nullptr);
  }
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
  std::string label = floatype::IndicatorLabel(indicator.state(), style.label());
  NSFont *font = [NSFont systemFontOfSize:style.font_size() weight:NSFontWeightMedium];
  NSDictionary *attributes = @{
    NSFontAttributeName : font,
    NSForegroundColorAttributeName : ColorFromRGB(style.text_color(), 1.0),
  };
  view_.label = [[NSAttributedString alloc] initWithString:@(label.c_str()) attributes:attributes];
  // Mixed English input: the word before the caret, Japanese or English.
  view_.mode = indicator.has_english() && !label.empty()
                   ? [[NSAttributedString alloc] initWithString:indicator.english() ? @"A" : @"あ"
                                                     attributes:attributes]
                   : nil;
  view_.showsDot = style.label() == Config::IndicatorStyle::FULL_LABEL &&
                   style.shape() != Config::IndicatorStyle::CIRCLE && view_.label.length > 1;
  const double label_width =
      view_.label.size.width + (view_.showsDot ? kDotRoom : 0) + ModeRoom(view_.mode);
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
        style, label_width,
        {NSMinX(visible), NSMinY(visible), NSWidth(visible), NSHeight(visible)});
  } else if (line_height < line_width) {
    rect = floatype::LayoutIndicatorVertical(style, label_width, origin.x,
                                             origin.x + line_width, origin.y + line_height,
                                             NSMaxX(visible));
  } else {
    rect = floatype::LayoutIndicator(style, label_width, origin.x, origin.y,
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
