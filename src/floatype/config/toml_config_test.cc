#include "floatype/config/toml_config.h"

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "base/protobuf/text_format.h"
#include "config/config_handler.h"
#include "floatype/config/text_config.h"
#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

using config::Config;

void ExpectErrorAt(absl::string_view text, absl::string_view line) {
  const absl::StatusOr<Config> config = ParseTomlConfig(text);
  ASSERT_FALSE(config.ok()) << text;
  EXPECT_TRUE(absl::StartsWith(config.status().message(), line))
      << config.status();
}

std::string Print(const Config& config) {
  std::string text;
  EXPECT_TRUE(google::protobuf::TextFormat::PrintToString(config, &text));
  return text;
}

// The settings a file gives: `defaults` with what it sets.
Config Read(absl::string_view text, const Config& defaults) {
  const absl::StatusOr<Config> file = ParseTomlConfig(text);
  EXPECT_TRUE(file.ok()) << file.status() << "\n" << text;
  Config config = defaults;
  ApplyOverlay(file.value_or(Config()), config);
  return config;
}

TEST(ParseTomlConfigTest, ReadsTheFieldsWritten) {
  const absl::StatusOr<Config> config = ParseTomlConfig(
      "# comment\n"
      "use_zenz = false\n"
      "live_conversion_delay_millisec = 300\n"
      "session_keymap = \"MSIME\"\n"
      "overlay_keymaps = [\"KOTOERI\", \"ATOK\"]\n"
      "direct_commit_characters = \"。\\\"\"\n"
      "custom_roman_table = '''\n"
      "a\tあ\n"
      "'''\n"
      "\n"
      "[indicator_style]\n"
      "placement = \"BELOW\"\n"
      "zenz_color = 0x112233\n"
      "text_color = 255\n"
      "offset_x = -3\n"
      "\n"
      "[[character_form_rules]]\n"
      "group = \"ア\"\n"
      "preedit_character_form = \"HALF_WIDTH\"\n"
      "[[character_form_rules]]\n"
      "group = \"0\"\n");
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_FALSE(config->use_zenz());
  EXPECT_TRUE(config->has_use_zenz());
  EXPECT_EQ(config->live_conversion_delay_millisec(), 300);
  EXPECT_EQ(config->session_keymap(), Config::MSIME);
  ASSERT_EQ(config->overlay_keymaps_size(), 2);
  EXPECT_EQ(config->overlay_keymaps(1), Config::ATOK);
  EXPECT_EQ(config->direct_commit_characters(), "。\"");
  EXPECT_EQ(config->custom_roman_table(), "a\tあ\n");
  EXPECT_EQ(config->indicator_style().placement(),
            Config::IndicatorStyle::BELOW);
  EXPECT_EQ(config->indicator_style().zenz_color(), 0x112233u);
  EXPECT_EQ(config->indicator_style().text_color(), 255u);
  EXPECT_EQ(config->indicator_style().offset_x(), -3);
  EXPECT_FALSE(config->indicator_style().has_width());
  ASSERT_EQ(config->character_form_rules_size(), 2);
  EXPECT_EQ(config->character_form_rules(0).group(), "ア");
  EXPECT_EQ(config->character_form_rules(0).preedit_character_form(),
            Config::HALF_WIDTH);
  EXPECT_EQ(config->character_form_rules(1).group(), "0");
  EXPECT_FALSE(config->has_incognito_mode());
}

TEST(ParseTomlConfigTest, NamesTheLineOfAnError) {
  ExpectErrorAt("use_zenz = true\nuse_zenzz = true\n", "2 行目");
  ExpectErrorAt("\nuse_zenz = 3\n", "2 行目");
  ExpectErrorAt("session_keymap = \"QWERTY\"\n", "1 行目");
  ExpectErrorAt("session_keymap = 1\n", "1 行目");
  ExpectErrorAt("suggestions_size = -1\n", "1 行目");
  ExpectErrorAt("indicator_style = 3\n", "1 行目");
  ExpectErrorAt("[indicator_style]\nwidth = 60\nshade = 1\n", "3 行目");
  ExpectErrorAt("\n\n[[character_form_rules]]\ngroup = 1\n", "4 行目");
  ExpectErrorAt("use_zenz = true\nuse_zenz = \n", "2 行目");
}

TEST(ParseTomlConfigTest, ChecksRangesLikeTheTextFile) {
  const absl::StatusOr<Config> delay = ParseTomlConfig(
      "use_zenz = true\n\nlive_conversion_delay_millisec = 3000\n");
  ASSERT_FALSE(delay.ok());
  EXPECT_EQ(delay.status().message(),
            "3 行目: live_conversion_delay_millisec は 0〜2000 の範囲で"
            "書いてください（3000 と書かれています）");
  ExpectErrorAt("[indicator_style]\nopacity_percent = 150\n", "2 行目");
  ExpectErrorAt("indicator_style.opacity_percent = 150\n", "1 行目");
  ExpectErrorAt("\n[ruby_style]\nfont_size = 200\n", "3 行目");
  ExpectErrorAt("zenz_timeout_millisec = 10\n", "1 行目");
  EXPECT_TRUE(ParseTomlConfig("zenz_timeout_millisec = 50\n").ok());
}

TEST(ParseTomlConfigTest, IgnoresGeneralConfig) {
  const absl::StatusOr<Config> config = ParseTomlConfig(
      "use_zenz = false\n"
      "[general_config]\n"
      "config_version = 1\n"
      "last_modified_time = 5\n");
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_FALSE(config->has_general_config());
  EXPECT_FALSE(config->use_zenz());
}

TEST(UpdateTomlConfigTest, WritesACleanFile) {
  Config config;
  config.set_use_zenz(false);
  config.set_suggestions_size(3);  // The default: left out.
  config.mutable_indicator_style()->set_zenz_color(0x0a0b0c);
  Config::CharacterFormRule* rule = config.add_character_form_rules();
  rule->set_group("ア");
  rule->set_preedit_character_form(Config::HALF_WIDTH);
  config.mutable_general_config()->set_config_version(1);
  const absl::StatusOr<std::string> text =
      UpdateTomlConfig("", config, Config());
  ASSERT_TRUE(text.ok()) << text.status();
  EXPECT_EQ(*text,
            "use_zenz = false\n"
            "\n"
            "[[character_form_rules]]\n"
            "group = \"ア\"\n"
            "preedit_character_form = \"HALF_WIDTH\"\n"
            "\n"
            "[indicator_style]\n"
            "zenz_color = 0x0a0b0c\n");
}

TEST(UpdateTomlConfigTest, ReadsBackWhatItWrites) {
  const Config defaults = config::ConfigHandler::DefaultConfig();
  Config config = defaults;
  config.set_use_zenz(!defaults.use_zenz());
  config.set_incognito_mode(!defaults.incognito_mode());
  config.set_suggestions_size(7);
  config.set_live_conversion_delay_millisec(300);
  config.set_session_keymap(Config::ATOK);
  config.set_preedit_method(Config::KANA);
  config.add_overlay_keymaps(Config::MSIME);
  config.set_direct_commit_characters("。\"\\");
  config.set_custom_roman_table("a\tあ\n\"b\"\tび\n'c'\n");
  config.mutable_information_list_config()->set_use_local_usage_dictionary(
      false);
  config.mutable_indicator_style()->set_placement(
      Config::IndicatorStyle::BELOW);
  config.mutable_indicator_style()->set_zenz_color(0x112233);
  config.mutable_indicator_style()->set_offset_x(-5);
  config.mutable_ruby_style()->set_font_size(14);
  config.mutable_ruby_style()->set_text_color(0x010203);
  config.mutable_candidate_style()->set_text_color(0xabcdef);
  config.clear_character_form_rules();
  Config::CharacterFormRule* rule = config.add_character_form_rules();
  rule->set_group("ア");
  rule->set_preedit_character_form(Config::HALF_WIDTH);
  rule = config.add_character_form_rules();
  rule->set_group("0");
  rule->set_conversion_character_form(Config::LAST_FORM);
  config.mutable_general_config()->set_last_modified_time(1);

  const absl::StatusOr<std::string> text =
      UpdateTomlConfig("", config, defaults);
  ASSERT_TRUE(text.ok()) << text.status();
  Config read = Read(*text, defaults);
  read.clear_general_config();
  config.clear_general_config();
  EXPECT_EQ(Print(read), Print(config)) << *text;
}

TEST(UpdateTomlConfigTest, KeepsMultilineTextByteForByte) {
  for (const absl::string_view table :
       {"a", "a\tあ\n", "\nleading\n", "x'", "it'''s\n", "a\r\nb\r\n",
        "\x01\x7f", "back\\slash \"\"\" end\"", "no newline at the end"}) {
    Config config;
    config.set_custom_roman_table(table);
    const absl::StatusOr<std::string> text =
        UpdateTomlConfig("", config, Config());
    ASSERT_TRUE(text.ok()) << text.status();
    const absl::StatusOr<Config> read = ParseTomlConfig(*text);
    ASSERT_TRUE(read.ok()) << read.status() << "\n" << *text;
    EXPECT_EQ(read->custom_roman_table(), table) << *text;
  }
}

constexpr absl::string_view kHandWritten =
    "# floatype settings\n"
    "use_zenz = false\n"
    "zenz_learning = true\n"
    "# how long to wait\n"
    "live_conversion_delay_millisec = 300 # ms\n"
    "suggest_added_words = true\n"
    "\n"
    "direct_commit_characters = \"、。\" # 句読点\n"
    "[indicator_style]\n"
    "# where\n"
    "placement = \"BELOW\"\n"
    "width = 60\n";

TEST(UpdateTomlConfigTest, ChangesOnlyTheLinesOfChangedFields) {
  const Config defaults;
  Config config = Read(kHandWritten, defaults);
  config.set_live_conversion_delay_millisec(400);
  config.set_direct_commit_characters("。");
  config.set_suggest_added_words(false);  // The default.
  config.set_mixed_english(true);
  config.mutable_indicator_style()->set_gap(9);
  config.mutable_ruby_style()->set_font_size(14);
  const absl::StatusOr<std::string> text =
      UpdateTomlConfig(kHandWritten, config, defaults);
  ASSERT_TRUE(text.ok()) << text.status();
  EXPECT_EQ(*text,
            "# floatype settings\n"
            "use_zenz = false\n"
            "zenz_learning = true\n"
            "# how long to wait\n"
            "live_conversion_delay_millisec = 400 # ms\n"
            "\n"
            "direct_commit_characters = \"。\" # 句読点\n"
            "mixed_english = true\n"
            "[indicator_style]\n"
            "# where\n"
            "placement = \"BELOW\"\n"
            "width = 60\n"
            "gap = 9\n"
            "\n"
            "[ruby_style]\n"
            "font_size = 14\n");
}

TEST(UpdateTomlConfigTest, LeavesTheFileAsItIsWithoutChanges) {
  // zenz_learning = true is the default, written in the file: it stays.
  const Config defaults = config::ConfigHandler::DefaultConfig();
  const Config config = Read(kHandWritten, defaults);
  const absl::StatusOr<std::string> text =
      UpdateTomlConfig(kHandWritten, config, defaults);
  ASSERT_TRUE(text.ok()) << text.status();
  EXPECT_EQ(*text, kHandWritten);
}

TEST(UpdateTomlConfigTest, AddsTopLevelKeysAboveTheFirstTable) {
  const Config defaults;
  constexpr absl::string_view kFile =
      "# floatype settings\n"
      "\n"
      "# the indicator\n"
      "[indicator_style]\n"
      "width = 60";  // No newline at the end.
  Config config = Read(kFile, defaults);
  config.set_use_zenz(false);
  config.mutable_indicator_style()->set_gap(9);
  const absl::StatusOr<std::string> text =
      UpdateTomlConfig(kFile, config, defaults);
  ASSERT_TRUE(text.ok()) << text.status();
  EXPECT_EQ(*text,
            "# floatype settings\n"
            "\n"
            "use_zenz = false\n"
            "\n"
            "# the indicator\n"
            "[indicator_style]\n"
            "width = 60\n"
            "gap = 9\n");
}

TEST(UpdateTomlConfigTest, RewritesTablesOfArraysAndDottedKeys) {
  const Config defaults;
  constexpr absl::string_view kFile =
      "use_zenz = false\n"
      "ruby_style.font_size = 14\n"
      "\n"
      "[[character_form_rules]]\n"
      "group = \"ア\"\n"
      "\n"
      "[indicator_style]\n"
      "width = 60\n";
  Config config = Read(kFile, defaults);
  config.mutable_ruby_style()->set_text_color(0x010203);
  config.mutable_character_form_rules(0)->set_group("A");
  const absl::StatusOr<std::string> text =
      UpdateTomlConfig(kFile, config, defaults);
  ASSERT_TRUE(text.ok()) << text.status();
  EXPECT_EQ(*text,
            "use_zenz = false\n"
            "\n"
            "[indicator_style]\n"
            "width = 60\n"
            "\n"
            "[[character_form_rules]]\n"
            "group = \"A\"\n"
            "\n"
            "[ruby_style]\n"
            "font_size = 14\n"
            "text_color = 0x010203\n");
}

TEST(UpdateTomlConfigTest, KeepsKeysInTheLastTableAboveNewTables) {
  const Config defaults;
  constexpr absl::string_view kFile = "[ruby_style]\nfont_size = 14\n";
  Config config = Read(kFile, defaults);
  config.mutable_ruby_style()->set_text_color(0x010203);
  config.mutable_indicator_style()->set_gap(9);
  config.add_character_form_rules()->set_group("A");
  const absl::StatusOr<std::string> text =
      UpdateTomlConfig(kFile, config, defaults);
  ASSERT_TRUE(text.ok()) << text.status();
  EXPECT_EQ(*text,
            "[ruby_style]\n"
            "font_size = 14\n"
            "text_color = 0x010203\n"
            "\n"
            "[[character_form_rules]]\n"
            "group = \"A\"\n"
            "\n"
            "[indicator_style]\n"
            "gap = 9\n");
}

TEST(UpdateTomlConfigTest, ReplacesAndRemovesMultilineValues) {
  const Config defaults;
  constexpr absl::string_view kFile =
      "# romaji\n"
      "custom_roman_table = '''\n"
      "a\tあ\n"
      "''' # mine\n"
      "# after\n"
      "use_zenz = false\n";
  Config config = Read(kFile, defaults);
  config.set_custom_roman_table("i\tい\nu\tう\n");
  const absl::StatusOr<std::string> changed =
      UpdateTomlConfig(kFile, config, defaults);
  ASSERT_TRUE(changed.ok()) << changed.status();
  EXPECT_EQ(*changed,
            "# romaji\n"
            "custom_roman_table = '''\n"
            "i\tい\n"
            "u\tう\n"
            "''' # mine\n"
            "# after\n"
            "use_zenz = false\n");

  config.clear_custom_roman_table();
  const absl::StatusOr<std::string> removed =
      UpdateTomlConfig(*changed, config, defaults);
  ASSERT_TRUE(removed.ok()) << removed.status();
  EXPECT_EQ(*removed, "# romaji\n# after\nuse_zenz = false\n");
}

TEST(UpdateTomlConfigTest, TakesUnsetRuleFieldsForTheirDefaults) {
  const Config defaults;
  // A comment in the block would be lost if the rules were rewritten.
  constexpr absl::string_view kFile =
      "[[character_form_rules]]\n# katakana\ngroup = \"ア\"\n";
  Config config = Read(kFile, defaults);
  config.mutable_character_form_rules(0)->set_preedit_character_form(
      Config::FULL_WIDTH);  // The default, now set.
  const absl::StatusOr<std::string> text =
      UpdateTomlConfig(kFile, config, defaults);
  ASSERT_TRUE(text.ok()) << text.status();
  EXPECT_EQ(*text, kFile);
}

TEST(UpdateTomlConfigTest, RefusesAFileThatDoesNotRead) {
  EXPECT_FALSE(UpdateTomlConfig("use_zenz = \n", Config(), Config()).ok());
  EXPECT_FALSE(UpdateTomlConfig("use_zenzz = true\n", Config(), Config()).ok());
}

}  // namespace
}  // namespace mozc::floatype
