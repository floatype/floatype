#include "floatype/zenz/letter_reading.h"

#include "testing/gmock.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(LetterNamesTest, NamesTheLettersInEitherCase) {
  EXPECT_THAT(LetterNames('J'), ElementsAre("じぇー", "じぇい"));
  EXPECT_THAT(LetterNames('r'), ElementsAre("あーる"));
  EXPECT_THAT(LetterNames('V'), Contains("ぶい"));
  EXPECT_THAT(LetterNames('z'), Contains("ぜっと"));
}

TEST(LetterNamesTest, NamesNothingElse) {
  EXPECT_THAT(LetterNames('8'), IsEmpty());
  EXPECT_THAT(LetterNames('-'), IsEmpty());
  EXPECT_THAT(LetterNames('\xE3'), IsEmpty());  // The first byte of a kana.
}

}  // namespace
}  // namespace mozc::floatype
