// floatype: draws the candidate window and the indicator in their looks,
// checks what can be checked, and saves them as PNG files in the temporary
// directory (floatype-look) for a look.

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "floatype/look/window_look.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/renderer_command.pb.h"
#import "renderer/mac/CandidateView.h"
#include "renderer/mac/CandidateWindow.h"
#import "renderer/mac/IndicatorWindow.h"
#include "renderer/table_layout.h"
#include "testing/gunit.h"

namespace mozc {
namespace {

commands::CandidateWindow Candidates() {
  commands::CandidateWindow window;
  window.set_size(3);
  window.set_focused_index(0);
  window.set_position(0);
  const char *values[] = {"機械", "機会", "器械"};
  const char *descriptions[] = {"", "", "医療用など"};
  for (int i = 0; i < 3; ++i) {
    commands::CandidateWindow::Candidate *candidate = window.add_candidate();
    candidate->set_index(i);
    candidate->set_value(values[i]);
    candidate->mutable_annotation()->set_shortcut(std::to_string(i + 1));
    candidate->mutable_annotation()->set_description(descriptions[i]);
  }
  window.mutable_footer()->set_index_visible(true);
  return window;
}

// Saves what `view` draws as `name`.png.
void Save(NSView *view, const char *name) {
  NSBitmapImageRep *image = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
  // xcodebuild runs the test without Bazel's outputs directory.
  NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"floatype-look"];
  [[NSFileManager defaultManager] createDirectoryAtPath:dir
                            withIntermediateDirectories:YES
                                             attributes:nil
                                                  error:nil];
  NSData *png = [image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  [png writeToFile:[NSString stringWithFormat:@"%@/%s.png", dir, name] atomically:YES];
}

// Lays the view out, draws it, and saves it as `name`.png. Returns the
// laid-out size.
NSSize Draw(CandidateView *view, const char *name) {
  const NSSize size = [view updateLayout];
  [view setFrame:NSMakeRect(0, 0, size.width, size.height)];
  Save(view, name);
  return size;
}

using Style = config::Config::CandidateStyle;

// A view for `style` in the light (or dark) appearance, laid out.
CandidateView *ViewFor(const Style &style, bool dark = false) {
  const commands::CandidateWindow candidates = Candidates();
  CandidateView *view = [[CandidateView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
  view.appearance =
      [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
  [view setLookStyle:style];
  [view setCandidateWindow:&candidates];
  return view;
}

// The color at point (x, y) from the top left of what `view` draws, as
// 0xRRGGBB, and its alpha.
uint32_t ColorAt(NSView *view, double x, double y, double *alpha = nullptr) {
  NSBitmapImageRep *image = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
  const double scale = image.pixelsWide / NSWidth(view.bounds);
  NSColor *color = [[image colorAtX:x * scale
                                  y:y * scale] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  if (alpha != nullptr) {
    *alpha = color.alphaComponent;
  }
  const auto byte = [](CGFloat c) { return static_cast<uint32_t>(c * 255 + 0.5); };
  return byte(color.redComponent) << 16 | byte(color.greenComponent) << 8 |
         byte(color.blueComponent);
}

// Color spaces and antialiasing shift the values a little.
void ExpectNear(uint32_t color, uint32_t expected, const char *what) {
  for (int shift : {16, 8, 0}) {
    EXPECT_NEAR(static_cast<int>((color >> shift) & 0xff),
                static_cast<int>((expected >> shift) & 0xff), 20)
        << what << std::hex << ": 0x" << color << " for 0x" << expected;
  }
}

// A spot in the empty description cell of the second (unfocused) row, where
// the window's background shows.
uint32_t BackgroundOf(CandidateView *view) {
  const mozc::Rect row = [view tableLayout]->GetRowRect(1);
  return ColorAt(view, row.Right() - 4, row.Top() + row.Height() / 2.0);
}

// A spot in the focused (first) row, left of its number.
uint32_t FocusedOf(CandidateView *view) {
  const mozc::Rect row = [view tableLayout]->GetRowRect(0);
  return ColorAt(view, row.Left() + 4, row.Top() + row.Height() / 2.0);
}

// The color in `column` of `row` farthest from the background: the text's.
uint32_t TextIn(CandidateView *view, int row, renderer::ColumnType column) {
  const mozc::Rect cell = [view tableLayout]->GetCellRect(row, column);
  const uint32_t background = row == 0 ? FocusedOf(view) : BackgroundOf(view);
  NSBitmapImageRep *image = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
  const double scale = image.pixelsWide / NSWidth(view.bounds);
  const auto byte = [](CGFloat c) { return static_cast<int>(c * 255 + 0.5); };
  uint32_t text = background;
  int farthest = -1;
  for (int x = cell.Left() * scale; x < cell.Right() * scale; ++x) {
    for (int y = cell.Top() * scale; y < cell.Bottom() * scale; ++y) {
      NSColor *color = [[image colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      const int r = byte(color.redComponent), g = byte(color.greenComponent),
                b = byte(color.blueComponent);
      const int distance = std::abs(r - static_cast<int>((background >> 16) & 0xff)) +
                           std::abs(g - static_cast<int>((background >> 8) & 0xff)) +
                           std::abs(b - static_cast<int>(background & 0xff));
      if (distance > farthest) {
        farthest = distance;
        text = r << 16 | g << 8 | b;
      }
    }
  }
  return text;
}

TEST(FloatypeLookTest, TheCandidateWindowGrowsWithItsFonts) {
  CandidateView *view = ViewFor(Style());
  const NSSize normal = Draw(view, "candidates_100");

  Style large_style;
  large_style.set_size_percent(150);
  [view setLookStyle:large_style];
  const NSSize large = Draw(view, "candidates_150");
  EXPECT_GT(large.width / normal.width, 1.3);
  EXPECT_LT(large.width / normal.width, 1.6);
  EXPECT_GT(large.height / normal.height, 1.3);
  EXPECT_LT(large.height / normal.height, 1.6);

  // Back to 100% is Mozc's look again.
  [view setLookStyle:Style()];
  const NSSize again = Draw(view, "candidates_again");
  EXPECT_EQ(again.width, normal.width);
  EXPECT_EQ(again.height, normal.height);
}

TEST(FloatypeLookTest, TheCandidateWindowTakesItsColors) {
  Style style;
  style.set_theme(Style::CUSTOM);
  style.set_text_color(0x1a3d6b);
  style.set_focused_background_color(0xffe4b0);
  style.set_focused_border_color(0xd08a00);
  CandidateView *view = ViewFor(style);
  Draw(view, "candidates_colors");
  // The default's 0xc4d0e1 is far off in red and blue.
  ExpectNear(FocusedOf(view), 0xffe4b0, "focused");
  ExpectNear(BackgroundOf(view), 0xffffff, "the light background");

  // Without CUSTOM the colors wait.
  style.set_theme(Style::LIGHT);
  [view setLookStyle:style];
  Draw(view, "candidates_light");
  ExpectNear(FocusedOf(view), 0xc4d0e1, "light's focused");

  style.set_theme(Style::CUSTOM);
  style.set_background_color(0x203040);
  [view setLookStyle:style];
  Draw(view, "candidates_custom_background");
  ExpectNear(BackgroundOf(view), 0x203040, "custom background");
}

TEST(FloatypeLookTest, AutoFollowsTheAppearance) {
  CandidateView *view = ViewFor(Style(), /*dark=*/true);
  Draw(view, "candidates_dark");
  EXPECT_TRUE([view look].dark);
  ExpectNear(BackgroundOf(view), 0x26272b, "dark background");
  ExpectNear(FocusedOf(view), 0xc4d0e1, "the focused row's blue in the dark too");
  ExpectNear(TextIn(view, 0, renderer::kColumnCandidate), 0x111318, "ink on it");
  ExpectNear(TextIn(view, 1, renderer::kColumnCandidate), 0xeceef2, "dark's text");
  ExpectNear(TextIn(view, 1, renderer::kColumnShortcut), 0x80848d, "dark's numbers");

  // The appearance changes while it is shown: the text turns dark too,
  // without a new layout.
  view.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
  EXPECT_FALSE([view look].dark);
  Save(view, "candidates_light_switched");
  const mozc::Rect cell = [view tableLayout]->GetCellRect(1, renderer::kColumnCandidate);
  NSBitmapImageRep *image = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
  const double scale = image.pixelsWide / NSWidth(view.bounds);
  CGFloat darkest = 1;
  for (int x = cell.Left() * scale; x < cell.Right() * scale; ++x) {
    for (int y = cell.Top() * scale; y < cell.Bottom() * scale; ++y) {
      darkest = std::min(
          darkest,
          [[image colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.sRGBColorSpace].blueComponent);
    }
  }
  EXPECT_LT(darkest, 0.25) << "black text, not dark mode's light grey";
  Draw(view, "candidates_light_again");
  ExpectNear(BackgroundOf(view), 0xffffff, "light background");

  // DARK stays dark in the light.
  Style dark;
  dark.set_theme(Style::DARK);
  [view setLookStyle:dark];
  Draw(view, "candidates_dark_theme");
  ExpectNear(BackgroundOf(view), 0x26272b, "DARK in the light");
}

TEST(FloatypeLookTest, AFontFamilyMissingIsTheSystemFont) {
  const NSSize system = Draw(ViewFor(Style()), "candidates_font_system");
  Style style;
  style.set_font_family("No Such Family Anywhere");
  const NSSize missing = Draw(ViewFor(style), "candidates_font_missing");
  EXPECT_EQ(missing.width, system.width);
  EXPECT_EQ(missing.height, system.height);
  style.set_font_family("Hiragino Mincho ProN");
  ASSERT_NE(
      [[NSFontManager sharedFontManager] availableMembersOfFontFamily:@"Hiragino Mincho ProN"],
      nil);
  const NSSize mincho = Draw(ViewFor(style), "candidates_font_mincho");
  EXPECT_NE(mincho.width, system.width) << "a font of its own";
}

TEST(FloatypeLookTest, TheLightLook) {
  CandidateView *view = ViewFor(Style());
  Draw(view, "candidates_light_look");
  ExpectNear(BackgroundOf(view), 0xffffff, "background");
  ExpectNear(FocusedOf(view), 0xc4d0e1, "focused row");
  ExpectNear(TextIn(view, 0, renderer::kColumnCandidate), 0x111318, "focused text");
  ExpectNear(TextIn(view, 1, renderer::kColumnCandidate), 0x17191e, "text");
  ExpectNear(TextIn(view, 1, renderer::kColumnShortcut), 0x989ca5, "number");
  ExpectNear(TextIn(view, 0, renderer::kColumnShortcut), 0x555b64, "number on the focus");
  ExpectNear(TextIn(view, 2, renderer::kColumnDescription), 0x989ca5, "description");
  // No column behind the numbers.
  const mozc::Rect number = [view tableLayout]->GetCellRect(1, renderer::kColumnShortcut);
  ExpectNear(ColorAt(view, number.Left() + 2, number.Top() + 2), 0xffffff, "no number column");
  // Rows of 24pt inside 5pt of padding.
  EXPECT_EQ([view tableLayout]->GetRowRect(0).Height(), 24);
  EXPECT_EQ([view tableLayout]->GetRowRect(0).Top(), 5);
  // The focused row's corners are round: its corner shows the background.
  const mozc::Rect row = [view tableLayout]->GetRowRect(0);
  ExpectNear(ColorAt(view, row.Left() + 0.5, row.Top() + 0.5), 0xffffff, "rounded focus");
}

TEST(FloatypeLookTest, TheScrollbarIsAThumbWithoutATrack) {
  commands::CandidateWindow candidates = Candidates();
  candidates.set_size(9);
  CandidateView *view = ViewFor(Style());
  [view setCandidateWindow:&candidates];
  Draw(view, "candidates_scrollbar");
  const renderer::TableLayout *layout = [view tableLayout];
  const mozc::Rect bar = layout->GetVScrollBarRect();
  ASSERT_GT(bar.Width(), 0);
  const mozc::Rect thumb = layout->GetVScrollIndicatorRect(0, 2, 9);
  ExpectNear(ColorAt(view, thumb.Left() + thumb.Width() / 2.0, thumb.Top() + thumb.Height() / 2.0),
             0x989ca5, "the thumb");
  ExpectNear(ColorAt(view, bar.Left() + bar.Width() / 2.0, bar.Bottom() - 3), 0xffffff,
             "no track below it");
}

TEST(FloatypeLookTest, RoundedCornersAreSeeThrough) {
  Style style;
  CandidateView *view = ViewFor(style);
  Draw(view, "candidates_rounded");
  double alpha = 1;
  ColorAt(view, 0.5, 0.5, &alpha);
  EXPECT_LT(alpha, 0.1) << "the corner";
  ExpectNear(BackgroundOf(view), 0xffffff, "inside");

  // Square ones at 0.
  style.set_corner_radius(0);
  view = ViewFor(style);
  Draw(view, "candidates_square");
  ColorAt(view, 0.5, 0.5, &alpha);
  EXPECT_EQ(alpha, 1);
}

class CandidateWindowTest : public ::testing::Test {
 protected:
  void Update(const Style &conversion, const Style &suggest,
              commands::Category category = commands::CONVERSION) {
    commands::CandidateWindow candidates = Candidates();
    candidates.set_category(category);
    window_.SetLook(conversion, suggest);
    window_.SetCandidateWindow(candidates);
    window_.MoveWindow(NSMakePoint(200, 400));
    window_.Show();
  }
  renderer::mac::CandidateWindow window_;
};

TEST_F(CandidateWindowTest, TheDefaultIsRoundedWithASoftShadow) {
  Update(Style(), Style());
  NSWindow *window = window_.WindowForTesting();
  EXPECT_FALSE(window.opaque);
  EXPECT_EQ(window.alphaValue, 1);
  EXPECT_FALSE(window.hasShadow) << "the look's own, not the system's";
  EXPECT_TRUE(window_.ShadowWindowForTesting().visible);
  window_.Hide();
}

TEST_F(CandidateWindowTest, TakesItsCornersOpacityAndShadow) {
  Style style;
  style.set_corner_radius(8);
  style.set_opacity_percent(70);
  style.set_shadow(Style::NONE);
  Update(style, Style());
  NSWindow *window = window_.WindowForTesting();
  EXPECT_FALSE(window.opaque);
  EXPECT_NEAR(window.alphaValue, 0.7, 0.01);
  EXPECT_FALSE(window.hasShadow);
  Save(window_.ViewForTesting(), "window_rounded_see_through");

  style.set_shadow(Style::STRONG);
  Update(style, Style());
  EXPECT_FALSE(window.hasShadow);
  NSWindow *shadow = window_.ShadowWindowForTesting();
  ASSERT_NE(shadow, nil);
  EXPECT_TRUE(shadow.visible);
  EXPECT_TRUE(NSContainsRect(shadow.frame, window.frame));
  EXPECT_GT(NSWidth(shadow.frame), NSWidth(window.frame));
  Save(shadow.contentView, "window_strong_shadow");
  // The strong shadow goes and comes with its window.
  window_.MoveWindow(NSMakePoint(300, 500));
  EXPECT_TRUE(NSContainsRect(shadow.frame, window.frame));
  window_.Hide();
  EXPECT_FALSE(shadow.visible);
  window_.Show();
  EXPECT_TRUE(shadow.visible);

  // SOFT's is smaller.
  const double strong = NSWidth(shadow.frame);
  style.set_shadow(Style::SOFT);
  Update(style, Style());
  EXPECT_FALSE(window.hasShadow);
  EXPECT_TRUE(shadow.visible);
  EXPECT_LT(NSWidth(shadow.frame), strong);
  Save(shadow.contentView, "window_soft_shadow");
  window_.Hide();
}

TEST_F(CandidateWindowTest, SuggestionsTakeTheSuggestStyle) {
  Style suggest;
  suggest.set_size_percent(150);
  Update(Style(), suggest, commands::CONVERSION);
  const double conversion = NSWidth(window_.WindowForTesting().frame);
  Update(Style(), suggest, commands::SUGGESTION);
  const double suggestion = NSWidth(window_.WindowForTesting().frame);
  Save(window_.ViewForTesting(), "window_suggest_style");
  EXPECT_GT(suggestion, conversion * 1.3);
  Update(Style(), suggest, commands::PREDICTION);
  EXPECT_EQ(NSWidth(window_.WindowForTesting().frame), suggestion);
  Update(suggest, Style(), commands::PREDICTION);
  EXPECT_EQ(NSWidth(window_.WindowForTesting().frame), conversion)
      << "candidate_style is the conversion window's";
  window_.Hide();
}

TEST(FloatypeLookTest, TheIndicatorShowsEachState) {
  renderer::mac::IndicatorWindow window;
  commands::RendererCommand command;
  command.set_visible(true);
  commands::RendererCommand::Rectangle *line = command.mutable_composition_rectangle();
  line->set_left(200);
  line->set_top(300);
  line->set_bottom(320);
  const commands::Output::Indicator::State states[] = {
      commands::Output::Indicator::READING, commands::Output::Indicator::MOZC,
      commands::Output::Indicator::ZENZ_PENDING, commands::Output::Indicator::ZENZ};
  const char *names[] = {"indicator_reading", "indicator_mozc", "indicator_pending",
                         "indicator_zenz"};
  for (int i = 0; i < 4; ++i) {
    command.mutable_output()->mutable_indicator()->set_state(states[i]);
    window.Update(command, 1000);
    NSView *view = window.ViewForTesting();
    // Mozkey's default size, grown to fit 入力中 if need be.
    EXPECT_GE(NSWidth(view.bounds), 56) << names[i];
    EXPECT_EQ(NSHeight(view.bounds), 22) << names[i];
    Save(view, names[i]);
  }
  command.mutable_output()->mutable_indicator()->mutable_style()->set_shape(
      config::Config::IndicatorStyle::CIRCLE);
  command.mutable_output()->mutable_indicator()->mutable_style()->set_label(
      config::Config::IndicatorStyle::SYMBOL);
  window.Update(command, 1000);
  EXPECT_EQ(NSWidth(window.ViewForTesting().bounds), 22) << "a circle is round";
  Save(window.ViewForTesting(), "indicator_circle");
  window.Hide();
}

TEST(FloatypeLookTest, MixedEnglishAddsTheWordsLanguage) {
  renderer::mac::IndicatorWindow window;
  commands::RendererCommand command;
  command.set_visible(true);
  commands::RendererCommand::Rectangle *line = command.mutable_composition_rectangle();
  line->set_left(200);
  line->set_right(201);
  line->set_top(300);
  line->set_bottom(320);
  commands::Output::Indicator *indicator = command.mutable_output()->mutable_indicator();
  indicator->set_state(commands::Output::Indicator::READING);
  window.Update(command, 1000);
  const double plain = NSWidth(window.ViewForTesting().window.frame);
  indicator->set_english(true);
  window.Update(command, 1000);
  EXPECT_GT(NSWidth(window.ViewForTesting().window.frame), plain);
  Save(window.ViewForTesting(), "indicator-english");
  // Color only stays without text.
  indicator->mutable_style()->set_label(config::Config::IndicatorStyle::COLOR_ONLY);
  window.Update(command, 1000);
  const double color_only = NSWidth(window.ViewForTesting().window.frame);
  indicator->clear_english();
  window.Update(command, 1000);
  EXPECT_EQ(NSWidth(window.ViewForTesting().window.frame), color_only);
  window.Hide();
}

TEST(FloatypeLookTest, TheReadingSitsAboveTheIndicator) {
  renderer::mac::IndicatorWindow window;
  commands::RendererCommand command;
  command.set_visible(true);
  commands::RendererCommand::Rectangle *line = command.mutable_composition_rectangle();
  line->set_left(200);
  line->set_right(201);
  line->set_top(300);
  line->set_bottom(320);
  command.mutable_output()->mutable_indicator()->set_state(commands::Output::Indicator::MOZC);
  command.mutable_output()->set_live_reading("きかい");
  window.Update(command, 1000);
  NSView *ruby = window.RubyViewForTesting();
  NSView *indicator = window.ViewForTesting();
  EXPECT_GE(NSMinY(ruby.window.frame), NSMaxY(indicator.window.frame));
  EXPECT_EQ(NSMinX(ruby.window.frame), 200);
  Save(ruby, "ruby");
  // A larger, colored reading follows its style.
  const double plain = NSHeight(ruby.window.frame);
  command.mutable_output()->mutable_ruby_style()->set_font_size(18);
  command.mutable_output()->mutable_ruby_style()->set_background_color(0xfff3c4);
  window.Update(command, 1000);
  EXPECT_GT(NSHeight(ruby.window.frame), plain);
  Save(ruby, "ruby-styled");
  command.mutable_output()->clear_ruby_style();

  // Its corners and opacity: rounded by default, square at 0; 92% by
  // default, half at 50.
  window.Update(command, 1000);
  double alpha = 1;
  ColorAt(ruby, 0.5, 0.5, &alpha);
  EXPECT_LT(alpha, 0.5) << "a rounded corner";
  ColorAt(ruby, 1.5, NSHeight(ruby.bounds) / 2, &alpha);
  EXPECT_NEAR(alpha, 0.92, 0.03);
  command.mutable_output()->mutable_ruby_style()->set_corner_radius(0);
  command.mutable_output()->mutable_ruby_style()->set_opacity_percent(50);
  window.Update(command, 1000);
  Save(ruby, "ruby-square-half");
  ColorAt(ruby, 0.5, 0.5, &alpha);
  EXPECT_GT(alpha, 0.4) << "a square corner";
  ColorAt(ruby, 1.5, NSHeight(ruby.bounds) / 2, &alpha);
  EXPECT_NEAR(alpha, 0.5, 0.04);
  command.mutable_output()->clear_ruby_style();

  // A vertical column puts it beside the column, past the indicator.
  line->set_left(200);
  line->set_right(220);
  line->set_top(300);
  line->set_bottom(301);
  window.Update(command, 1000);
  EXPECT_TRUE(ruby.window.isVisible);
  EXPECT_GE(NSMinX(ruby.window.frame), NSMaxX(indicator.window.frame));
  EXPECT_EQ(NSMaxY(ruby.window.frame), 1000 - 300);

  // Without a reading there is no ruby.
  command.mutable_output()->clear_live_reading();
  window.Update(command, 1000);
  EXPECT_FALSE(ruby.window.isVisible);
  window.Hide();
}

// The conversion and suggestion windows of the mock, on a backdrop with the
// window's own shadow, saved as sheet_<theme>_<window>.png.
commands::CandidateWindow SheetCandidates(bool suggestion) {
  commands::CandidateWindow window;
  window.set_category(suggestion ? commands::SUGGESTION : commands::CONVERSION);
  window.set_size(3);
  window.set_focused_index(0);
  window.set_position(0);
  const char *values[] = {"今日はいい天気です", "今日もいい天気です", "キョウハイイテンキ"};
  const char *descriptions[] = {"", "", "カタカナ"};
  for (int i = 0; i < 3; ++i) {
    commands::CandidateWindow::Candidate *candidate = window.add_candidate();
    candidate->set_index(i);
    candidate->set_value(values[i]);
    if (!suggestion) {
      candidate->mutable_annotation()->set_shortcut(std::to_string(i + 1));
    }
    candidate->mutable_annotation()->set_description(descriptions[i]);
  }
  if (suggestion) {
    // What Mozc shows under suggestions (engine/engine_output.cc).
    window.mutable_footer()->set_label("Tabキーで選択");
  } else {
    window.mutable_footer()->set_index_visible(true);
  }
  return window;
}

NSImage *Capture(NSView *view) {
  NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
  NSImage *image = [[NSImage alloc] initWithSize:view.bounds.size];
  [image addRepresentation:rep];
  return image;
}

TEST(FloatypeLookSheet, SavesTheWindows) {
  for (const bool dark : {false, true}) {
    for (const bool suggestion : {false, true}) {
      renderer::mac::CandidateWindow window;
      const commands::CandidateWindow candidates = SheetCandidates(suggestion);
      window.SetLook(Style(), Style());
      window.SetCandidateWindow(candidates);
      window.WindowForTesting().appearance =
          [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
      window.SetCandidateWindow(candidates);
      window.MoveWindow(NSMakePoint(200, 400));
      window.Show();
      NSView *view = window.ViewForTesting();
      NSWindow *shadow = window.ShadowWindowForTesting();
      const CGFloat margin = 60;
      const NSSize size =
          NSMakeSize(NSWidth(view.bounds) + 2 * margin, NSHeight(view.bounds) + 2 * margin);
      NSBitmapImageRep *out =
          [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                  pixelsWide:size.width * 2
                                                  pixelsHigh:size.height * 2
                                               bitsPerSample:8
                                             samplesPerPixel:4
                                                    hasAlpha:YES
                                                    isPlanar:NO
                                              colorSpaceName:NSDeviceRGBColorSpace
                                                 bytesPerRow:0
                                                bitsPerPixel:0];
      out.size = size;
      [NSGraphicsContext saveGraphicsState];
      [NSGraphicsContext
          setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:out]];
      [(dark ? [NSColor colorWithSRGBRed:0x1d / 255.0 green:0x1e / 255.0 blue:0x23 / 255.0 alpha:1]
             : [NSColor colorWithSRGBRed:0xe9 / 255.0 green:0xeb / 255.0 blue:0xf0 / 255.0
                                   alpha:1]) set];
      NSRectFill(NSMakeRect(0, 0, size.width, size.height));
      const NSRect frame = window.WindowForTesting().frame;
      if (shadow != nil && shadow.visible) {
        const NSRect around = shadow.frame;
        [Capture(shadow.contentView) drawInRect:NSMakeRect(margin + NSMinX(around) - NSMinX(frame),
                                                           margin + NSMinY(around) - NSMinY(frame),
                                                           NSWidth(around), NSHeight(around))];
      }
      [Capture(view) drawInRect:NSMakeRect(margin, margin, NSWidth(frame), NSHeight(frame))
                       fromRect:NSZeroRect
                      operation:NSCompositingOperationSourceOver
                       fraction:window.WindowForTesting().alphaValue];
      [NSGraphicsContext restoreGraphicsState];
      NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"floatype-look"];
      [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:nil];
      [[out representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
          writeToFile:[NSString stringWithFormat:@"%@/sheet_%s_%s.png", dir,
                                                 dark ? "dark" : "light",
                                                 suggestion ? "suggestion" : "conversion"]
           atomically:YES];
      window.Hide();
    }
  }
}

}  // namespace
}  // namespace mozc
