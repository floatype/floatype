#include "floatype/zenz/zenz_memory.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

class ZenzMemoryTest : public testing::TestWithTempUserProfile {
 protected:
  std::string Path() const {
    return FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(),
                              "stopped.tsv");
  }
};

TEST_F(ZenzMemoryTest, RemembersAcrossRestarts) {
  {
    ZenzMemory memory(Path());
    EXPECT_FALSE(memory.Stopped("きかい", "機械"));
    memory.Stop("きかい", "機械");
    EXPECT_TRUE(memory.Stopped("きかい", "機械"));
    EXPECT_FALSE(memory.Stopped("きかい", "機会")) << "only that text";
    EXPECT_FALSE(memory.Stopped("きかいが", "機械")) << "only that reading";
  }
  ZenzMemory again(Path());
  EXPECT_TRUE(again.Stopped("きかい", "機械"));
  again.Clear();
  EXPECT_FALSE(ZenzMemory(Path()).Stopped("きかい", "機械"));
}

TEST_F(ZenzMemoryTest, KeepsTheNewestUpToItsCapacity) {
  ZenzMemory memory(Path());
  for (size_t i = 0; i <= ZenzMemory::kCapacity; ++i) {
    memory.Stop(absl::StrCat("よみ", i), "文");
  }
  EXPECT_FALSE(memory.Stopped("よみ0", "文")) << "the oldest went";
  EXPECT_TRUE(memory.Stopped("よみ1", "文"));
  EXPECT_TRUE(ZenzMemory(Path()).Stopped(
      absl::StrCat("よみ", ZenzMemory::kCapacity), "文"));
}

TEST_F(ZenzMemoryTest, TheDefaultLivesInTheUserProfile) {
  ZenzMemory::Default().Stop("きかい", "機械");
  EXPECT_TRUE(FileUtil::FileExists(
                  FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(),
                                     "floatype_zenz_stopped.tsv"))
                  .ok());
}

}  // namespace
}  // namespace mozc::floatype
