#include "floatype/english/english_words.h"

#include <string>

#include "base/file_util.h"
#include "base/system_util.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

class EnglishWordsTest : public testing::TestWithTempUserProfile {
 protected:
  EnglishWords Words() {
    const std::string path =
        FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(), "words");
    EXPECT_OK(FileUtil::SetContents(path, "Macbook\nhello\nupdate\nkoto\n"));
    return EnglishWords(path);
  }
};

TEST_F(EnglishWordsTest, AWordWithLettersLeftOverIsEnglish) {
  const EnglishWords words = Words();
  EXPECT_TRUE(LooksEnglish("macbook", "まcぼおk", words));
  EXPECT_TRUE(LooksEnglish("update", "うｐだて", words)) << "full width too";
}

TEST_F(EnglishWordsTest, KanaThatReadsWellStaysJapanese) {
  const EnglishWords words = Words();
  EXPECT_FALSE(LooksEnglish("hello", "へっぉ", words)) << "no letters left";
  EXPECT_FALSE(LooksEnglish("koto", "こと", words));
}

TEST_F(EnglishWordsTest, ATypoIsNotAWord) {
  const EnglishWords words = Words();
  EXPECT_FALSE(LooksEnglish("wtasi", "wたし", words));
  EXPECT_FALSE(LooksEnglish("ab", "あb", words)) << "too short";
}

TEST(EnglishWordsDefaultTest, ReadsTheSystemList) {
  EXPECT_GT(EnglishWords::Default().size(), 100000);
  EXPECT_TRUE(EnglishWords::Default().Contains("keyboard"));
}

}  // namespace
}  // namespace mozc::floatype
