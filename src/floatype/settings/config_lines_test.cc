#include "floatype/settings/config_lines.h"

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "config/config_handler.h"
#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

TEST(ConfigLinesTest, WritesEveryFieldInEffect) {
  const std::string lines = ConfigLines(config::Config());
  EXPECT_TRUE(absl::StrContains(lines, "preedit_method = \"ROMAN\"\n"))
      << "the default where unset";
  EXPECT_TRUE(absl::StrContains(lines, "zenz_learning = true\n"));
  EXPECT_TRUE(absl::StrContains(lines, "indicator_style."))
      << "nested fields as dotted keys";
  EXPECT_FALSE(absl::StrContains(lines, "general_config"));
}

TEST(ConfigLinesTest, ReadsBackWhatItWrites) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_zenz_learning(false);
  config.set_punctuation_method(config::Config::COMMA_PERIOD);
  const std::string lines = ConfigLines(config);
  const absl::StatusOr<config::Config> read =
      ApplyConfigLines(config::Config(), lines);
  ASSERT_TRUE(read.ok()) << read.status();
  EXPECT_EQ(ConfigLines(*read), lines);
}

TEST(ConfigLinesTest, LeavesOutAColorLeftToTheTheme) {
  config::Config config;
  config.mutable_suggest_style()->set_text_color(0x123456);
  const std::string lines = ConfigLines(config);
  EXPECT_FALSE(absl::StrContains(lines, "candidate_style.text_color"));
  EXPECT_TRUE(absl::StrContains(lines, "suggest_style.text_color = 1193046\n"));
  EXPECT_TRUE(absl::StrContains(lines, "indicator_style.zenz_color = "))
      << "a color with a default is written";
  const absl::StatusOr<config::Config> read =
      ApplyConfigLines(config::Config(), lines);
  ASSERT_TRUE(read.ok()) << read.status();
  EXPECT_FALSE(read->candidate_style().has_text_color());
  EXPECT_EQ(read->suggest_style().text_color(), 0x123456);
}

TEST(ConfigLinesTest, ChangesOnlyTheFieldsGiven) {
  config::Config loaded;
  loaded.set_use_live_conversion(false);
  const absl::StatusOr<config::Config> changed = ApplyConfigLines(
      loaded,
      "zenz_learning = false\nsymbol_method = \"SQUARE_BRACKET_SLASH\"\n");
  ASSERT_TRUE(changed.ok()) << changed.status();
  EXPECT_TRUE(changed->has_zenz_learning());
  EXPECT_FALSE(changed->zenz_learning());
  EXPECT_EQ(changed->symbol_method(), config::Config::SQUARE_BRACKET_SLASH);
  EXPECT_FALSE(changed->use_live_conversion()) << "set before, stays";
  EXPECT_FALSE(changed->has_preedit_method()) << "unset before, stays unset";
}

TEST(ConfigLinesTest, WritesARepeatedFieldAsOneLine) {
  config::Config config;
  config::Config::CharacterFormRule* rule = config.add_character_form_rules();
  rule->set_group("0");
  rule->set_preedit_character_form(config::Config::HALF_WIDTH);
  config.add_character_form_rules()->set_group("ア");
  config.add_overlay_keymaps(config::Config::ATOK);
  const std::string lines = ConfigLines(config);
  EXPECT_TRUE(absl::StrContains(
      lines,
      "character_form_rules = [{group = \"0\", preedit_character_form = "
      "\"HALF_WIDTH\", conversion_character_form = \"FULL_WIDTH\"}, "
      "{group = \"ア\", preedit_character_form = \"FULL_WIDTH\", "
      "conversion_character_form = \"FULL_WIDTH\"}]\n"))
      << lines;
  EXPECT_TRUE(absl::StrContains(lines, "overlay_keymaps = [\"ATOK\"]\n"));
  const absl::StatusOr<config::Config> read =
      ApplyConfigLines(config::Config(), lines);
  ASSERT_TRUE(read.ok()) << read.status();
  EXPECT_EQ(ConfigLines(*read), lines);
}

TEST(ConfigLinesTest, ReadsAndSetsTheNumberForm) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  EXPECT_EQ(NumberForm(config), 0) << "Floatype's default, half width";
  ASSERT_TRUE(SetNumberForm(2, config));
  EXPECT_EQ(NumberForm(config), 2);
  ASSERT_TRUE(SetNumberForm(1, config));
  EXPECT_EQ(NumberForm(config), 1);

  // The other groups' rules are as they were.
  config::Config others = config::ConfigHandler::DefaultConfig();
  ASSERT_EQ(config.character_form_rules_size(),
            others.character_form_rules_size());
  for (int i = 0; i < config.character_form_rules_size(); ++i) {
    if (config.character_form_rules(i).group() != "0") {
      EXPECT_EQ(config.character_form_rules(i).SerializeAsString(),
                others.character_form_rules(i).SerializeAsString());
    }
  }

  // A pair the choice does not offer, and no digits' rule.
  for (config::Config::CharacterFormRule& rule :
       *config.mutable_character_form_rules()) {
    if (rule.group() == "0") {
      rule.set_preedit_character_form(config::Config::HALF_WIDTH);
      rule.set_conversion_character_form(config::Config::LAST_FORM);
    }
  }
  EXPECT_EQ(NumberForm(config), 3);
  config::Config none;
  none.add_character_form_rules()->set_group("ア");
  EXPECT_EQ(NumberForm(none), 3);
  EXPECT_FALSE(SetNumberForm(0, none));
  EXPECT_EQ(none.character_form_rules_size(), 1);
  EXPECT_FALSE(SetNumberForm(3, config)) << "the 4th choice sets nothing";
}

TEST(ConfigLinesTest, RefusesWhatTheFileWould) {
  EXPECT_FALSE(ApplyConfigLines(config::Config(), "no_such_field = 1\n").ok());
  EXPECT_FALSE(
      ApplyConfigLines(config::Config(), "preedit_method = \"NOPE\"\n").ok());
}

}  // namespace
}  // namespace mozc::floatype
