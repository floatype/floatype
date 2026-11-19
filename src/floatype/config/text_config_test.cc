#include "floatype/config/text_config.h"

#include "absl/strings/match.h"
#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

using config::Config;

TEST(ParseTextConfigTest, ReadsTheFieldsWritten) {
  const absl::StatusOr<Config> config = ParseTextConfig(
      "# comment\n"
      "live_conversion_delay_millisec: 300\n"
      "indicator_style { placement: BELOW zenz_color: 0x112233 }\n"
      "suggest_added_words: true\n");
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->live_conversion_delay_millisec(), 300);
  EXPECT_TRUE(config->suggest_added_words());
  EXPECT_EQ(config->indicator_style().placement(),
            Config::IndicatorStyle::BELOW);
  EXPECT_EQ(config->indicator_style().zenz_color(), 0x112233u);
  EXPECT_FALSE(config->has_use_zenz());
}

TEST(ParseTextConfigTest, NamesTheLineOfAnError) {
  const absl::StatusOr<Config> unknown =
      ParseTextConfig("use_zenz: true\nuse_zenzz: true\n");
  ASSERT_FALSE(unknown.ok());
  EXPECT_TRUE(absl::StartsWith(unknown.status().message(), "2 行目"))
      << unknown.status();

  const absl::StatusOr<Config> type = ParseTextConfig("use_zenz: 3\n");
  ASSERT_FALSE(type.ok());
  EXPECT_TRUE(absl::StartsWith(type.status().message(), "1 行目"))
      << type.status();
}

TEST(ParseTextConfigTest, ChecksRangesLikeTypes) {
  const absl::StatusOr<Config> delay = ParseTextConfig(
      "use_zenz: true\n\nlive_conversion_delay_millisec: 3000\n");
  ASSERT_FALSE(delay.ok());
  EXPECT_EQ(delay.status().message(),
            "3 行目: live_conversion_delay_millisec は 0〜2000 の範囲で"
            "書いてください（3000 と書かれています）");

  const absl::StatusOr<Config> opacity =
      ParseTextConfig("indicator_style {\n  opacity_percent: 150\n}\n");
  ASSERT_FALSE(opacity.ok());
  EXPECT_TRUE(absl::StartsWith(opacity.status().message(), "2 行目"))
      << opacity.status();

  EXPECT_FALSE(ParseTextConfig("ruby_style { font_size: 200 }\n").ok());
  EXPECT_TRUE(
      ParseTextConfig("ruby_style { font_size: 14 text_color: 0x112233 }\n")
          .ok());
  EXPECT_FALSE(ParseTextConfig("suggest_style { size_percent: 20 }\n").ok());
  EXPECT_FALSE(
      ParseTextConfig("candidate_style { opacity_percent: 10 }\n").ok());
  EXPECT_TRUE(ParseTextConfig("suggest_style { corner_radius: 24 }\n").ok());

  EXPECT_FALSE(ParseTextConfig("zenz_timeout_millisec: 10\n").ok());
  EXPECT_TRUE(ParseTextConfig("zenz_timeout_millisec: 50\n").ok());
}

TEST(OverlayTest, TheFileWinsForWhatItSets) {
  Config config;
  config.set_use_zenz(true);
  config.set_suggestions_size(5);
  config.add_character_form_rules()->set_group("ア");
  config.add_character_form_rules()->set_group("0");
  const Config overlay = *ParseTextConfig(
      "use_zenz: false\n"
      "character_form_rules { group: \"A\" }\n");

  ApplyOverlay(overlay, config);
  EXPECT_FALSE(config.use_zenz());
  EXPECT_EQ(config.suggestions_size(), 5) << "left as it was";
  ASSERT_EQ(config.character_form_rules_size(), 1) << "replaced, not appended";
  EXPECT_EQ(config.character_form_rules(0).group(), "A");
}

}  // namespace
}  // namespace mozc::floatype
