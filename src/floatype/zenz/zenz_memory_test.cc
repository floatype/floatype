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
                              "memory.tsv");
  }
};

TEST_F(ZenzMemoryTest, RemembersAcrossRestarts) {
  {
    ZenzMemory memory(Path());
    EXPECT_FALSE(memory.Contains("きかい", "機械"));
    memory.Add("きかい", "機械");
    EXPECT_TRUE(memory.Contains("きかい", "機械"));
    EXPECT_FALSE(memory.Contains("きかい", "機会")) << "only that text";
    EXPECT_FALSE(memory.Contains("きかいが", "機械")) << "only that reading";
  }
  ZenzMemory again(Path());
  EXPECT_TRUE(again.Contains("きかい", "機械"));
  again.Clear();
  EXPECT_FALSE(ZenzMemory(Path()).Contains("きかい", "機械"));
}

TEST_F(ZenzMemoryTest, FindsTheNewestTextForAReading) {
  ZenzMemory memory(Path());
  EXPECT_EQ(memory.Latest("きかい"), "");
  memory.Add("きかい", "機械");
  memory.Add("きかい", "器械");
  EXPECT_EQ(memory.Latest("きかい"), "器械");
  memory.Add("きかい", "機械");  // Again: the newest now.
  EXPECT_EQ(memory.Latest("きかい"), "機械");
  memory.Remove("きかい", "機械");
  EXPECT_EQ(memory.Latest("きかい"), "器械");
  EXPECT_EQ(ZenzMemory(Path()).Latest("きかい"), "器械");
  EXPECT_EQ(memory.Latest("きか"), "") << "a whole reading only";
}

TEST_F(ZenzMemoryTest, KeepsTheNewestUpToItsCapacity) {
  ZenzMemory memory(Path());
  for (size_t i = 0; i <= ZenzMemory::kCapacity; ++i) {
    memory.Add(absl::StrCat("よみ", i), "文");
  }
  EXPECT_FALSE(memory.Contains("よみ0", "文")) << "the oldest went";
  EXPECT_TRUE(memory.Contains("よみ1", "文"));
  EXPECT_TRUE(ZenzMemory(Path()).Contains(
      absl::StrCat("よみ", ZenzMemory::kCapacity), "文"));
}

TEST_F(ZenzMemoryTest, TheMemoriesLiveInTheUserProfile) {
  ZenzMemory::Stopped().Add("きかい", "機械");
  ZenzMemory::Accepted().Add("きかい", "器械");
  const std::string dir = SystemUtil::GetUserProfileDirectory();
  EXPECT_TRUE(
      FileUtil::FileExists(FileUtil::JoinPath(dir, "floatype_zenz_stopped.tsv"))
          .ok());
  EXPECT_TRUE(FileUtil::FileExists(
                  FileUtil::JoinPath(dir, "floatype_zenz_accepted.tsv"))
                  .ok());
  EXPECT_FALSE(ZenzMemory::Accepted().Contains("きかい", "機械"));
}

}  // namespace
}  // namespace mozc::floatype
