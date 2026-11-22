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

TEST(ConfigLinesTest, RefusesWhatTheFileWould) {
  EXPECT_FALSE(ApplyConfigLines(config::Config(), "no_such_field = 1\n").ok());
  EXPECT_FALSE(
      ApplyConfigLines(config::Config(), "preedit_method = \"NOPE\"\n").ok());
}

}  // namespace
}  // namespace mozc::floatype
