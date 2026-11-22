#include <string>

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

}  // namespace
}  // namespace mozc::floatype
