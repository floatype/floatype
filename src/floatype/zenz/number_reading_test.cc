#include "floatype/zenz/number_reading.h"

#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

TEST(ReadsAsNumberTest, ReadsNumbersAsSaidAloud) {
  EXPECT_TRUE(ReadsAsNumber("せんきゅうひゃくななじゅうに", 1972));
  EXPECT_TRUE(ReadsAsNumber("にせんご", 2005));
  EXPECT_TRUE(ReadsAsNumber("さんびゃくろくじゅうご", 365));
  EXPECT_TRUE(ReadsAsNumber("じゅうきゅう", 19));
  EXPECT_TRUE(ReadsAsNumber("ひゃく", 100));
  EXPECT_TRUE(ReadsAsNumber("いちまんにせん", 12000));
  EXPECT_TRUE(ReadsAsNumber("ひゃくまん", 1000000));
  EXPECT_TRUE(ReadsAsNumber("さんおくにせんまん", 320000000));
  EXPECT_TRUE(ReadsAsNumber("ぜろ", 0));
}

TEST(ReadsAsNumberTest, ReadsTheFormsBeforeCounters) {
  EXPECT_TRUE(ReadsAsNumber("にせんじゅうよ", 2014));  // 2014年
  EXPECT_TRUE(ReadsAsNumber("じゅうはっ", 18));        // 18歳
  EXPECT_TRUE(ReadsAsNumber("ごじゅっ", 50));          // 50分
  EXPECT_TRUE(ReadsAsNumber("く", 9));                 // 9月
  EXPECT_TRUE(ReadsAsNumber("しち", 7));               // 7月
  EXPECT_TRUE(ReadsAsNumber("ろっ", 6));               // 6本
  EXPECT_TRUE(ReadsAsNumber("ひゃっ", 100));           // 100回
}

TEST(ReadsAsNumberTest, ReadsSmallNumbersSaidInEnglish) {
  EXPECT_TRUE(ReadsAsNumber("ふぁいぶ", 5));               // X-5
  EXPECT_TRUE(ReadsAsNumber("ふぃふてぃー", 50));           // 50周年
  EXPECT_TRUE(ReadsAsNumber("とぅえんてぃーわん", 21));
  EXPECT_TRUE(ReadsAsNumber("しっくすてぃーん", 16));
  EXPECT_FALSE(ReadsAsNumber("ふぁいぶ", 4));
  EXPECT_FALSE(ReadsAsNumber("ふぃふてぃーふぁいぶ", 505));
}

TEST(ReadsAsNumberTest, RefusesOtherNumbersAndOtherWords) {
  EXPECT_FALSE(ReadsAsNumber("せんきゅうひゃくななじゅうに", 1973));
  EXPECT_FALSE(ReadsAsNumber("せんきゅう", 1972));
  EXPECT_FALSE(ReadsAsNumber("はちにち", 8));
  EXPECT_FALSE(ReadsAsNumber("", 0));
  EXPECT_FALSE(ReadsAsNumber("", 5));
  EXPECT_FALSE(ReadsAsNumber("まん", 10000));  // 万円 is not 10000円.
  EXPECT_FALSE(ReadsAsNumber("いっちょう", 1'0000'0000'0000));
}

}  // namespace
}  // namespace mozc::floatype
