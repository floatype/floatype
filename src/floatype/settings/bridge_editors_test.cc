#include <sys/stat.h>

#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
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

  ZenzMemory::Pinned().Add("さいとう", "齋藤");
  FloatypeSettingsZenzForgetAll();
  EXPECT_EQ(Take(FloatypeSettingsZenzMemory()), "4\tさいとう\t齋藤\n")
      << "a pinned word stays";
  FloatypeSettingsZenzForget(4, "さいとう", "齋藤");
  EXPECT_EQ(Take(FloatypeSettingsZenzMemory()), "");
}

TEST(BridgeCharacterFormsTest, ReadsAndWritesTheTable) {
  const std::string rows = Take(FloatypeSettingsCharacterForms(""));
  EXPECT_TRUE(absl::StartsWith(rows, "ア\tFULL_WIDTH\tFULL_WIDTH\n"))
      << "the default table for none: " << rows;
  EXPECT_TRUE(absl::StrContains(rows, "0\tHALF_WIDTH\tHALF_WIDTH\n"));

  const std::string value = Take(FloatypeSettingsWithCharacterForms(
      "ア\tFULL_WIDTH\tHALF_WIDTH\n0\tFULL_WIDTH\tLAST_FORM\n"));
  EXPECT_EQ(value,
            "[{group = \"ア\", preedit_character_form = \"FULL_WIDTH\", "
            "conversion_character_form = \"HALF_WIDTH\"}, {group = \"0\", "
            "preedit_character_form = \"FULL_WIDTH\", "
            "conversion_character_form = \"LAST_FORM\"}]");
  EXPECT_EQ(Take(FloatypeSettingsCharacterForms(
                absl::StrCat("character_form_rules = ", value).c_str())),
            "ア\tFULL_WIDTH\tHALF_WIDTH\n0\tFULL_WIDTH\tLAST_FORM\n");
  EXPECT_EQ(FloatypeSettingsWithCharacterForms("ア\tWIDE\tHALF_WIDTH\n"),
            nullptr);
  EXPECT_EQ(FloatypeSettingsWithCharacterForms(""), nullptr);
}

TEST(BridgeRomanTableTest, ReadsEditsAndKeepsTheDefaultAsNone) {
  const std::string defaults = Take(FloatypeSettingsRomanRows(nullptr));
  EXPECT_TRUE(absl::StrContains(defaults, "ka\tか\t\n")) << "Mozc's table";
  EXPECT_EQ(Take(FloatypeSettingsRomanTable("")), defaults)
      << "the default when the settings have none";
  EXPECT_EQ(Take(FloatypeSettingsRomanTableValue(defaults.c_str())), "\"\"")
      << "the default table is saved as none";

  EXPECT_EQ(Take(FloatypeSettingsRomanRows("a\tあ\n\nkk\tっ\tk\nx\r\n")),
            "a\tあ\t\nkk\tっ\tk\n")
      << "a blank line and a line of one field left out";
  const char kRows[] = "a\tあ\t\nkk\tっ\tk\n\tい\t\nq\t\t\n";
  EXPECT_EQ(Take(FloatypeSettingsRomanTableText(kRows)), "a\tあ\nkk\tっ\tk\n")
      << "rows without an input, or without output and next, left out";
  const std::string value = Take(FloatypeSettingsRomanTableValue(kRows));
  EXPECT_EQ(value, "\"a\\tあ\\nkk\\tっ\\tk\\n\"");
  EXPECT_EQ(Take(FloatypeSettingsRomanTable(
                absl::StrCat("custom_roman_table = ", value).c_str())),
            "a\tあ\t\nkk\tっ\tk\n");
  EXPECT_EQ(FloatypeSettingsRomanTableValue("\tい\t\n"), nullptr);
}

TEST(BridgeKeymapTest, NamesEveryModeAndCommandInJapanese) {
  const std::string names = Take(FloatypeSettingsKeymapNames());
  int commands = 0;
  for (const absl::string_view line :
       absl::StrSplit(names, '\n', absl::SkipEmpty())) {
    const std::vector<absl::string_view> fields = absl::StrSplit(line, '\t');
    ASSERT_EQ(fields.size(), 3);
    EXPECT_NE(fields[1], fields[2]) << "no Japanese name: " << fields[1];
    commands += fields[0] == "command";
  }
  EXPECT_GT(commands, 30);
  EXPECT_FALSE(absl::StrContains(names, "\tIMEOn\t")) << "not on a Mac";
}

TEST(BridgeKeymapTest, KeepsTheRowsItDoesNotShow) {
  const char kTable[] =
      "status\tkey\tcommand\n"
      "Composition\tCtrl a\tMoveCursorToBeginning\n"
      "# a comment\n"
      "Precomposition\tKanji\tIMEOn\n"
      "Composition\tShift Space\tCommit|IMEOff\n"
      "Conversion\tSpace\tSegmentFocusRight\n";
  const std::string rows = Take(FloatypeSettingsKeymapRows(kTable));
  EXPECT_EQ(rows,
            "Composition\tCtrl a\tMoveCursorToBeginning\t1\n"
            "Conversion\tSpace\tSegmentFocusRight\t1\n"
            "Precomposition\tKanji\tIMEOn\t0\n"
            "Composition\tShift Space\tCommit|IMEOff\t0\n");
  EXPECT_EQ(Take(FloatypeSettingsKeymapText(rows.c_str())),
            "status\tkey\tcommand\n"
            "Composition\tCtrl a\tMoveCursorToBeginning\n"
            "Conversion\tSpace\tSegmentFocusRight\n"
            "Precomposition\tKanji\tIMEOn\n"
            "Composition\tShift Space\tCommit|IMEOff\n");
  const std::string value = Take(FloatypeSettingsKeymapValue(rows.c_str()));
  EXPECT_EQ(Take(FloatypeSettingsKeymap(
                absl::StrCat("session_keymap = \"CUSTOM\"\n"
                             "custom_keymap_table = ",
                             value, "\n")
                    .c_str())),
            rows)
      << "read back as written";
  EXPECT_EQ(FloatypeSettingsKeymapValue(
                "Precomposition\tKanji\tIMEOn\t0\n"),
            nullptr)
      << "no row shown: an empty table to Mozc";
}

TEST(BridgeKeymapTest, OpensAPresetAsItsFile) {
  const std::string msime = Take(FloatypeSettingsKeymapPreset("MSIME"));
  EXPECT_TRUE(absl::StrContains(msime, "\t1\n"));
  EXPECT_EQ(Take(FloatypeSettingsKeymap("session_keymap = \"MSIME\"\n")),
            msime);
  EXPECT_EQ(Take(FloatypeSettingsKeymapPreset("CUSTOM")), "");
}

TEST(BridgeKeymapTest, ChecksAKey) {
  EXPECT_EQ(FloatypeSettingsKeymapKeyIsValid("Ctrl Shift a"), 1);
  EXPECT_EQ(FloatypeSettingsKeymapKeyIsValid("Henkan"), 1);
  EXPECT_EQ(FloatypeSettingsKeymapKeyIsValid("Kanji"), 0);
  EXPECT_EQ(FloatypeSettingsKeymapKeyIsValid("NoSuchKey"), 0);
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
