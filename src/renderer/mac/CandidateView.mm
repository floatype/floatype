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

#import "renderer/mac/CandidateView.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "client/client_interface.h"
#include "floatype/look/window_look.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/mac/mac_view_util.h"
#include "renderer/table_layout.h"

using mozc::client::SendCommandInterface;
using mozc::commands::CandidateWindow;
using mozc::commands::Output;
using mozc::commands::SessionCommand;
using mozc::renderer::ColumnType;
using mozc::renderer::kColumnShortcut;
using mozc::renderer::kColumnGap1;
using mozc::renderer::kColumnCandidate;
using mozc::renderer::kColumnDescription;
using mozc::renderer::kNumberOfColumns;
using mozc::renderer::TableLayout;
using mozc::renderer::mac::MacViewUtil;

namespace {

// floatype: the converter marks a pinned candidate (#28) with this at the
// start of its description; the window draws a red pin right of the word
// instead.
constexpr absl::string_view kPinMark = "📌";

bool IsPinned(const mozc::commands::CandidateWindow::Candidate &candidate) {
  return absl::StartsWith(candidate.annotation().description(), kPinMark);
}

std::string DescriptionWithoutPin(const mozc::commands::CandidateWindow::Candidate &candidate) {
  std::string description = candidate.annotation().description();
  if (IsPinned(candidate)) {
    description.erase(0, kPinMark.size());
    if (absl::StartsWith(description, " ")) {
      description.erase(0, 1);
    }
  }
  return description;
}

// The pin for a candidate drawn in `font`: SF Symbols' pin.fill, in red.
NSImage *PinImage(NSFont *font) {
  NSImageSymbolConfiguration *config = [[NSImageSymbolConfiguration
      configurationWithPointSize:font.pointSize * 0.62
                          weight:NSFontWeightMedium]
      configurationByApplyingConfiguration:
          [NSImageSymbolConfiguration configurationWithPaletteColors:@[
            // #E5484D, a soft red.
            [NSColor colorWithSRGBRed:0xe5 / 255.0 green:0x48 / 255.0 blue:0x4d / 255.0 alpha:1]
          ]]];
  return [[NSImage imageWithSystemSymbolName:@"pin.fill" accessibilityDescription:@"固定"]
      imageWithSymbolConfiguration:config];
}

NSFont *FontOf(const NSAttributedString *text) {
  return text.length > 0 ? [text attribute:NSFontAttributeName atIndex:0 effectiveRange:nil]
                         : nil;
}

}  // namespace

// Those constants and most rendering logic is as same as Windows
// native candidate window.
// TODO(mukai): integrate and share the code among Win and Mac.

// Private method declarations.
@interface CandidateView ()
- (void)initializeDefaultStyle;
- (BOOL)systemDark;
- (BOOL)updateLook;
- (std::string)footerLabel;
- (std::string)footerIndex;
- (NSAttributedString *)footerTextWithIndexAt:(CGFloat)indexRight;
// floatype: the space between a pinned word and its pin.
- (CGFloat)pinGap;

// Draw the |row|-th row.
- (void)drawRow:(int)row;

// Draw footer
- (void)drawFooter;

// Draw scroll bar
- (void)drawVScrollBar;
@end

@implementation CandidateView {
  int columnMinimumWidth_;

  mozc::commands::CandidateWindow candidate_window_;
  mozc::renderer::TableLayout tableLayout_;
  mozc::renderer::RendererStyle style_;

  // The row which has focused background.
  int focusedRow_;

  // Cache of attributed strings which is allocated at updateLayout.
  NSArray *candidateStringsCache_;

  // |command_sender_| holds a callback for mouse clicks.
  mozc::client::SendCommandInterface *command_sender_;

  // floatype: the look's settings and what they come to now.
  mozc::config::Config::CandidateStyle lookStyle_;
  mozc::floatype::WindowLook look_;
  // The settings and appearance look_ is for, to resolve it only on a change.
  std::string lookKey_;
  BOOL kindMark_;
}

#pragma mark initialization

- (id)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    [self updateLook];
    focusedRow_ = -1;
  }
  return self;
}

- (void)initializeDefaultStyle {
  style_ = mozc::floatype::RendererStyleFor(look_);

  // This macOS implementation appends two spaces to calculate the minimum width.
  // This is a workaround for compatibility with the historical behavior.
  std::string min_width_string = style_.column_minimum_width_string();
  if (!min_width_string.empty()) {
    min_width_string.append("  ");
  }
  // floatype: in the candidates' font (Mozc's: the message font at 14pt).
  const NSAttributedString *defaultMessage =
      MacViewUtil::ToNSAttributedString(min_width_string, style_.candidate_style());
  columnMinimumWidth_ = [defaultMessage size].width;

  // default line width is specified as 1.0 *pt*, but we want to draw
  // it as 1.0 px.
  [NSBezierPath setDefaultLineWidth:1.0];
  [NSBezierPath setDefaultLineJoinStyle:NSLineJoinStyleMiter];
}

- (BOOL)systemDark {
  return [[self.effectiveAppearance
             bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]]
      isEqualToString:NSAppearanceNameDarkAqua];
}

// Resolves the look again; returns whether it changed.
- (BOOL)updateLook {
  const BOOL dark = [self systemDark];
  std::string key = lookStyle_.SerializeAsString() + (dark ? "dark" : "light");
  if (key == lookKey_) {
    return NO;
  }
  lookKey_ = std::move(key);
  look_ = mozc::floatype::ResolveWindowLook(lookStyle_, dark);
  [self initializeDefaultStyle];
  return YES;
}

- (void)setLookStyle:(const mozc::config::Config::CandidateStyle &)style {
  lookStyle_ = style;
  [self updateLook];
}

- (void)setKindMark:(BOOL)kindMark {
  kindMark_ = kindMark;
}

// floatype: the footer's dot, when there is one: its diameter, and the space
// it takes with the gap after it.
- (BOOL)hasKindMark {
  return kindMark_ && candidate_window_.has_footer() &&
         (candidate_window_.category() == mozc::commands::PREDICTION ||
          candidate_window_.category() == mozc::commands::CONVERSION);
}

- (CGFloat)kindMarkDiameter {
  return 7 * look_.size_percent / 100.0;
}

- (const mozc::floatype::WindowLook &)look {
  return look_;
}

// Only colors change with the appearance, but the laid-out strings hold them.
- (void)viewDidChangeEffectiveAppearance {
  [super viewDidChangeEffectiveAppearance];
  if ([self updateLook]) {
    [self updateLayout];
    [self setNeedsDisplay:YES];
  }
}

- (void)setCandidateWindow:(const CandidateWindow *)candidate_window {
  candidate_window_ = *candidate_window;
}

- (void)setSendCommandInterface:(SendCommandInterface *)command_sender {
  command_sender_ = command_sender;
}

// Override of NSView.
- (BOOL)isFlipped {
  return YES;
}

- (void)dealloc {
  candidateStringsCache_ = nil;
}

- (const TableLayout *)tableLayout {
  return &tableLayout_;
}

#pragma mark drawing

- (NSSize)updateLayout {
  candidateStringsCache_ = nil;
  tableLayout_.Initialize(candidate_window_.candidate_size(), kNumberOfColumns);
  tableLayout_.SetWindowBorder(style_.window_border());

  // calculating focusedRow_
  if (candidate_window_.has_focused_index() && candidate_window_.candidate_size() > 0) {
    const int focusedIndex = candidate_window_.focused_index();
    focusedRow_ = focusedIndex - candidate_window_.candidate(0).index();
  } else {
    focusedRow_ = -1;
  }

  // Reserve footer space: floatype's line, the label left and the index
  // right with a gap between.
  if (candidate_window_.has_footer()) {
    const auto width = [&](const std::string &text) {
      return text.empty() ? 0.0
                          : MacViewUtil::ToNSAttributedString(text, style_.footer_style()).size.width;
    };
    const CGFloat label = width([self footerLabel]);
    const CGFloat index = width([self footerIndex]);
    const CGFloat gap = label > 0 && index > 0 ? 2 * look_.row_padding : 0;
    const CGFloat mark = [self hasKindMark] ? [self kindMarkDiameter] + look_.row_padding : 0;
    const NSSize textSize = [[self footerTextWithIndexAt:label + gap + index] size];
    tableLayout_.EnsureFooterSize(MacViewUtil::ToSize(NSMakeSize(
        std::ceil(label + gap + index + 2 * look_.row_padding + mark),
        std::ceil(textSize.height + look_.footer_top_padding + look_.footer_bottom_padding))));
  }

  tableLayout_.SetRowRectPadding(style_.row_rect_padding());
  if (candidate_window_.candidate_size() < candidate_window_.size()) {
    // floatype: room on both sides of the thumb, off the rows and the edge.
    tableLayout_.SetVScrollBar(style_.scrollbar_width() + 2 * [self scrollbarGap]);
  }

  const NSAttributedString *gap1 =
      MacViewUtil::ToNSAttributedString(" ", style_.gap1_style());
  // floatype: the gap after the numbers, and the rows' height.
  tableLayout_.EnsureCellSize(
      kColumnGap1, mozc::Size(std::lround(look_.row_padding), std::lround(look_.row_height)));

  NSMutableArray *newCache = [[NSMutableArray array] init];
  for (size_t i = 0; i < candidate_window_.candidate_size(); ++i) {
    const CandidateWindow::Candidate &candidate = candidate_window_.candidate(i);
    const NSAttributedString *shortcut = MacViewUtil::ToNSAttributedString(
        candidate.annotation().shortcut(), style_.shortcut_style());
    std::string value = candidate.value();
    if (candidate.annotation().has_prefix()) {
      value.insert(0, candidate.annotation().prefix());  // Prepend the prefix() to value.
    }
    if (candidate.annotation().has_suffix()) {
      value.append(candidate.annotation().suffix());
    }
    if (!value.empty()) {
      value.append("  ");
    }

    const NSAttributedString *candidateValue =
        MacViewUtil::ToNSAttributedString(value, style_.candidate_style());
    const NSAttributedString *description = MacViewUtil::ToNSAttributedString(
        DescriptionWithoutPin(candidate), style_.description_style());
    if ([shortcut length] > 0) {
      const NSSize shortcutSize =
          MacViewUtil::applyTheme([shortcut size], style_.shortcut_style());
      tableLayout_.EnsureCellSize(kColumnShortcut, MacViewUtil::ToSize(shortcutSize));
      tableLayout_.EnsureCellSize(
          kColumnShortcut,
          mozc::Size(std::lround(look_.row_padding + look_.number_width), 0));
    }
    if ([candidateValue length] > 0) {
      NSSize valueSize =
          MacViewUtil::applyTheme([candidateValue size], style_.candidate_style());
      // floatype: room for the pin right of the word.
      if (IsPinned(candidate)) {
        valueSize.width += PinImage(FontOf(candidateValue)).size.width + [self pinGap];
      }
      tableLayout_.EnsureCellSize(kColumnCandidate, MacViewUtil::ToSize(valueSize));
    }
    if ([description length] > 0) {
      NSSize descriptionSize =
          MacViewUtil::applyTheme([description size], style_.description_style());
      // floatype: room for the usage dot, which pushes the description left.
      if (candidate.has_information_id()) {
        descriptionSize.width += look_.row_padding;
      }
      tableLayout_.EnsureCellSize(kColumnDescription, MacViewUtil::ToSize(descriptionSize));
    }

    [newCache
        addObject:[NSArray arrayWithObjects:shortcut, gap1, candidateValue, description, nil]];
  }

  tableLayout_.EnsureColumnsWidth(kColumnCandidate, kColumnDescription, columnMinimumWidth_);

  candidateStringsCache_ = newCache;
  tableLayout_.FreezeLayout();
  return MacViewUtil::ToNSSize(tableLayout_.GetTotalSize());
}

- (void)drawRect:(NSRect)rect {
  if (!Category_IsValid(candidate_window_.category())) {
    LOG(WARNING) << "Unknown candidates category: " << candidate_window_.category();
    return;
  }

  // floatype: the background, which shows between the cells, inside the
  // rounded corners.
  const CGFloat radius = look_.corner_radius;
  [MacViewUtil::ToNSColor(style_.candidate_style().background_color()) set];
  if (radius > 0) {
    NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:self.bounds
                                                          xRadius:radius
                                                          yRadius:radius];
    [shape addClip];
    [shape fill];
  } else {
    NSRectFill(self.bounds);
  }

  for (int i = 0; i < candidate_window_.candidate_size(); ++i) {
    [self drawRow:i];
  }

  if (candidate_window_.candidate_size() < candidate_window_.size()) {
    [self drawVScrollBar];
  }
  [self drawFooter];

  // Draw the window border at last: floatype's hairline outline.
  [MacViewUtil::ToNSColor(style_.border_color()) set];
  [NSBezierPath setDefaultLineWidth:1.0];
  const mozc::Size windowSize = tableLayout_.GetTotalSize();
  const NSRect borderRect = NSMakeRect(0.5, 0.5, windowSize.width - 1, windowSize.height - 1);
  if (radius > 0) {
    [[NSBezierPath bezierPathWithRoundedRect:borderRect
                                     xRadius:radius - 0.5
                                     yRadius:radius - 0.5] stroke];
  } else {
    [NSBezierPath strokeRect:borderRect];
  }
}

#pragma mark drawing aux methods

- (void)drawRow:(int)row {
  // floatype: the focused row is a rounded rectangle; the others have no
  // background of their own.
  if (row == focusedRow_) {
    const NSRect focusedRect = MacViewUtil::ToNSRect(tableLayout_.GetRowRect(focusedRow_));
    const CGFloat radius = look_.row_corner_radius;
    [MacViewUtil::ToNSColor(style_.focused_background_color()) set];
    [[NSBezierPath bezierPathWithRoundedRect:focusedRect xRadius:radius yRadius:radius] fill];
    if (style_.focused_border_color().a() > 0) {
      [MacViewUtil::ToNSColor(style_.focused_border_color()) set];
      [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(focusedRect, 0.5, 0.5)
                                       xRadius:radius - 0.5
                                       yRadius:radius - 0.5] stroke];
    }
  }

  NSArray<NSAttributedString *> *candidate = [candidateStringsCache_ objectAtIndex:row];

  auto drawText = [&](ColumnType type,
                      const mozc::renderer::RendererStyle::TextStyle& text_style) {
    const NSAttributedString *text = [candidate objectAtIndex:type];
    if (row == focusedRow_ && text.length > 0) {
      NSMutableAttributedString *focused = [text mutableCopy];
      const uint32_t color =
          type == kColumnCandidate ? look_.focused_text : look_.focused_secondary_text;
      [focused addAttribute:NSForegroundColorAttributeName
                      value:[NSColor colorWithSRGBRed:((color >> 16) & 0xff) / 255.0
                                                green:((color >> 8) & 0xff) / 255.0
                                                 blue:(color & 0xff) / 255.0
                                                alpha:1]
                      range:NSMakeRange(0, focused.length)];
      text = focused;
    }
    NSRect cellRect = MacViewUtil::ToNSRect(tableLayout_.GetCellRect(row, type));
    NSPoint position = cellRect.origin;
    position.x += text_style.left_padding();
    // floatype: descriptions at the row's right end.
    if (type == kColumnDescription) {
      position.x = NSMaxX(cellRect) - text_style.right_padding() - [text size].width;
      // Clear of the usage dot.
      if (candidate_window_.candidate(row).has_information_id()) {
        position.x -= look_.row_padding;
      }
    }
    position.y += (cellRect.size.height - [text size].height) / 2;
    [text drawAtPoint:position];
  };

  drawText(kColumnShortcut, style_.shortcut_style());
  drawText(kColumnGap1, style_.gap1_style());
  drawText(kColumnCandidate, style_.candidate_style());
  drawText(kColumnDescription, style_.description_style());

  // floatype: a pinned candidate's pin, just right of the word, centered on
  // the text (the user found it low a little below).
  if (IsPinned(candidate_window_.candidate(row))) {
    const NSAttributedString *value = [candidate objectAtIndex:kColumnCandidate];
    // The value ends with two spaces before the description.
    const NSString *word =
        [value.string stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    const NSAttributedString *trimmed =
        [value attributedSubstringFromRange:NSMakeRange(0, word.length)];
    NSImage *pin = PinImage(FontOf(value));
    const NSRect cell = MacViewUtil::ToNSRect(tableLayout_.GetCellRect(row, kColumnCandidate));
    const NSSize size = pin.size;
    const NSRect rect = NSMakeRect(
        cell.origin.x + style_.candidate_style().left_padding() + trimmed.size.width +
            [self pinGap],
        NSMidY(cell) - size.height / 2, size.width,
        size.height);
    [pin drawInRect:rect
           fromRect:NSZeroRect
          operation:NSCompositingOperationSourceOver
           fraction:1
     respectFlipped:YES
              hints:nil];
  }

  // floatype: a row with usage examples gets a small orange dot at its
  // right end, rather than Mozc's bar, which looked like the scrollbar.
  if (candidate_window_.candidate(row).has_information_id()) {
    const NSRect rect = MacViewUtil::ToNSRect(tableLayout_.GetRowRect(row));
    const CGFloat diameter = 5 * look_.size_percent / 100.0;
    const NSRect dot = NSMakeRect(NSMaxX(rect) - look_.row_padding - diameter / 2,
                                  NSMidY(rect) - diameter / 2, diameter, diameter);
    // #E8914A, a muted orange.
    [[NSColor colorWithSRGBRed:0xe8 / 255.0 green:0x91 / 255.0 blue:0x4a / 255.0 alpha:1] set];
    [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
  }
}

- (CGFloat)pinGap {
  return 3 * look_.size_percent / 100.0;
}

// floatype: the footer's label and sub label, at its left.
- (std::string)footerLabel {
  const mozc::commands::Footer &footer = candidate_window_.footer();
  std::vector<std::string> parts;
  if (footer.has_label()) {
    parts.push_back(footer.label());
  }
  if (footer.has_sub_label()) {
    parts.push_back(footer.sub_label());
  }
  return absl::StrJoin(parts, "  ");
}

// floatype: the footer's index ("1 / 3"), at its right.
- (std::string)footerIndex {
  if (!candidate_window_.footer().index_visible()) {
    return "";
  }
  return absl::StrFormat("%d / %d",
                         candidate_window_.focused_index() + 1,  // 1-origin.
                         candidate_window_.size());
}

// floatype: the footer as one line, the label, a tab, and the index ending at
// `indexRight` from the line's start (the user asked for the label at the
// left). One line keeps both on one baseline. Without a label, an ideographic
// space keeps the Japanese line's height, so the window does not jump when
// the hint comes and goes.
- (NSAttributedString *)footerTextWithIndexAt:(CGFloat)indexRight {
  const std::string label = [self footerLabel];
  const std::string index = [self footerIndex];
  std::string text = label.empty() ? "\u3000" : label;
  if (!index.empty()) {
    absl::StrAppend(&text, "\t", index);
  }
  NSMutableAttributedString *line =
      [MacViewUtil::ToNSAttributedString(text, style_.footer_style()) mutableCopy];
  NSMutableParagraphStyle *paragraph = [[NSMutableParagraphStyle alloc] init];
  paragraph.tabStops = @[ [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentRight
                                                          location:indexRight
                                                           options:@{}] ];
  [line addAttribute:NSParagraphStyleAttributeName
               value:paragraph
               range:NSMakeRange(0, line.length)];
  return line;
}

- (void)drawFooter {
  if (!candidate_window_.has_footer()) {
    return;
  }
  const NSRect footerRect = MacViewUtil::ToNSRect(tableLayout_.GetFooterRect());
  const CGFloat mark = [self hasKindMark] ? [self kindMarkDiameter] + look_.row_padding : 0;
  const CGFloat left = NSMinX(footerRect) + look_.row_padding + mark;
  const NSAttributedString *text =
      [self footerTextWithIndexAt:NSMaxX(footerRect) - look_.row_padding - left];
  [text drawAtPoint:NSMakePoint(left, NSMinY(footerRect) + look_.footer_top_padding)];
  if ([self hasKindMark]) {
    const CGFloat diameter = [self kindMarkDiameter];
    const CGFloat middle = NSMinY(footerRect) + look_.footer_top_padding + text.size.height / 2;
    const uint32_t color = mozc::floatype::KindMarkColor(
        lookStyle_, candidate_window_.category() == mozc::commands::PREDICTION, look_.dark);
    [[NSColor colorWithSRGBRed:((color >> 16) & 0xff) / 255.0
                         green:((color >> 8) & 0xff) / 255.0
                          blue:(color & 0xff) / 255.0
                         alpha:1] set];
    [[NSBezierPath
        bezierPathWithOvalInRect:NSMakeRect(NSMinX(footerRect) + look_.row_padding,
                                            middle - diameter / 2, diameter, diameter)] fill];
  }
}

// floatype: the space on each side of the scrollbar's thumb.
- (CGFloat)scrollbarGap {
  return std::round(3 * look_.size_percent / 100.0);
}

- (void)drawVScrollBar {
  const mozc::Rect vscrollRect = tableLayout_.GetVScrollBarRect();
  if (vscrollRect.IsRectEmpty() || candidate_window_.candidate_size() <= 0) {
    return;
  }

  const int beginIndex = candidate_window_.candidate(0).index();
  const int candidatesTotal = candidate_window_.size();
  const int endIndex = candidate_window_.candidate(candidate_window_.candidate_size() - 1).index();

  // floatype: a thin rounded thumb, no track.
  const NSRect thumb = NSInsetRect(
      MacViewUtil::ToNSRect(
          tableLayout_.GetVScrollIndicatorRect(beginIndex, endIndex, candidatesTotal)),
      [self scrollbarGap], 0);
  const CGFloat radius = NSWidth(thumb) / 2;
  [MacViewUtil::ToNSColor(style_.scrollbar_indicator_color()) set];
  [[NSBezierPath bezierPathWithRoundedRect:thumb xRadius:radius yRadius:radius] fill];
}

#pragma mark event handling callbacks

- (void)mouseDown:(NSEvent *)event {
  const mozc::Point localPos = MacViewUtil::ToPoint([self convertPoint:[event locationInWindow]
                                                              fromView:nil]);
  int clickedRow = -1;
  for (int i = 0; i < tableLayout_.number_of_rows(); ++i) {
    const mozc::Rect rowRect = tableLayout_.GetRowRect(i);
    if (rowRect.PtrInRect(localPos)) {
      clickedRow = i;
      break;
    }
  }

  if (clickedRow >= 0 && clickedRow != focusedRow_) {
    focusedRow_ = clickedRow;
    [self setNeedsDisplay:YES];
  }
}

- (void)mouseUp:(NSEvent *)event {
  const mozc::Point localPos = MacViewUtil::ToPoint([self convertPoint:[event locationInWindow]
                                                              fromView:nil]);
  if (command_sender_ == nullptr) {
    return;
  }
  if (candidate_window_.candidate_size() < tableLayout_.number_of_rows()) {
    return;
  }
  for (int i = 0; i < tableLayout_.number_of_rows(); ++i) {
    const mozc::Rect rowRect = tableLayout_.GetRowRect(i);
    if (rowRect.PtrInRect(localPos)) {
      SessionCommand command;
      command.set_type(SessionCommand::SELECT_CANDIDATE);
      command.set_id(candidate_window_.candidate(i).id());
      Output dummy_output;
      command_sender_->SendCommand(command, &dummy_output);
      break;
    }
  }
}

- (void)mouseDragged:(NSEvent *)event {
  [self mouseDown:event];
}
@end
