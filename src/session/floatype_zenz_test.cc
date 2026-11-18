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
#include "config/config_handler.h"
#include "engine/mock_data_engine_factory.h"
#include "floatype/zenz/scorer_client.h"
#include "floatype/zenz/zenz_corrector.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/session_handler.h"
#include "session/session_handler_test_util.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::session {
namespace {

bool Suggests(const commands::Output& output, absl::string_view value) {
  for (const commands::CandidateWord& word :
       output.all_candidate_words().candidates()) {
    if (word.value() == value) return true;
  }
  return false;
}

bool AllUnderlined(const commands::Output& output) {
  for (const commands::Preedit::Segment& segment : output.preedit().segment()) {
    if (segment.annotation() != commands::Preedit::Segment::UNDERLINE) {
      return false;
    }
  }
  return output.preedit().segment_size() > 0;
}

commands::Output::Indicator::State Indicator(const commands::Output& output) {
  return output.indicator().state();
}

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
    floatype::EnableSessionFeatures(false);
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

  // Sets how long a live conversion waits for zenz before it shows.
  void SetLiveConversionWait(int millisec) {
    config::Config config = config::ConfigHandler::DefaultConfig();
    config.set_live_conversion_waits_for_zenz_millisec(millisec);
    commands::Input input;
    input.set_type(commands::Input::SET_CONFIG);
    *input.mutable_config() = config;
    Eval(std::move(input));
  }

  // Sends the callback command, as the client does when its delay has passed.
  commands::Output Poll() {
    commands::Input input;
    input.set_type(commands::Input::SEND_COMMAND);
    input.mutable_command()->set_type(commands::SessionCommand::POLL_ZENZ);
    return Eval(std::move(input));
  }

  // Sends the callback the output asks for, as the client's timer does.
  commands::Output CallBack(const commands::Output& output) {
    EXPECT_TRUE(output.has_callback());
    commands::Input input;
    input.set_type(commands::Input::SEND_COMMAND);
    *input.mutable_command() = output.callback().session_command();
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

  void SetZenzSuggestionLearning(bool on) {
    config::Config config = config::ConfigHandler::DefaultConfig();
    config.set_zenz_suggestion_learning(on);
    commands::Input input;
    input.set_type(commands::Input::SET_CONFIG);
    *input.mutable_config() = config;
    Eval(std::move(input));
  }

  // Converts きかいがきかい, which the fake scorer turns into 機械が機械, and
  // commits zenz's correction.
  void CommitCorrectedSentence() {
    ASSERT_OK(FileUtil::SetContents(answers_, "キカイガキカイ\t機械が機械\n"));
    const commands::Output corrected = PollWhileAsked(SendKeys(
        {"k", "i", "k", "a", "i", "g", "a", "k", "i", "k", "a", "i", "Space"}));
    ASSERT_EQ(Preedit(corrected), "機械が機械");
    ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  }

  commands::Output TypeKikaiga() {
    return SendKeys({"k", "i", "k", "a", "i", "g", "a"});
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
  EXPECT_EQ(converted.callback().delay_millisec(),
            absl::ToInt64Milliseconds(floatype::ZenzCorrector::kPollInterval));

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

TEST_F(FloatypeZenzTest, BackspaceRightAfterACommitTakesBackTheLearning) {
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  EXPECT_FALSE(SendKeys({"Backspace"}).consumed()) << "the app deletes it";

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機会");
}

TEST_F(FloatypeZenzTest, EscapeAfterACommitKeepsTheLearning) {
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  SendKeys({"Escape"});  // As in vim, leaving insert mode.

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機械");
}

TEST_F(FloatypeZenzTest, PickingZenzsCandidateAfterShowingMozcIsLearned) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機械") << "zenz's is second";
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機械");
}

TEST_F(FloatypeZenzTest, TheZenzKeyKeepsZenzsCorrectionWhenZenzAgrees) {
  floatype::EnableSessionFeatures(true);
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  const commands::Output again = PollWhileAsked(SendKeys({"Ctrl z"}));
  EXPECT_EQ(Preedit(again), "機械");
  EXPECT_EQ(Indicator(again), commands::Output::Indicator::ZENZ);
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

TEST_F(FloatypeZenzTest, SuggestsWhatTheUserCommittedAfterZenz) {
  CommitCorrectedSentence();
  EXPECT_TRUE(Suggests(TypeKikaiga(), "機械が機械"));
}

TEST_F(FloatypeZenzTest, KeepsZenzsCorrectionOutOfSuggestionsWhenAskedTo) {
  SetZenzSuggestionLearning(false);
  CommitCorrectedSentence();
  EXPECT_FALSE(Suggests(TypeKikaiga(), "機械が機械"));
}

TEST_F(FloatypeZenzTest, CtrlBackspaceDeletesTheSelectedSuggestion) {
  CommitCorrectedSentence();
  ASSERT_TRUE(Suggests(TypeKikaiga(), "機械が機械"));
  // Move through the predictions until the learned sentence is selected.
  commands::Output output = SendKeys({"Down"});
  for (int i = 0; i < 10 && Preedit(output) != "機械が機械"; ++i) {
    output = SendKeys({"Down"});
  }
  ASSERT_EQ(Preedit(output), "機械が機械");
  SendKeys({"Ctrl Backspace"});
  SendKeys({"Escape", "Escape"});
  EXPECT_FALSE(Suggests(TypeKikaiga(), "機械が機械"));
}

TEST_F(FloatypeZenzTest, LiveConvertsWhenTheUserPausesAndZenzFollows) {
  floatype::EnableSessionFeatures(true);
  const commands::Output typed = SendKeys({"k", "i", "k", "a", "i"});
  EXPECT_EQ(Preedit(typed), "きかい");
  ASSERT_TRUE(typed.has_callback());
  EXPECT_EQ(typed.callback().session_command().type(),
            commands::SessionCommand::LIVE_CONVERT);
  EXPECT_EQ(typed.callback().delay_millisec(), 228);

  const commands::Output live = CallBack(typed);
  EXPECT_EQ(Preedit(live), "機会");
  EXPECT_TRUE(AllUnderlined(live));
  ASSERT_TRUE(live.has_callback());
  EXPECT_EQ(live.callback().session_command().type(),
            commands::SessionCommand::REQUEST_ZENZ);
  EXPECT_EQ(live.callback().delay_millisec(), 500 - 228);

  const commands::Output corrected = PollWhileAsked(CallBack(live));
  EXPECT_EQ(Preedit(corrected), "機械");
  EXPECT_TRUE(AllUnderlined(corrected));
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "機械");
}

TEST_F(FloatypeZenzTest, TypingAfterALiveConversionEditsTheReading) {
  floatype::EnableSessionFeatures(true);
  CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  // The converted part stays; only what was typed after it is the reading.
  const commands::Output typed = SendKeys({"g", "a"});
  EXPECT_EQ(Preedit(typed), "機会が");
  EXPECT_TRUE(AllUnderlined(typed));
  EXPECT_EQ(typed.callback().session_command().type(),
            commands::SessionCommand::LIVE_CONVERT);
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "機会が")
      << "Enter commits what is on screen";

  CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  EXPECT_EQ(Preedit(SendKeys({"Backspace"})), "きか");
}

TEST_F(FloatypeZenzTest, ZenzsCorrectionStaysWhileTypingOn) {
  floatype::EnableSessionFeatures(true);
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(live))), "機械");
  const commands::Output typed = SendKeys({"g", "a"});
  EXPECT_EQ(Preedit(typed), "機械が");
  // The next live conversion keeps it rather than going back to Mozc's 機会.
  EXPECT_EQ(Preedit(CallBack(typed)), "機械が");
}

TEST_F(FloatypeZenzTest, ALiveConversionCanConvertAfreshAsMozkeyDoes) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_live_conversion_keeps_shown_text(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(live))), "機械");
  const commands::Output typed = SendKeys({"g", "a"});
  EXPECT_EQ(Preedit(typed), "機械が");
  EXPECT_EQ(Preedit(CallBack(typed)), "機会が");
}

TEST_F(FloatypeZenzTest, ALiveConversionNeedsTwoCharactersAndCanBeOff) {
  floatype::EnableSessionFeatures(true);
  EXPECT_FALSE(SendKeys({"k", "i"}).has_callback());
  SendKeys({"Escape"});

  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  EXPECT_FALSE(SendKeys({"k", "i", "k", "a", "i"}).has_callback());
}

TEST_F(FloatypeZenzTest, TheIndicatorTellsWhoseResultIsShown) {
  floatype::EnableSessionFeatures(true);
  const commands::Output typed = SendKeys({"k", "i", "k", "a", "i"});
  EXPECT_EQ(Indicator(typed), commands::Output::Indicator::READING);
  EXPECT_EQ(typed.indicator().style().mozc_color(), 0x586b85u);

  commands::Output converted = SendKeys({"Space"});
  EXPECT_EQ(Preedit(converted), "機会");
  EXPECT_EQ(Indicator(converted), commands::Output::Indicator::ZENZ_PENDING);
  const commands::Output corrected = PollWhileAsked(converted);
  EXPECT_EQ(Preedit(corrected), "機械");
  EXPECT_EQ(Indicator(corrected), commands::Output::Indicator::ZENZ);

  // Picking another candidate makes it the user's choice again.
  EXPECT_EQ(Indicator(SendKeys({"Space"})), commands::Output::Indicator::MOZC);
  EXPECT_FALSE(SendKeys({"Enter"}).has_indicator());
}

TEST_F(FloatypeZenzTest, TheIndicatorGoesBackToMozcWhenZenzAgrees) {
  floatype::EnableSessionFeatures(true);
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機会\n"));
  const commands::Output done = PollWhileAsked(ConvertKikai());
  EXPECT_EQ(Preedit(done), "機会");
  EXPECT_EQ(Indicator(done), commands::Output::Indicator::MOZC);
}

TEST_F(FloatypeZenzTest, TheIndicatorCanBeHidden) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_show_indicator(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  EXPECT_FALSE(SendKeys({"k", "i"}).has_indicator());
}

TEST_F(FloatypeZenzTest, SpaceShowsMozcAgainAndTheZenzKeyAsksAgain) {
  floatype::EnableSessionFeatures(true);
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");

  const commands::Output mozc = SendKeys({"Space"});
  EXPECT_EQ(Preedit(mozc), "機会");
  EXPECT_EQ(Indicator(mozc), commands::Output::Indicator::MOZC);
  EXPECT_FALSE(mozc.has_callback());

  const commands::Output asked = SendKeys({"Ctrl z"});
  EXPECT_EQ(Preedit(asked), "機会");
  EXPECT_EQ(Indicator(asked), commands::Output::Indicator::ZENZ_PENDING);
  const commands::Output corrected = PollWhileAsked(asked);
  EXPECT_EQ(Preedit(corrected), "機械");
  EXPECT_EQ(Indicator(corrected), commands::Output::Indicator::ZENZ);

  // Showing Mozc's result again is not a choice the user made.
  SendKeys({"Space"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "機会");
}

TEST_F(FloatypeZenzTest, SpaceAfterShowingMozcAgainPicksTheNextCandidate) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
  const commands::Output next = SendKeys({"Space"});
  EXPECT_NE(Preedit(next), "機会");
  EXPECT_TRUE(next.has_candidate_window());
}

TEST_F(FloatypeZenzTest, TheZenzKeyConvertsWhileComposing) {
  const commands::Output asked = SendKeys({"k", "i", "k", "a", "i", "Ctrl z"});
  EXPECT_EQ(Preedit(asked), "機会");
  EXPECT_EQ(Preedit(PollWhileAsked(asked)), "機械");
}

TEST_F(FloatypeZenzTest, TheZenzKeyKeepsALiveConversionLive) {
  floatype::EnableSessionFeatures(true);
  const commands::Output asked = SendKeys({"k", "i", "k", "a", "i", "Ctrl z"});
  EXPECT_EQ(Preedit(asked), "機会");
  EXPECT_EQ(Indicator(asked), commands::Output::Indicator::ZENZ_PENDING);
  EXPECT_EQ(Preedit(PollWhileAsked(asked)), "機械");
  // Typing on edits the reading, as after any live conversion.
  EXPECT_EQ(Preedit(SendKeys({"g", "a"})), "機械が");
}

std::vector<std::string> Suggestions(const commands::Output& output) {
  std::vector<std::string> values;
  for (const commands::CandidateWindow::Candidate& candidate :
       output.candidate_window().candidate()) {
    values.push_back(candidate.value());
  }
  return values;
}

TEST_F(FloatypeZenzTest, KeepsSuggestionsWhileARomanSequenceIsUnfinished) {
  floatype::EnableSessionFeatures(true);
  const std::vector<std::string> shown =
      Suggestions(SendKeys({"k", "i", "k", "a", "i"}));
  ASSERT_GE(shown.size(), 2);
  const commands::Output k = SendKeys({"k"});
  EXPECT_EQ(Preedit(k), "きかいｋ");
  EXPECT_EQ(Suggestions(k), shown);
  const commands::Output ky = SendKeys({"y"});
  EXPECT_EQ(Preedit(ky), "きかいｋｙ");
  EXPECT_EQ(Suggestions(ky), shown) << "Mozc alone suggests one candidate";
  // A finished sequence suggests afresh.
  EXPECT_EQ(Preedit(SendKeys({"o"})), "きかいきょ");
}

TEST_F(FloatypeZenzTest, PickingAHeldSuggestionCommitsItAsMozcWould) {
  floatype::EnableSessionFeatures(true);
  SendKeys({"k", "i", "k", "a", "i", "k", "y"});
  // Mozc alone suggests 機会 for きかいｋｙ too, dropping the unfinished ｋｙ.
  ASSERT_EQ(Preedit(SendKeys({"Down"})), "機会");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  // Enter without picking commits the composition as typed.
  SendKeys({"k", "i", "k", "a", "i", "k", "y"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "きかいｋｙ");
}

TEST_F(FloatypeZenzTest, SuggestsForTheFirstLettersOfAComposition) {
  floatype::EnableSessionFeatures(true);
  EXPECT_EQ(Suggestions(SendKeys({"k"})), std::vector<std::string>{"k"});
}

TEST_F(FloatypeZenzTest, WhatWasCommittedLastStandsInForTheContext) {
  // The fake turns キカイ into 機械 only after 機会.
  ASSERT_OK(
      FileUtil::SetContents(answers_, "キカイ\t機会\n機会|キカイ\t機械\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  // The test client tells nothing about the text before the caret.
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
}

TEST_F(FloatypeZenzTest, LeavingTheFieldForgetsWhatStoodInForTheContext) {
  ASSERT_OK(
      FileUtil::SetContents(answers_, "キカイ\t機会\n機会|キカイ\t機械\n"));
  PollWhileAsked(ConvertKikai());
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  // What the macOS client sends when the field loses focus.
  commands::Input input;
  input.set_type(commands::Input::SEND_COMMAND);
  input.mutable_command()->set_type(commands::SessionCommand::RESET_CONTEXT);
  Eval(std::move(input));
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
}

TEST_F(FloatypeZenzTest, ALiveConversionCanWaitForZenzAndChangeOnce) {
  floatype::EnableSessionFeatures(true);
  SetLiveConversionWait(250);
  const commands::Output typed = SendKeys({"k", "i", "k", "a", "i"});
  const commands::Output waiting = CallBack(typed);
  EXPECT_EQ(Preedit(waiting), "きかい") << "Mozc's 機会 is never shown";
  EXPECT_EQ(Indicator(waiting), commands::Output::Indicator::ZENZ_PENDING);
  ASSERT_TRUE(waiting.has_callback());
  EXPECT_EQ(waiting.callback().session_command().type(),
            commands::SessionCommand::POLL_ZENZ);
  EXPECT_EQ(Preedit(PollWhileAsked(waiting)), "機械");
}

TEST_F(FloatypeZenzTest, AKeyWhileWaitingForZenzMeetsWhatIsShown) {
  floatype::EnableSessionFeatures(true);
  SetLiveConversionWait(250);
  // Enter commits what is on screen: the reading.
  CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "きかい");
  // Typing on edits the reading.
  CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  EXPECT_EQ(Preedit(SendKeys({"g", "a"})), "きかいが");
  SendKeys({"Escape"});
  SendKeys({"Escape"});
  // Space converts as it always does, and zenz follows.
  CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  const commands::Output converted = SendKeys({"Space"});
  EXPECT_EQ(Preedit(converted), "機会");
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機械");
}

TEST_F(FloatypeZenzTest, TheCandidateWindowGetsItsFontScale) {
  floatype::EnableSessionFeatures(true);
  EXPECT_FALSE(
      SendKeys({"k", "i", "k", "a", "i"}).has_candidate_font_scale_percent())
      << "100% is Mozc's look, left unsaid";
  SendKeys({"Escape"});
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_candidate_font_scale_percent(150);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const commands::Output suggested = SendKeys({"k", "i", "k", "a", "i"});
  ASSERT_TRUE(suggested.has_candidate_window());
  EXPECT_EQ(suggested.candidate_font_scale_percent(), 150);
}

TEST_F(FloatypeZenzTest, CtrlDeleteStopsACorrectionForGood) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  EXPECT_EQ(Preedit(SendKeys({"Ctrl Delete"})), "機会");
  SendKeys({"Escape"});
  SendKeys({"Escape"});
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会")
      << "zenz's 機械 for きかい is not made again";
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // Clearing the user history forgets it.
  commands::Input clear;
  clear.set_type(commands::Input::CLEAR_USER_HISTORY);
  Eval(std::move(clear));
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
}

TEST_F(FloatypeZenzTest, ACommittedCorrectionShowsAtOnceNextTime) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  // The control: Mozc alone, in a new session where the commit is no context
  // to repeat it from, still converts to 機会.
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(ConvertKikai()), "機会");
  StartSession(/*zenz=*/true);

  // Right at Space, before zenz answers; zenz still looks.
  const commands::Output converted = ConvertKikai();
  EXPECT_EQ(Preedit(converted), "機械");
  EXPECT_TRUE(converted.has_callback());
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機械");
  // Space still shows Mozc's result.
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "機会");
}

TEST_F(FloatypeZenzTest, StoppingACommittedCorrectionForgetsIt) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  StartSession(/*zenz=*/true);
  ASSERT_EQ(Preedit(ConvertKikai()), "機械") << "shown from memory";
  EXPECT_EQ(Preedit(SendKeys({"Ctrl Delete"})), "機会");
  SendKeys({"Escape"});
  SendKeys({"Escape"});
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
}

TEST_F(FloatypeZenzTest, ShowingMozcThenCommittingTeachesNothing) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  StartSession(/*zenz=*/true);
  EXPECT_EQ(Preedit(ConvertKikai()), "機会") << "not shown before zenz answers";
}

TEST_F(FloatypeZenzTest, CtrlDeleteForgetsNothingWithLearningOff) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_zenz_learning(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  EXPECT_EQ(Preedit(SendKeys({"Ctrl Delete"})), "機会") << "still stops it";
  SendKeys({"Escape"});
  SendKeys({"Escape"});
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
}

class DirectCommitTest : public FloatypeZenzTest {
 protected:
  void SetUp() override {
    FloatypeZenzTest::SetUp();
    floatype::EnableSessionFeatures(true);
    ASSERT_OK(FileUtil::SetContents(answers_, "キカイ。\t機会。\n"));
    config::Config config = config::ConfigHandler::DefaultConfig();
    config.set_direct_commit_characters("、。");
    config.set_use_live_conversion(false);
    commands::Input input;
    input.set_type(commands::Input::SET_CONFIG);
    *input.mutable_config() = config;
    Eval(std::move(input));
  }
};

TEST_F(DirectCommitTest, APeriodConvertsWaitsForZenzAndCommits) {
  const commands::Output converted = SendKeys({"k", "i", "k", "a", "i", "."});
  EXPECT_EQ(Preedit(converted), "機械。") << "Mozc's, while zenz works";
  EXPECT_FALSE(converted.has_result());
  const commands::Output done = PollWhileAsked(converted);
  EXPECT_EQ(done.result().value(), "機会。") << "zenz's";
  EXPECT_EQ(Preedit(done), "");
}

TEST_F(DirectCommitTest, AKeyBeforeZenzAnswersCommitsWhatIsShown) {
  const commands::Output typed = SendKeys({"k", "i", "k", "a", "i", ".", "a"});
  EXPECT_EQ(typed.result().value(), "機械。");
  EXPECT_EQ(Preedit(typed), "あ");
}

TEST_F(DirectCommitTest, EnterBeforeZenzAnswersOnlyCommits) {
  SendKeys({"k", "i", "k", "a", "i", "."});
  const commands::Output entered = SendKeys({"Enter"});
  EXPECT_EQ(entered.result().value(), "機械。");
  EXPECT_TRUE(entered.consumed()) << "no new line in the app";
}

TEST_F(DirectCommitTest, APeriodAfterALiveConversionCommitsToo) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_direct_commit_characters("、。");
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  CallBack(SendKeys({"k", "i", "k", "a", "i"}));  // A live conversion.
  const commands::Output done = PollWhileAsked(SendKeys({"."}));
  EXPECT_EQ(done.result().value(), "機会。");
  EXPECT_EQ(Preedit(done), "");
}

TEST_F(DirectCommitTest, OtherCharactersCompose) {
  EXPECT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i"})), "きかい");
}

TEST_F(FloatypeZenzTest, ALiveConversionCanShowItsReading) {
  floatype::EnableSessionFeatures(true);
  EXPECT_FALSE(CallBack(SendKeys({"k", "i", "k", "a", "i"})).has_live_reading())
      << "off by default";
  SendKeys({"Escape"});
  SendKeys({"Escape"});
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_live_conversion_ruby(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  EXPECT_EQ(Preedit(live), "機会");
  EXPECT_EQ(live.live_reading(), "きかい");
}

TEST_F(FloatypeZenzTest, ZenzCanBeTurnedOff) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_use_zenz(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const commands::Output converted = ConvertKikai();
  EXPECT_EQ(Preedit(converted), "機会");
  EXPECT_FALSE(converted.has_callback());
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
