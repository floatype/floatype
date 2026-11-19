#include "floatype/english/english_words.h"

#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

class EnglishWordsTest : public testing::Test {
 protected:
  static EnglishWords Words() {
    return EnglishWords("Macbook\nhello\nupdate\nkoto\n");
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

TEST(EnglishWordsDefaultTest, HasSCOWLsWords) {
  EXPECT_GT(EnglishWords::Default().size(), 80000);
  EXPECT_TRUE(EnglishWords::Default().Contains("keyboard"));
  EXPECT_TRUE(EnglishWords::Default().Contains("email"))
      << "newer than macOS's 1934 list";
}

}  // namespace
}  // namespace mozc::floatype
