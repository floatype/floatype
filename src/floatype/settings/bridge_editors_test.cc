#include <sys/stat.h>

#include <string>

#include "absl/strings/match.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "floatype/config/settings_file.h"
#include "floatype/settings/bridge.h"
#include "floatype/zenz/zenz_memory.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

std::string Take(char* text) {
  std::string out = text;
  FloatypeSettingsFree(text);
  return out;
}

class BridgeZenzMemoryTest : public testing::TestWithTempUserProfile {};

TEST_F(BridgeZenzMemoryTest, ListsAndForgetsTheLearningData) {
  ZenzMemory::TurnedDown().Add("かんじ", "完治");
  ZenzMemory::Accepted().Add("きかい", "機械");
  ZenzMemory::Accepted().Add("よい", "良い");
  EXPECT_EQ(Take(FloatypeSettingsZenzMemory()),
            "0\tかんじ\t完治\n2\tよい\t良い\n2\tきかい\t機械\n")
      << "by kind, the newest first";

  FloatypeSettingsZenzForget(2, "きかい", "機械");
  EXPECT_FALSE(ZenzMemory::Accepted().Contains("きかい", "機械"));
  EXPECT_TRUE(ZenzMemory::Accepted().Contains("よい", "良い"));
  FloatypeSettingsZenzForget(9, "よい", "良い");  // No such kind.

  FloatypeSettingsZenzForgetAll();
  EXPECT_EQ(Take(FloatypeSettingsZenzMemory()), "");
}

class BridgeEnglishWordsTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    // A folder of its own, never the user's ~/.config/floatype.
    SetUserFilesDir(SystemUtil::GetUserProfileDirectory(),
                    /*report_errors=*/false);
    path_ = UserFilePath(kEnglishWordsFile);
  }
  void TearDown() override { SetUserFilesDir("", false); }

  std::string Contents() { return FileUtil::GetContents(path_).value_or(""); }

  std::string path_;
};

TEST_F(BridgeEnglishWordsTest, StartsANewFileWithItsHeader) {
  const std::string listed = Take(FloatypeSettingsEnglishWords());
  EXPECT_TRUE(absl::StrContains(listed, "page\t1\t0\n")) << "a preset word";
  EXPECT_EQ(FloatypeSettingsEditEnglishWords("quokka", 0), nullptr);
  EXPECT_TRUE(absl::StartsWith(Contents(), "# floatype: "));
  EXPECT_TRUE(absl::EndsWith(Contents(), "\nquokka\n"));
  EXPECT_TRUE(absl::StartsWith(Take(FloatypeSettingsEnglishWords()),
                               "quokka\t1\t2\n"))
      << "the file's words first, from the file alone";
}

TEST_F(BridgeEnglishWordsTest, TakesOutAndForgets) {
  Take(FloatypeSettingsEnglishWords());
  EXPECT_EQ(FloatypeSettingsEditEnglishWords("page\nquokka", 1), nullptr);
  const std::string listed = Take(FloatypeSettingsEnglishWords());
  EXPECT_TRUE(absl::StrContains(listed, "page\t0\t1\n")) << listed;
  EXPECT_TRUE(absl::StrContains(listed, "quokka\t0\t2\n")) << listed;
  EXPECT_EQ(FloatypeSettingsEditEnglishWords("page", 2), nullptr);
  EXPECT_TRUE(absl::StrContains(Take(FloatypeSettingsEnglishWords()),
                                "page\t1\t0\n"))
      << "as the presets say again";
  EXPECT_FALSE(absl::StrContains(Contents(), "page"));
}

TEST_F(BridgeEnglishWordsTest, RefusesAWordTheFileWouldLeaveOut) {
  Take(FloatypeSettingsEnglishWords());
  char* why = FloatypeSettingsEditEnglishWords("ab", 0);
  ASSERT_NE(why, nullptr);
  EXPECT_TRUE(absl::StrContains(Take(why), "「ab」は使えません"));
  EXPECT_FALSE(FileUtil::FileExists(path_).ok()) << "nothing written";
}

TEST_F(BridgeEnglishWordsTest, NeverWritesOverAFileChangedSinceRead) {
  ASSERT_TRUE(FileUtil::SetContents(path_, "quokka\n").ok());
  Take(FloatypeSettingsEnglishWords());
  ASSERT_TRUE(FileUtil::SetContents(path_, "quokka\nslack\n").ok());
  char* why = FloatypeSettingsEditEnglishWords("zoom", 0);
  ASSERT_NE(why, nullptr);
  EXPECT_TRUE(absl::StrContains(Take(why), "書き換えられた"));
  EXPECT_EQ(Contents(), "quokka\nslack\n");
  EXPECT_EQ(FloatypeSettingsEditEnglishWords("zoom", 0), nullptr)
      << "read again, so the next edit goes through";
  EXPECT_EQ(Contents(), "quokka\nslack\nzoom\n");
}

TEST_F(BridgeEnglishWordsTest, NeverWritesOverAFileItCannotRead) {
  ASSERT_TRUE(FileUtil::SetContents(path_, "quokka\n").ok());
  ASSERT_EQ(chmod(path_.c_str(), 0), 0);
  Take(FloatypeSettingsEnglishWords());
  char* why = FloatypeSettingsEditEnglishWords("zoom", 0);
  chmod(path_.c_str(), 0600);
  ASSERT_NE(why, nullptr);
  EXPECT_TRUE(absl::StrContains(Take(why), "読めない"));
  EXPECT_EQ(Contents(), "quokka\n");
}

}  // namespace
}  // namespace mozc::floatype
