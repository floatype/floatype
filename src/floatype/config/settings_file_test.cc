// The settings file through Mozc's ConfigHandler, as the converter (writable)
// and the IME client (read only) use it.

#include "floatype/config/settings_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/clock_mock.h"
#include "base/config_file_stream.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "config/config_handler.h"
#include "protocol/config.pb.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

using config::Config;
using config::ConfigHandler;

class SettingsFileTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    Clock::SetClockForUnitTest(&clock_);
    profile_ = SystemUtil::GetUserProfileDirectory();
    dir_ = FileUtil::JoinPath({profile_, ".config", "floatype"});
    path_ = FileUtil::JoinPath(dir_, "config.toml");
    ConfigHandler::Reload();
  }
  void TearDown() override {
    SetSettingsPath("", false, false);
    ConfigHandler::Reload();
    Clock::SetClockForUnitTest(nullptr);
  }

  // As the converter's and the IME client's main()s do.
  void Start(bool writable) {
    SetSettingsPath(path_, false, writable);
    ConfigHandler::Reload();
  }

  void WriteFile(const std::string& text) {
    ASSERT_OK(FileUtil::CreateDirectory(FileUtil::Dirname(dir_)));
    ASSERT_OK(FileUtil::CreateDirectory(dir_));
    ASSERT_OK(FileUtil::SetContents(path_, text));
  }

  std::string ReadFile(const std::string& path) {
    absl::StatusOr<std::string> text = FileUtil::GetContents(path);
    EXPECT_OK(text);
    return text.value_or("");
  }

  // What config1.db holds.
  Config Stored() {
    Config config;
    EXPECT_TRUE(config.ParseFromString(
        ReadFile(FileUtil::JoinPath(profile_, "config1.db"))));
    return config;
  }

  std::shared_ptr<const Config> Current() {
    return ConfigHandler::GetSharedConfig();
  }

  ClockMock clock_{absl::FromUnixSeconds(1000)};
  std::string profile_;
  std::string dir_;
  std::string path_;
};

// config1.db as an older floatype left it, and the old text file on it.
class MigrationTest : public SettingsFileTest {
 protected:
  void SetUp() override {
    SettingsFileTest::SetUp();
    Config stored = ConfigHandler::DefaultConfig();
    stored.mutable_general_config()->set_config_version(1);
    stored.set_incognito_mode(true);
    stored.set_suggestions_size(5);
    db_ = stored.SerializeAsString();
    ASSERT_TRUE(ConfigFileStream::AtomicUpdate("user://config1.db", db_));
    ASSERT_OK(FileUtil::CreateDirectory(FileUtil::Dirname(dir_)));
    ASSERT_OK(FileUtil::CreateDirectory(dir_));
    ASSERT_OK(FileUtil::SetContents(FileUtil::JoinPath(dir_, "config.txtpb"),
                                    kOldFile));
  }

  void ExpectMerged() {
    EXPECT_TRUE(Current()->incognito_mode());
    EXPECT_EQ(Current()->suggestions_size(), 7);
    EXPECT_FALSE(Current()->use_zenz());
    EXPECT_EQ(ReadFile(FileUtil::JoinPath(profile_, "config1.db")), db_);
    EXPECT_EQ(ReadFile(FileUtil::JoinPath(dir_, "config.txtpb")), kOldFile);
  }

  static constexpr char kOldFile[] = "suggestions_size: 7\nuse_zenz: false\n";
  std::string db_;
};

TEST_F(MigrationTest, WritesWhatDiffersFromTheDefaults) {
  Start(/*writable=*/true);
  ExpectMerged();
  EXPECT_EQ(ReadFile(path_),
            "incognito_mode = true\n"
            "suggestions_size = 7\n"
            "use_zenz = false\n");
}

TEST_F(MigrationTest, ABrokenFileKeepsTheMovedSettings) {
  Start(/*writable=*/true);
  WriteFile("broken\n");
  ConfigHandler::Reload();
  ExpectMerged();
}

TEST_F(MigrationTest, AReaderWritesNothing) {
  Start(/*writable=*/false);
  ExpectMerged();
  EXPECT_FALSE(FileUtil::FileExists(path_).ok());
}

TEST_F(SettingsFileTest, AFreshStartWritesAnEmptyFile) {
  Start(/*writable=*/true);
  EXPECT_EQ(ReadFile(path_), "");
  EXPECT_EQ(Current()->character_form_rules_size(),
            ConfigHandler::DefaultConfig().character_form_rules_size());
}

TEST_F(SettingsFileTest, UnsetFieldsKeepTheirDefaults) {
  WriteFile(
      "# mine\n"
      "suggestions_size = 6\n"
      "[indicator_style]\n"
      "width = 60\n");
  Start(/*writable=*/true);
  EXPECT_EQ(Current()->suggestions_size(), 6);
  EXPECT_EQ(Current()->indicator_style().width(), 60);
  EXPECT_EQ(Current()->use_zenz(), ConfigHandler::DefaultConfig().use_zenz());
  EXPECT_EQ(Current()->character_form_rules_size(),
            ConfigHandler::DefaultConfig().character_form_rules_size());
  EXPECT_EQ(Stored().suggestions_size(), 6) << "config1.db keeps a copy";
}

TEST_F(SettingsFileTest, TheSettingsScreenChangesOnlyItsLine) {
  constexpr char kFile[] =
      "# my settings\n"
      "use_zenz = false # off\n"
      "\n"
      "# how many\n"
      "suggestions_size = 6\n";
  WriteFile(kFile);
  Start(/*writable=*/true);

  Config shown = ConfigHandler::GetCopiedConfig();
  shown.set_suggestions_size(8);
  ConfigHandler::SetConfig(shown);
  EXPECT_EQ(ReadFile(path_),
            "# my settings\n"
            "use_zenz = false # off\n"
            "\n"
            "# how many\n"
            "suggestions_size = 8\n");
  EXPECT_EQ(Current()->suggestions_size(), 8);
  EXPECT_FALSE(Current()->use_zenz());
  EXPECT_EQ(Stored().suggestions_size(), 8);
}

TEST_F(SettingsFileTest, ResettingTheSettingsEmptiesTheFile) {
  // As resetting settings one by one left it.
  WriteFile(
      "\nmixed_english = true\n\n[indicator_style]\n\n[candidate_style]\n");
  Start(/*writable=*/true);
  ASSERT_TRUE(Current()->mixed_english());

  ConfigHandler::SetConfig(ConfigHandler::DefaultConfig());
  EXPECT_EQ(ReadFile(path_), "");
  EXPECT_FALSE(Current()->mixed_english());
}

TEST_F(SettingsFileTest, ASymbolicLinkStaysOne) {
  // A dotfiles manager links config.toml to its own copy.
  const std::string target = FileUtil::JoinPath(profile_, "dotfiles.toml");
  ASSERT_OK(FileUtil::SetContents(target, "suggestions_size = 6\n"));
  ASSERT_OK(FileUtil::CreateDirectory(FileUtil::Dirname(dir_)));
  ASSERT_OK(FileUtil::CreateDirectory(dir_));
  ASSERT_EQ(symlink(target.c_str(), path_.c_str()), 0);
  Start(/*writable=*/true);

  Config shown = ConfigHandler::GetCopiedConfig();
  shown.set_suggestions_size(8);
  ConfigHandler::SetConfig(shown);
  struct stat info;
  ASSERT_EQ(lstat(path_.c_str(), &info), 0);
  EXPECT_TRUE(S_ISLNK(info.st_mode));
  EXPECT_EQ(ReadFile(target), "suggestions_size = 8\n");
}

TEST_F(SettingsFileTest, OurOwnWriteIsNotAChange) {
  WriteFile("suggestions_size = 6\n");
  Start(/*writable=*/true);
  clock_.Advance(absl::Seconds(1));
  Config shown = ConfigHandler::GetCopiedConfig();
  shown.set_incognito_mode(true);
  ConfigHandler::SetConfig(shown);
  clock_.Advance(absl::Seconds(1));
  EXPECT_FALSE(SettingsFileChanged());
}

TEST_F(SettingsFileTest, AHandEditTakesEffectASecondLater) {
  WriteFile("suggestions_size = 6\n");
  Start(/*writable=*/true);

  WriteFile("suggestions_size = 4\nuse_zenz = false\n");
  EXPECT_FALSE(SettingsFileChanged()) << "looks at most once a second";
  clock_.Advance(absl::Seconds(1));
  EXPECT_TRUE(SettingsFileChanged());
  ConfigHandler::Reload();
  EXPECT_EQ(Current()->suggestions_size(), 4);
  EXPECT_FALSE(Current()->use_zenz());
  EXPECT_EQ(Stored().suggestions_size(), 4);
}

TEST_F(SettingsFileTest, SavingTheSameSettingsAfterAnEditWritesThem) {
  WriteFile("suggestions_size = 6\n");
  Start(/*writable=*/true);
  Config shown = ConfigHandler::GetCopiedConfig();
  shown.set_suggestions_size(5);
  ConfigHandler::SetConfig(shown);

  WriteFile("suggestions_size = 7\n");
  ConfigHandler::Reload();
  ASSERT_EQ(Current()->suggestions_size(), 7);
  // The same settings again, as when the screen saves twice.
  ConfigHandler::SetConfig(shown);
  EXPECT_EQ(ReadFile(path_), "suggestions_size = 5\n");
  EXPECT_EQ(Current()->suggestions_size(), 5);
}

TEST_F(SettingsFileTest, ABrokenFileKeepsTheLastGoodSettings) {
  WriteFile("suggestions_size = 6\n");
  Start(/*writable=*/true);
  ASSERT_EQ(Current()->suggestions_size(), 6);

  constexpr char kBroken[] = "suggestions_size = 6\nbroken\n";
  WriteFile(kBroken);
  ConfigHandler::Reload();
  EXPECT_EQ(Current()->suggestions_size(), 6);

  // Started again: config1.db has them.
  Start(/*writable=*/true);
  EXPECT_EQ(Current()->suggestions_size(), 6);

  // The settings screen cannot save into it.
  Config shown = ConfigHandler::GetCopiedConfig();
  shown.set_suggestions_size(8);
  ConfigHandler::SetConfig(shown);
  EXPECT_EQ(ReadFile(path_), kBroken);
  EXPECT_EQ(Current()->suggestions_size(), 6);
  EXPECT_EQ(Stored().suggestions_size(), 6);
}

TEST_F(SettingsFileTest, WithoutAPathOnlyConfig1DbIsUsed) {
  WriteFile("suggestions_size = 6\n");
  ConfigHandler::Reload();
  EXPECT_EQ(Current()->suggestions_size(), 3);
  Config config = ConfigHandler::GetCopiedConfig();
  config.set_suggestions_size(8);
  ConfigHandler::SetConfig(config);
  EXPECT_EQ(ReadFile(path_), "suggestions_size = 6\n");
  EXPECT_EQ(Stored().suggestions_size(), 8);
}

TEST_F(SettingsFileTest, NoticesAFileSwappedInWithTheSameTimeAndSize) {
  // As a dotfile manager does: another file, same length, same time.
  const std::string other = path_ + ".new";
  WriteFile("zenz_delay_millisec = 228\n");
  ASSERT_OK(FileUtil::SetContents(other, "zenz_delay_millisec = 300\n"));
  struct stat original;
  ASSERT_EQ(stat(path_.c_str(), &original), 0);
  const struct timespec times[2] = {original.st_atimespec,
                                    original.st_mtimespec};
  ASSERT_EQ(utimensat(AT_FDCWD, other.c_str(), times, 0), 0);
  Start(/*writable=*/false);
  ASSERT_EQ(Current()->zenz_delay_millisec(), 228);

  ASSERT_OK(FileUtil::AtomicRename(other, path_));
  clock_.Advance(absl::Seconds(1));
  EXPECT_TRUE(SettingsFileChanged());
  ConfigHandler::Reload();
  EXPECT_EQ(Current()->zenz_delay_millisec(), 300);
}

}  // namespace
}  // namespace mozc::floatype
