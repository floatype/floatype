#include "floatype/config/text_config.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/clock_mock.h"
#include "base/file_util.h"
#include "base/protobuf/text_format.h"
#include "base/system_util.h"
#include "protocol/config.pb.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

using config::Config;

TEST(ParseTextConfigTest, ReadsTheFieldsWritten) {
  const absl::StatusOr<Config> config = ParseTextConfig(
      "# comment\n"
      "live_conversion_delay_millisec: 300\n"
      "indicator_style { placement: BELOW zenz_color: 0x112233 }\n");
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->live_conversion_delay_millisec(), 300);
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
  EXPECT_EQ(config.suggestions_size(), 5) << "left to the settings screen";
  ASSERT_EQ(config.character_form_rules_size(), 1) << "replaced, not appended";
  EXPECT_EQ(config.character_form_rules(0).group(), "A");
}

TEST(OverlayTest, PinnedValuesNeverReachTheStoredSettings) {
  Config stored;
  stored.set_suggestions_size(5);
  stored.add_character_form_rules()->set_group("ア");
  const Config overlay = *ParseTextConfig(
      "use_zenz: false\n"
      "suggestions_size: 7\n"
      "character_form_rules { group: \"A\" }\n");
  // What the settings screen sends back: the effective settings, changed.
  Config edited = stored;
  ApplyOverlay(overlay, edited);
  edited.set_incognito_mode(true);

  RestoreOverlaidFields(overlay, stored, edited);
  EXPECT_FALSE(edited.has_use_zenz());
  EXPECT_EQ(edited.suggestions_size(), 5);
  ASSERT_EQ(edited.character_form_rules_size(), 1);
  EXPECT_EQ(edited.character_form_rules(0).group(), "ア");
  EXPECT_TRUE(edited.incognito_mode()) << "the user's own change stays";
}

// The example with every "# key: value" line turned on.
std::string ExampleTurnedOn() {
  const absl::StatusOr<std::string> example =
      FileUtil::GetContents(testing::GetSourceFileOrDie(
          {"floatype", "config", "config.example.txtpb"}));
  EXPECT_TRUE(example.ok());
  std::vector<std::string> lines;
  for (const absl::string_view line : absl::StrSplit(*example, '\n')) {
    if (absl::StartsWith(line, "# ")) {
      lines.emplace_back(line.substr(2));
    }
  }
  return absl::StrJoin(lines, "\n");
}

// Expects every scalar field set in `example` to hold its default value.
void ExpectDefaults(const google::protobuf::Message& example,
                    absl::string_view skipped = "") {
  const google::protobuf::Descriptor& descriptor = *example.GetDescriptor();
  const google::protobuf::Message& defaults =
      *example.GetReflection()->GetMessageFactory()->GetPrototype(&descriptor);
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor.field(i);
    if (field->is_repeated() || field->name() == skipped ||
        field->cpp_type() ==
            google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE ||
        !example.GetReflection()->HasField(example, field)) {
      continue;
    }
    std::string written;
    std::string fallback;
    google::protobuf::TextFormat::PrintFieldValueToString(example, field, -1,
                                                          &written);
    google::protobuf::TextFormat::PrintFieldValueToString(defaults, field, -1,
                                                          &fallback);
    EXPECT_EQ(written, fallback) << field->name();
  }
}

// Expects every field numbered from `first` on to be set in `example`.
void ExpectListed(const google::protobuf::Message& example, int first) {
  const google::protobuf::Descriptor& descriptor = *example.GetDescriptor();
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor.field(i);
    if (field->number() >= first) {
      EXPECT_TRUE(example.GetReflection()->HasField(example, field))
          << field->name() << " is missing from the example";
    }
  }
}

TEST(ExampleTest, ListsEveryFloatypeFieldWithItsDefault) {
  const absl::StatusOr<Config> example = ParseTextConfig(ExampleTurnedOn());
  ASSERT_TRUE(example.ok()) << example.status();
  ExpectListed(*example, 1001);
  ExpectListed(example->indicator_style(), 1);
  // session_keymap's default, NONE, stands for the platform's: KOTOERI.
  ExpectDefaults(*example, "session_keymap");
  ExpectDefaults(example->indicator_style());
}

class TextConfigFileTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    Clock::SetClockForUnitTest(&clock_);
    path_ = FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(),
                               "config.txtpb");
  }
  void TearDown() override {
    SetTextConfigPath("", false);
    Clock::SetClockForUnitTest(nullptr);
  }

  ClockMock clock_{absl::FromUnixSeconds(1000)};
  std::string path_;
};

TEST_F(TextConfigFileTest, ReadsTheFileAndKeepsTheLastGoodOne) {
  EXPECT_EQ(TextConfigOverlay().ByteSizeLong(), 0) << "off until a path is set";
  ASSERT_OK(FileUtil::SetContents(path_, "use_zenz: false\n"));
  SetTextConfigPath(path_, false);
  EXPECT_FALSE(TextConfigOverlay().use_zenz());

  ASSERT_OK(FileUtil::SetContents(path_, "use_zenz: false\nbroken\n"));
  EXPECT_FALSE(TextConfigChanged()) << "looks at most once a second";
  clock_.Advance(absl::Seconds(1));
  EXPECT_TRUE(TextConfigChanged());
  ReloadTextConfig();
  EXPECT_TRUE(TextConfigOverlay().has_use_zenz()) << "the last good settings";

  ASSERT_OK(FileUtil::Unlink(path_));
  ReloadTextConfig();
  EXPECT_EQ(TextConfigOverlay().ByteSizeLong(), 0);
}

TEST_F(TextConfigFileTest, NoticesAFileSwappedInWithTheSameTimeAndSize) {
  // As a dotfile manager does: another file, same length, same time.
  const std::string other = path_ + ".new";
  ASSERT_OK(FileUtil::SetContents(path_, "zenz_delay_millisec: 228\n"));
  ASSERT_OK(FileUtil::SetContents(other, "zenz_delay_millisec: 300\n"));
  struct stat original;
  ASSERT_EQ(stat(path_.c_str(), &original), 0);
  const struct timespec times[2] = {original.st_atimespec,
                                    original.st_mtimespec};
  ASSERT_EQ(utimensat(AT_FDCWD, other.c_str(), times, 0), 0);
  SetTextConfigPath(path_, false);
  ASSERT_EQ(TextConfigOverlay().zenz_delay_millisec(), 228);

  ASSERT_OK(FileUtil::AtomicRename(other, path_));
  clock_.Advance(absl::Seconds(1));
  EXPECT_TRUE(TextConfigChanged());
  ReloadTextConfig();
  EXPECT_EQ(TextConfigOverlay().zenz_delay_millisec(), 300);
}

}  // namespace
}  // namespace mozc::floatype
