#include "floatype/zenz/zenz_memory.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/clock_mock.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "testing/gmock.h"
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

TEST_F(ZenzMemoryTest, AWriteKeepsWhatAnotherInstanceWroteOrRemoved) {
  // Two processes on one file: the converter and the settings screen.
  ZenzMemory converter(Path());
  ZenzMemory screen(Path());
  converter.Add("きかい", "機械");
  converter.Add("きかい", "器械");
  screen.Remove("きかい", "機械");
  // The converter's next write does not bring the line back.
  converter.Add("きしゃ", "記者");
  const ZenzMemory read(Path());
  EXPECT_FALSE(read.Contains("きかい", "機械"));
  EXPECT_TRUE(read.Contains("きかい", "器械"));
  EXPECT_TRUE(read.Contains("きしゃ", "記者"));
  EXPECT_THAT(read.Entries(),
              ::testing::ElementsAre(::testing::Pair("きかい", "器械"),
                                     ::testing::Pair("きしゃ", "記者")));
}

TEST_F(ZenzMemoryTest, ReadsAnEditByHandWithinASecond) {
  ClockMock clock(absl::FromUnixSeconds(1000));
  Clock::SetClockForUnitTest(&clock);
  ZenzMemory memory(Path());
  memory.Add("きかい", "機械");
  ASSERT_OK(FileUtil::SetContents(Path(), "きしゃ\t記者\n"));
  clock.Advance(absl::Seconds(2));
  EXPECT_TRUE(memory.Contains("きしゃ", "記者"));
  EXPECT_FALSE(memory.Contains("きかい", "機械"));
  Clock::SetClockForUnitTest(nullptr);
}

}  // namespace
}  // namespace mozc::floatype
