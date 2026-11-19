#include "floatype/zenz/scorer_client.h"

#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include "absl/time/time.h"
#include "base/clock.h"
#include "base/clock_mock.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "floatype/scorer/protocol.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

using ::floatype::scorer::Request;
using ::floatype::scorer::Response;

// Polls until the response to `id` arrives or two seconds pass.
std::optional<Response> WaitForResponse(ScorerClient& client, uint64_t id) {
  for (int i = 0; i < 2000; ++i) {
    std::optional<Response> response = client.Poll();
    if (response.has_value() && response->id == id) return response;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return std::nullopt;
}

class ScorerClientTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override { Clock::SetClockForUnitTest(&clock_); }
  void TearDown() override { Clock::SetClockForUnitTest(nullptr); }

  static std::string FakeScorer() {
    return testing::GetSourceFileOrDie({"floatype", "zenz", "fake_scorer"});
  }

  ClockMock clock_{absl::FromUnixSeconds(1000)};
};

TEST_F(ScorerClientTest, AnswersARequest) {
  ScorerClient client({FakeScorer()});
  EXPECT_EQ(client.Poll(), std::nullopt);
  ASSERT_TRUE(client.Send(Request{1, "熱が", "イシャ"}));
  const std::optional<Response> response = WaitForResponse(client, 1);
  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->id, uint64_t{1});
  EXPECT_EQ(response->conversion, "イシャ");
  EXPECT_EQ(response->stop, "eog");
  const std::optional<Response> again = client.Poll();
  ASSERT_TRUE(again.has_value()) << "the latest response stays";
  EXPECT_EQ(again->id, uint64_t{1});
}

TEST_F(ScorerClientTest, KeepsTheProcessAcrossRequests) {
  ScorerClient client({FakeScorer()});
  for (uint64_t id = 1; id <= 3; ++id) {
    ASSERT_TRUE(client.Send(Request{id, "", "ア"}));
    EXPECT_TRUE(WaitForResponse(client, id).has_value());
  }
}

TEST_F(ScorerClientTest, PollDoesNotWaitForASlowScorer) {
  ScorerClient client({FakeScorer()});
  ASSERT_TRUE(client.Send(Request{1, "", "スロウ"}));
  const absl::Time start = absl::Now();
  EXPECT_EQ(client.Poll(), std::nullopt);
  EXPECT_LT(absl::Now() - start, absl::Milliseconds(50));
  EXPECT_TRUE(WaitForResponse(client, 1).has_value());
}

TEST_F(ScorerClientTest, RestartsADeadScorerAfterTheInterval) {
  ScorerClient client({FakeScorer()});
  ASSERT_TRUE(client.Send(Request{1, "", "CRASH"}));
  EXPECT_EQ(WaitForResponse(client, 1), std::nullopt);
  EXPECT_FALSE(client.Send(Request{2, "", "ア"}))
      << "no restart within the interval";
  clock_.Advance(ScorerClient::kRestartInterval);
  ASSERT_TRUE(client.Send(Request{3, "", "ア"}));
  EXPECT_TRUE(WaitForResponse(client, 3).has_value());
}

TEST_F(ScorerClientTest, StartsAgainAtOnceAfterTheScorerIdledOut) {
  ScorerClient client({FakeScorer()});
  ASSERT_TRUE(client.Send(Request{1, "", "アイドル"}));
  // Nothing polls while the user is away, so the client only learns of the
  // exit when the next request comes.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  // The scorer idles out a minute after its last request, so long after it
  // started; the request after that is not lost to the dead process.
  clock_.Advance(absl::Seconds(60));
  ASSERT_TRUE(client.Send(Request{2, "", "ア"}));
  EXPECT_TRUE(WaitForResponse(client, 2).has_value());
}

TEST_F(ScorerClientTest, WarmStartsAgainAfterTheScorerIdledOut) {
  ScorerClient client({FakeScorer()});
  ASSERT_TRUE(client.Send(Request{1, "", "アイドル"}));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  clock_.Advance(absl::Seconds(60));
  client.Warm();
  ASSERT_TRUE(client.Send(Request{2, "", "ア"}));
  EXPECT_TRUE(WaitForResponse(client, 2).has_value());
}

TEST_F(ScorerClientTest, WarmStartsTheScorerAhead) {
  ScorerClient client({FakeScorer()});
  client.Warm();
  ASSERT_TRUE(client.Send(Request{1, "", "ア"}));
  EXPECT_TRUE(WaitForResponse(client, 1).has_value());
}

TEST_F(ScorerClientTest, NewArgumentsTakeEffectOnTheNextStart) {
  const std::string answers =
      FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(), "answers.tsv");
  ASSERT_OK(FileUtil::SetContents(answers, "ア\t亜\n"));
  ScorerClient client({FakeScorer()});
  ASSERT_TRUE(client.Send(Request{1, "", "ア"}));
  EXPECT_EQ(WaitForResponse(client, 1)->conversion, "ア");
  // The fake reads its first argument as its answers.
  client.SetArguments({answers});
  clock_.Advance(ScorerClient::kRestartInterval);
  ASSERT_TRUE(client.Send(Request{2, "", "ア"}));
  EXPECT_EQ(WaitForResponse(client, 2)->conversion, "亜");
}

TEST_F(ScorerClientTest, FailsWithoutAScorer) {
  ScorerClient client({"/nonexistent/floatype_scorer"});
  EXPECT_FALSE(client.Send(Request{1, "", "ア"}));
  EXPECT_EQ(client.Poll(), std::nullopt);
}

}  // namespace
}  // namespace mozc::floatype
