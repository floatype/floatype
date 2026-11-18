// floatype: draws the candidate window at Mozc's font size and scaled, checks
// that the window grows with the fonts, and saves both as PNG files in the
// temporary directory (floatype-look) for a look.

#import <Cocoa/Cocoa.h>

#include <string>

#include "protocol/candidate_window.pb.h"
#import "renderer/mac/CandidateView.h"
#import "renderer/mac/IndicatorWindow.h"
#include "protocol/renderer_command.pb.h"
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

TEST(FloatypeLookTest, TheCandidateWindowGrowsWithItsFonts) {
  const commands::CandidateWindow candidates = Candidates();
  CandidateView *view = [[CandidateView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
  [view setCandidateWindow:&candidates];
  const NSSize normal = Draw(view, "candidates_100");

  [view setFontScalePercent:150 colors:config::Config::CandidateStyle()];
  [view setCandidateWindow:&candidates];
  const NSSize large = Draw(view, "candidates_150");
  EXPECT_GT(large.width / normal.width, 1.3);
  EXPECT_LT(large.width / normal.width, 1.6);
  EXPECT_GT(large.height / normal.height, 1.3);
  EXPECT_LT(large.height / normal.height, 1.6);

  // Back to 100% is Mozc's look again.
  [view setFontScalePercent:0 colors:config::Config::CandidateStyle()];
  [view setCandidateWindow:&candidates];
  const NSSize again = Draw(view, "candidates_again");
  EXPECT_EQ(again.width, normal.width);
  EXPECT_EQ(again.height, normal.height);
}

TEST(FloatypeLookTest, TheCandidateWindowTakesItsColors) {
  const commands::CandidateWindow candidates = Candidates();
  CandidateView *view = [[CandidateView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
  config::Config::CandidateStyle colors;
  colors.set_text_color(0x1a3d6b);
  colors.set_focused_background_color(0xffe4b0);
  colors.set_focused_border_color(0xd08a00);
  [view setFontScalePercent:100 colors:colors];
  [view setCandidateWindow:&candidates];
  Draw(view, "candidates_colors");
  // A spot in the focused (first) row's background, left of its text.
  NSBitmapImageRep *image = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
  NSColor *focused = [[image colorAtX:4 y:6] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  // Color spaces shift the values a little; Mozc's own focused background,
  // 0xd1eaff, is far off in red and blue.
  EXPECT_NEAR(focused.redComponent * 255, 0xff, 20);
  EXPECT_NEAR(focused.greenComponent * 255, 0xe4, 20);
  EXPECT_NEAR(focused.blueComponent * 255, 0xb0, 20);
  EXPECT_LT(focused.blueComponent * 255, 0xea) << "not Mozc's blue";
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
  // Without a reading there is no ruby.
  command.mutable_output()->clear_live_reading();
  window.Update(command, 1000);
  EXPECT_FALSE(ruby.window.isVisible);
  window.Hide();
}

}  // namespace
}  // namespace mozc
