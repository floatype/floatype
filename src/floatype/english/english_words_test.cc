#include "floatype/english/english_words.h"

#include <string>
#include <utility>
#include <vector>

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

// One composed character per element: what was typed, read and shown.
std::vector<EnglishSpan> Spans(
    const std::vector<std::vector<std::string>>& chars,
    const EnglishWords& words) {
  std::vector<std::string> raws, query, preedit;
  for (const std::vector<std::string>& c : chars) {
    raws.push_back(c[0]);
    query.push_back(c[1]);
    preedit.push_back(c.size() > 2 ? c[2] : c[1]);
  }
  return EnglishSpans(raws, query, preedit, words);
}

using Spans_ = std::vector<EnglishSpan>;

TEST_F(EnglishWordsTest, FindsAWordInsideJapanese) {
  const EnglishWords words = Words();
  // きょうは update する
  EXPECT_EQ(Spans({{"kyo", "きょ"}, {"u", "う"}, {"ha", "は"}, {"u", "う"},
                   {"p", "ｐ"}, {"da", "だ"}, {"te", "て"}, {"su", "す"},
                   {"ru", "る"}},
                  words),
            (Spans_{{3, 7, "update", ""}}));
  // update を
  EXPECT_EQ(Spans({{"u", "う"}, {"p", "ｐ"}, {"da", "だ"}, {"te", "て"},
                   {"wo", "を"}},
                  words),
            (Spans_{{0, 4, "update", ""}}));
}

TEST_F(EnglishWordsTest, LeavesAWordAlreadyInLettersAndJapaneseAlone) {
  const EnglishWords words = Words();
  EXPECT_EQ(Spans({{"u", "u"}, {"p", "p"}, {"d", "d"}, {"a", "a"}, {"t", "t"},
                   {"e", "e"}, {"wo", "を"}},
                  words),
            Spans_{});
  EXPECT_EQ(Spans({{"ko", "こ"}, {"to", "と"}}, words), Spans_{});
  EXPECT_EQ(Spans({{"he", "へ"}, {"llo", "っぉ"}}, words), Spans_{});
}

TEST_F(EnglishWordsTest, DoesNotCutAWordShort) {
  const EnglishWords words = Words();
  // "updatex" is no word, and "update" would cut it.
  EXPECT_EQ(Spans({{"u", "う"}, {"p", "ｐ"}, {"da", "だ"}, {"te", "て"},
                   {"x", "ｘ"}},
                  words),
            Spans_{});
}

TEST_F(EnglishWordsTest, ExtendsAWordKeptInLetters) {
  const EnglishWords words = EnglishWords("update\nupdates\n");
  EXPECT_EQ(Spans({{"u", "u"}, {"p", "p"}, {"d", "d"}, {"a", "a"}, {"t", "t"},
                   {"e", "e"}, {"s", "s", "ｓ"}},
                  words),
            (Spans_{{0, 7, "updates", ""}}));
}

TEST_F(EnglishWordsTest, RetypesLettersTheNextKanaTook) {
  const EnglishWords words = EnglishWords("email\n");
  // email を: the romaji held l and w together (lw starts ゎ).
  EXPECT_EQ(Spans({{"e", "え"}, {"ma", "ま"}, {"i", "い"}, {"l", "ｌ"},
                   {"w", "ｗ"}, {"o", "お"}},
                  words),
            (Spans_{{0, 6, "email", "wo"}}));
  // Letters still pending at the end may yet make a longer word.
  EXPECT_EQ(Spans({{"e", "え"}, {"ma", "ま"}, {"i", "い"}, {"l", "ｌ"},
                   {"w", "ｗ"}},
                  words),
            Spans_{});
}

TEST_F(EnglishWordsTest, EndsAWordInsideTheKanaAfterIt) {
  const EnglishWords words = EnglishWords("test\n");
  // test する: t and su made つ.
  EXPECT_EQ(Spans({{"te", "て"}, {"s", "ｓ"}, {"tsu", "つ"}, {"ru", "る"}},
                  words),
            (Spans_{{0, 3, "test", "su"}}));
}

TEST_F(EnglishWordsTest, ALongerWordWins) {
  const EnglishWords words = EnglishWords("has\nslack\nlack\n");
  // は slack: "has" would take the s and leave ck over.
  EXPECT_EQ(Spans({{"ha", "は"}, {"s", "ｓ"}, {"la", "ぁ"}, {"c", "ｃ"},
                   {"k", "ｋ"}, {"su", "す"}},
                  words),
            (Spans_{{1, 5, "slack", ""}}));
}

TEST(EnglishWordsDefaultTest, HasSCOWLsWords) {
  EXPECT_GT(EnglishWords::Default().size(), 80000);
  EXPECT_TRUE(EnglishWords::Default().Contains("keyboard"));
  EXPECT_TRUE(EnglishWords::Default().Contains("email"))
      << "newer than macOS's 1934 list";
}

}  // namespace
}  // namespace mozc::floatype
