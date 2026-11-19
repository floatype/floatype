// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <Carbon/Carbon.h>
#include <objc/message.h>

#import "CandidateView.h"

#include "absl/log/log.h"
#include "base/coordinates.h"
#include "floatype/look/window_look.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "renderer/mac/CandidateWindow.h"

// floatype: the strong shadow, drawn outside a rounded rectangle inset by
// `inset` from the bounds.
@interface FloatypeShadowView : NSView
@property(nonatomic) CGFloat inset;
@property(nonatomic) CGFloat cornerRadius;
@property(nonatomic) CGFloat blurRadius;
@property(nonatomic) CGFloat offsetY;
@property(nonatomic) NSColor* color;
@end

@implementation FloatypeShadowView
- (void)drawRect:(NSRect)dirtyRect {
  [[NSColor clearColor] set];
  NSRectFill(self.bounds);
  NSBezierPath* shape =
      [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, self.inset, self.inset)
                                      xRadius:self.cornerRadius
                                      yRadius:self.cornerRadius];
  // Only outside the window, which may be see-through.
  NSBezierPath* outside = [NSBezierPath bezierPathWithRect:self.bounds];
  [outside appendBezierPath:shape];
  outside.windingRule = NSWindingRuleEvenOdd;
  [outside addClip];
  NSShadow* shadow = [[NSShadow alloc] init];
  shadow.shadowBlurRadius = self.blurRadius;
  shadow.shadowOffset = NSMakeSize(0, -self.offsetY);
  shadow.shadowColor = self.color;
  [shadow set];
  [[NSColor blackColor] set];
  [shape fill];
}
@end

namespace mozc {
namespace renderer {
namespace mac {

CandidateWindow::CandidateWindow() : command_sender_(nullptr) {}

CandidateWindow::~CandidateWindow() { [shadow_window_ close]; }

void CandidateWindow::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  DLOG(INFO) << "CandidateWindow::SetSendCommandInterface()";
  command_sender_ = send_command_interface;

  const CandidateView* candidate_view = (CandidateView*)view_;
  [candidate_view setSendCommandInterface:send_command_interface];
}

void CandidateWindow::InitWindow() {
  RendererBaseWindow::InitWindow();
  const CandidateView* candidate_view = (CandidateView*)view_;
  [candidate_view setSendCommandInterface:command_sender_];
}
const mozc::renderer::TableLayout* CandidateWindow::GetTableLayout() const {
  const CandidateView* candidate_view = (CandidateView*)view_;
  return [candidate_view tableLayout];
}

void CandidateWindow::SetCandidateWindow(const commands::CandidateWindow& candidate_window) {
  DLOG(INFO) << "CandidateWindow::SetCandidateWindow";
  if (candidate_window.candidate_size() == 0) {
    return;
  }

  if (!window_) {
    InitWindow();
  }
  CandidateView* candidate_view = (CandidateView*)view_;
  const bool suggest = candidate_window.category() == commands::SUGGESTION ||
                       candidate_window.category() == commands::PREDICTION;
  [candidate_view setLookStyle:suggest ? suggest_style_ : conversion_style_];
  [candidate_view setKindMark:kind_mark_];
  [candidate_view setCandidateWindow:&candidate_window];
  [candidate_view setNeedsDisplay:YES];
  NSSize size = [candidate_view updateLayout];
  ResizeWindow(size.width, size.height);
  ApplyWindowLook();
}

void CandidateWindow::ApplyWindowLook() {
  const floatype::WindowLook& look = [(CandidateView*)view_ look];
  // Square corners keep Mozc's opaque window.
  const bool opaque = look.corner_radius <= 0;
  [window_ setOpaque:opaque];
  [window_ setBackgroundColor:opaque ? NSColor.whiteColor : NSColor.clearColor];
  [window_ setAlphaValue:look.opacity];
  // The look's shadow is drawn behind; the system's would add a second one.
  [window_ setHasShadow:NO];
  PlaceShadow();
}

void CandidateWindow::PlaceShadow() {
  const floatype::WindowLook& look = [(CandidateView*)view_ look];
  if (look.shadow == config::Config::CandidateStyle::NONE || !IsVisible()) {
    [shadow_window_ orderOut:nil];
    return;
  }
  if (shadow_window_ == nil) {
    shadow_window_ = [[NSPanel alloc]
        initWithContentRect:NSMakeRect(0, 0, 1, 1)
                  styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                    backing:NSBackingStoreBuffered
                      defer:YES];
    [shadow_window_ setOpaque:NO];
    [shadow_window_ setBackgroundColor:NSColor.clearColor];
    [shadow_window_ setHasShadow:NO];
    [shadow_window_ setIgnoresMouseEvents:YES];
    [shadow_window_ setReleasedWhenClosed:NO];
    [shadow_window_ setContentView:[[FloatypeShadowView alloc] initWithFrame:NSZeroRect]];
  }
  FloatypeShadowView* shadow = (FloatypeShadowView*)shadow_window_.contentView;
  const CGFloat inset = look.shadow_radius + look.shadow_offset_y;
  NSColor* color = [NSColor colorWithSRGBRed:((look.shadow_color >> 16) & 0xff) / 255.0
                                       green:((look.shadow_color >> 8) & 0xff) / 255.0
                                        blue:(look.shadow_color & 0xff) / 255.0
                                       alpha:look.shadow_opacity];
  const NSRect frame = NSInsetRect(window_.frame, -inset, -inset);
  // The blur is costly to draw; a window that only moves keeps it.
  const bool changed = shadow.inset != inset || shadow.cornerRadius != look.corner_radius ||
                       shadow.blurRadius != look.shadow_radius ||
                       shadow.offsetY != look.shadow_offset_y || ![shadow.color isEqual:color] ||
                       !NSEqualSizes(shadow_window_.frame.size, frame.size);
  shadow.inset = inset;
  shadow.cornerRadius = look.corner_radius;
  shadow.blurRadius = look.shadow_radius;
  shadow.offsetY = look.shadow_offset_y;
  shadow.color = color;
  [shadow_window_ setAlphaValue:look.opacity];
  [shadow_window_ setLevel:window_.level];
  [shadow_window_ setFrame:frame display:NO];
  if (changed) {
    [shadow setNeedsDisplay:YES];
  }
  [shadow_window_ orderWindow:NSWindowBelow relativeTo:window_.windowNumber];
}

void CandidateWindow::Show() {
  RendererBaseWindow::Show();
  PlaceShadow();
}

void CandidateWindow::Hide() {
  RendererBaseWindow::Hide();
  [shadow_window_ orderOut:nil];
}

void CandidateWindow::MoveWindow(const NSPoint& point) {
  RendererBaseWindow::MoveWindow(point);
  PlaceShadow();
}

void CandidateWindow::SetWindowLevel(NSInteger window_level) {
  RendererBaseWindow::SetWindowLevel(window_level);
  [shadow_window_ setLevel:window_level];
}

void CandidateWindow::ResetView() {
  DLOG(INFO) << "CandidateWindow::ResetView()";
  view_ = [[CandidateView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
