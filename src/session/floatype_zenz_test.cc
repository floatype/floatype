// floatype: zenz corrections through the session handler, with Mozc's test
// engine and a fake scorer that answers キカイ with 機械. The test plays the
// client and sends the callback itself, so it decides when the poll happens.

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "composer/key_parser.h"
#include "engine/mock_data_engine_factory.h"
#include "floatype/zenz/scorer_client.h"
#include "floatype/zenz/zenz_corrector.h"
#include "protocol/commands.pb.h"
#include "session/session_handler.h"
#include "session/session_handler_test_util.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::session {
namespace {

std::string Preedit(const commands::Output& output) {
  std::string text;
  for (const commands::Preedit::Segment& segment : output.preedit().segment()) {
    text += segment.value();
  }
  return text;
}

class FloatypeZenzTest : public testing::SessionHandlerTestBase {
 protected:
  void SetUp() override {
    SessionHandlerTestBase::SetUp();
    answers_ = FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(),
                                  "answers.tsv");
    ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機械\n"));
    // The fake scorer reads its answers when the first request starts it.
    client_ = std::make_unique<floatype::ScorerClient>(
        std::vector<std::string>{::mozc::testing::GetSourceFileOrDie(
                                     {"floatype", "zenz", "fake_scorer"}),
                                 answers_});
    handler_ = std::make_unique<SessionHandler>(
        MockDataEngineFactory::Create().value());
    StartSession(/*zenz=*/true);
  }

  void TearDown() override {
    handler_.reset();
    floatype::ZenzCorrector::SetDefaultClientForTesting(nullptr);
    client_.reset();
    SessionHandlerTestBase::TearDown();
  }

  commands::Output Eval(commands::Input input) {
    commands::Command command;
    *command.mutable_input() = std::move(input);
    command.mutable_input()->set_id(id_);
    EXPECT_TRUE(handler_->EvalCommand(&command));
    return command.output();
  }

  // Replaces the session; a session uses zenz if the scorer is set when it is
  // created. Learned conversions stay, since they live in the engine.
  void StartSession(bool zenz) {
    floatype::ZenzCorrector::SetDefaultClientForTesting(zenz ? client_.get()
                                                             : nullptr);
    commands::Input input;
    if (id_ != 0) {
      input.set_type(commands::Input::DELETE_SESSION);
      Eval(input);
    }
    input.set_type(commands::Input::CREATE_SESSION);
    id_ = Eval(input).id();
    ASSERT_NE(id_, 0);
  }

  commands::Output SendKeys(std::initializer_list<absl::string_view> keys) {
    commands::Output output;
    for (const absl::string_view key : keys) {
      commands::Input input;
      input.set_type(commands::Input::SEND_KEY);
      EXPECT_TRUE(KeyParser::ParseKey(key, input.mutable_key())) << key;
      output = Eval(std::move(input));
    }
    return output;
  }

  // Sends the callback command, as the client does when its delay has passed.
  commands::Output Poll() {
    commands::Input input;
    input.set_type(commands::Input::SEND_COMMAND);
    input.mutable_command()->set_type(commands::SessionCommand::POLL_ZENZ);
    return Eval(std::move(input));
  }

  // Calls back like the client until the session stops asking.
  commands::Output PollWhileAsked(commands::Output output) {
    for (int i = 0; i < 2000 && output.has_callback(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      output = Poll();
    }
    return output;
  }

  commands::Output ConvertKikai() {
    return SendKeys({"k", "i", "k", "a", "i", "Space"});
  }

  std::string answers_;
  std::unique_ptr<floatype::ScorerClient> client_;
  std::unique_ptr<SessionHandler> handler_;
  uint64_t id_ = 0;
};

TEST_F(FloatypeZenzTest, ShowsZenzsCorrectionAfterSpace) {
  const commands::Output converted = ConvertKikai();
  EXPECT_EQ(Preedit(converted), "機会") << "Mozc's conversion comes first";
  ASSERT_TRUE(converted.has_callback()) << "the session asks to be polled";
  EXPECT_EQ(converted.callback().session_command().type(),
            commands::SessionCommand::POLL_ZENZ);
  EXPECT_EQ(converted.callback().delay_millisec(), 20);

  const commands::Output corrected = PollWhileAsked(converted);
  EXPECT_FALSE(corrected.has_callback());
  EXPECT_EQ(Preedit(corrected), "機械");

  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "機械");
}

TEST_F(FloatypeZenzTest, DoesNotTeachMozcZenzsCorrection) {
  PollWhileAsked(ConvertKikai());
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機会");
}

TEST_F(FloatypeZenzTest, MozcStillLearnsWhatTheUserPicks) {
  // The control for the test above: picking 機械 by hand is learned.
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機械");
}

TEST_F(FloatypeZenzTest, KeepsWhatTheUserTaughtMozc) {
  // Picking 機械 by hand makes Mozc put it first from now on.
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");

  // zenz would rather have 機会, but the learned choice stays.
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機会\n"));
  StartSession(/*zenz=*/true);
  const commands::Output converted = ConvertKikai();
  ASSERT_EQ(Preedit(converted), "機械");
  ASSERT_TRUE(converted.has_callback());
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機械");
}

TEST_F(FloatypeZenzTest, DropsTheAnswerAfterTheUserActs) {
  ASSERT_TRUE(ConvertKikai().has_callback());
  const commands::Output picked = SendKeys({"Space"});
  EXPECT_FALSE(picked.has_callback());
  // Give the fake scorer time to answer, then call back late.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const commands::Output polled = Poll();
  EXPECT_FALSE(polled.has_callback());
  EXPECT_EQ(Preedit(polled), Preedit(picked));
}

TEST_F(FloatypeZenzTest, ALatePollAfterCommitChangesNothing) {
  ASSERT_TRUE(ConvertKikai().has_callback());
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const commands::Output polled = Poll();
  EXPECT_FALSE(polled.has_callback());
  EXPECT_FALSE(polled.has_preedit());
  EXPECT_FALSE(polled.has_result());
}

}  // namespace
}  // namespace mozc::session
