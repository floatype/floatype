// floatype: zenz corrections through the session handler, with Mozc's test
// engine and a fake scorer that answers キカイ with 機械. The test plays the
// client and sends the callback itself, so it decides when the poll happens.

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <istream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/config_file_stream.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "composer/key_parser.h"
#include "config/config_handler.h"
#include "engine/mock_data_engine_factory.h"
#include "floatype/zenz/scorer_client.h"
#include "floatype/zenz/zenz_corrector.h"
#include "floatype/zenz/zenz_memory.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/keymap.h"
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
    // The lock screen turns secure input on, which keeps zenz out.
    floatype::ZenzCorrector::IgnoreSecureInputForTesting(true);
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
    floatype::ZenzCorrector::IgnoreSecureInputForTesting(false);
    floatype::ZenzCorrector::PretendSecureInputForTesting(false);
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

  // Sets whether committing a correction teaches Mozc's ranking its words.
  void SetZenzTeachesMozc(bool on) {
    config::Config config = config::ConfigHandler::DefaultConfig();
    config.set_zenz_teaches_mozc(on);
    commands::Input input;
    input.set_type(commands::Input::SET_CONFIG);
    *input.mutable_config() = config;
    Eval(std::move(input));
  }

  // Sets from how many characters of reading on a remembered correction
  // shows at once.
  void SetRememberedMinChars(int chars) {
    config::Config config = config::ConfigHandler::DefaultConfig();
    config.set_zenz_remembered_min_chars(chars);
    commands::Input input;
    input.set_type(commands::Input::SET_CONFIG);
    *input.mutable_config() = config;
    Eval(std::move(input));
  }

  // Converts きかいがきかい, which the fake scorer turns into 機械が機械, and
  // commits zenz's correction. `more_answers` are more lines for the fake
  // scorer, which reads its answers at the first request.
  void CommitCorrectedSentence(absl::string_view more_answers = "") {
    ASSERT_OK(FileUtil::SetContents(
        answers_, absl::StrCat("キカイガキカイ\t機械が機械\n", more_answers)));
    const commands::Output corrected = PollWhileAsked(SendKeys(
        {"k", "i", "k", "a", "i", "g", "a", "k", "i", "k", "a", "i", "Space"}));
    ASSERT_EQ(Preedit(corrected), "機械が機械");
    ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  }

  commands::Output TypeKikaiga() {
    return SendKeys({"k", "i", "k", "a", "i", "g", "a"});
  }

  // Has zenz's 機械が機械 committed, so that it is suggested while Mozc's
  // ranking still puts 機会 first, and starts a session without zenz.
  void SuggestTheCorrectedSentence() {
    CommitCorrectedSentence();
    ASSERT_EQ(MozcsKikai(), "機会");
    StartSession(/*zenz=*/false);
  }

  // Moves through the predictions to `value`.
  commands::Output SelectPrediction(absl::string_view value) {
    commands::Output output = SendKeys({"Down"});
    for (int i = 0; i < 10 && Preedit(output) != value; ++i) {
      output = SendKeys({"Down"});
    }
    EXPECT_EQ(Preedit(output), value);
    return output;
  }

  // What Mozc alone makes of きかい, in a new session.
  std::string MozcsKikai() {
    StartSession(/*zenz=*/false);
    return Preedit(ConvertKikai());
  }

  // The id of the suggestion `value` among those `output` shows.
  int SuggestionId(const commands::Output& output, absl::string_view value) {
    for (const commands::CandidateWord& word :
         output.all_candidate_words().candidates()) {
      if (word.value() == value) {
        return word.id();
      }
    }
    ADD_FAILURE() << value << " is not suggested";
    return 0;
  }

  commands::Output SendCommand(commands::SessionCommand::CommandType type,
                               int id = 0) {
    commands::Input input;
    input.set_type(commands::Input::SEND_COMMAND);
    input.mutable_command()->set_type(type);
    input.mutable_command()->set_id(id);
    return Eval(std::move(input));
  }

  commands::Output ConvertKikai() {
    return SendKeys({"k", "i", "k", "a", "i", "Space"});
  }

  // Converts きかい, which zenz corrects to 機械, leaves the correction for
  // Mozc's 機会 with Space and commits that as it is.
  void LeaveZenzsKikaiForMozcs() {
    ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
    ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
    ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  }

  std::string answers_;
  std::unique_ptr<floatype::ScorerClient> client_;
  std::unique_ptr<SessionHandler> handler_;
  uint64_t id_ = 0;
};

// What the user turns down, with zenz teaching Mozc nothing (the default) and
// with the setting zenz_teaches_mozc on.
class ZenzTurnedDownTest : public FloatypeZenzTest,
                           public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    FloatypeZenzTest::SetUp();
    SetZenzTeachesMozc(GetParam());
  }
};

INSTANTIATE_TEST_SUITE_P(ZenzTeachesMozc, ZenzTurnedDownTest,
                         ::testing::Bool());

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

TEST_F(FloatypeZenzTest, TeachesMozcZenzsCorrectionWhereSetTo) {
  SetZenzTeachesMozc(true);
  PollWhileAsked(ConvertKikai());
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機械");
}

TEST_F(FloatypeZenzTest, BackspaceTakesBackWhatZenzTaughtMozc) {
  SetZenzTeachesMozc(true);
  PollWhileAsked(ConvertKikai());
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  SendKeys({"Backspace"});

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機会");
}

TEST_F(FloatypeZenzTest, ZenzTeachesMozcNothingInIncognitoMode) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_zenz_teaches_mozc(true);
  config.set_incognito_mode(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  PollWhileAsked(ConvertKikai());
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");

  SetZenzTeachesMozc(true);  // Incognito mode off again.
  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機会");
}

// What zenz taught is in the history like what the user chose, so zenz may
// change a word the history ranked first back to the one it displaced: 機械,
// zenz's after あ and learned there, is 機会 again elsewhere.
TEST_F(FloatypeZenzTest, ZenzChangesWhatItTaughtMozc) {
  // The fake turns キカイ into 機械 only after あ.
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機会\nあ|キカイ\t機械\n"));
  SetZenzTeachesMozc(true);
  ASSERT_EQ(SendKeys({"a", "Enter"}).result().value(), "あ");
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");

  StartSession(/*zenz=*/true);
  const commands::Output converted = ConvertKikai();
  ASSERT_EQ(Preedit(converted), "機械") << "learned: shown before zenz";
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機会");
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

// Mozc's test data splits ひゃくなな into ひゃく|なな (百|ナナ), with 百 and 七
// among the candidates and, with digits half width by default, half-width
// digits ranked first, so zenz's 107 shows on one segment, segmented again.
TEST_F(FloatypeZenzTest, WritesInDigitsANumberMozcSplitIntoKanjiNumerals) {
  ASSERT_OK(FileUtil::SetContents(answers_, "ヒャクナナ\t107\n"));
  const commands::Output converted =
      SendKeys({"h", "y", "a", "k", "u", "n", "a", "n", "a", "Space"});
  ASSERT_EQ(Preedit(converted), "百ナナ");
  ASSERT_EQ(converted.preedit().segment_size(), 2);
  const commands::Output corrected = PollWhileAsked(converted);
  EXPECT_EQ(Preedit(corrected), "107");
  EXPECT_EQ(corrected.preedit().segment_size(), 1);

  const commands::Output mozc = SendKeys({"Space"});
  EXPECT_EQ(Preedit(mozc), "百ナナ") << "Space brings back Mozc's segments";
  EXPECT_EQ(mozc.preedit().segment_size(), 2);
}

TEST_F(FloatypeZenzTest, DoesNotTeachMozcTheSegmentsOfZenzsCorrection) {
  ASSERT_OK(FileUtil::SetContents(answers_, "ヒャクナナ\t107\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(SendKeys(
                {"h", "y", "a", "k", "u", "n", "a", "n", "a", "Space"}))),
            "107");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "107");

  StartSession(/*zenz=*/false);
  const commands::Output mozc =
      SendKeys({"h", "y", "a", "k", "u", "n", "a", "n", "a", "Space"});
  EXPECT_EQ(Preedit(mozc), "百ナナ");
  EXPECT_EQ(mozc.preedit().segment_size(), 2) << "ひゃく|なな, not ひゃくなな";
}

TEST_F(FloatypeZenzTest, ACommittedCorrectionOnNewSegmentsShowsAtOnceNextTime) {
  ASSERT_OK(FileUtil::SetContents(answers_, "ヒャクナナ\t107\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(SendKeys(
                {"h", "y", "a", "k", "u", "n", "a", "n", "a", "Space"}))),
            "107");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "107");
  StartSession(/*zenz=*/true);

  // Right at Space, before zenz answers.
  EXPECT_EQ(Preedit(SendKeys(
                {"h", "y", "a", "k", "u", "n", "a", "n", "a", "Space"})),
            "107");
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "百ナナ");
}

// Mozc's test data leaves ひゃくせんねん in kana, with no candidate in kanji,
// but 百, 千 and 年 are words of its dictionary with those readings, so zenz's
// 百千年 shows as zenz wrote it.
TEST_F(FloatypeZenzTest, ShowsZenzsOwnWritingWhereTheDictionaryReadsIt) {
  ASSERT_OK(FileUtil::SetContents(answers_, "ヒャクセンネン\t百千年\n"));
  const commands::Output converted = SendKeys(
      {"h", "y", "a", "k", "u", "s", "e", "n", "n", "n", "e", "n", "n", "Space"});
  ASSERT_EQ(Preedit(converted), "ひゃくせんねん");
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "百千年");
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "ひゃくせんねん")
      << "Space brings back Mozc's conversion";
}

TEST_F(FloatypeZenzTest, DoesNotShowWritingTheDictionaryDoesNotRead) {
  // 念 is not a word read せん in the test data.
  ASSERT_OK(FileUtil::SetContents(answers_, "ヒャクセンネン\t百念年\n"));
  const commands::Output converted = SendKeys(
      {"h", "y", "a", "k", "u", "s", "e", "n", "n", "n", "e", "n", "n", "Space"});
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "ひゃくせんねん");
}

TEST_F(FloatypeZenzTest, DoesNotTeachMozcZenzsOwnWriting) {
  ASSERT_OK(FileUtil::SetContents(answers_, "ヒャクセンネン\t百千年\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(SendKeys({"h", "y", "a", "k", "u", "s", "e",
                                             "n", "n", "n", "e", "n", "n",
                                             "Space"}))),
            "百千年");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "百千年");

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(SendKeys({"h", "y", "a", "k", "u", "s", "e", "n", "n", "n",
                              "e", "n", "n", "Space"})),
            "ひゃくせんねん");
}

TEST_F(FloatypeZenzTest,
       SpaceOnZenzsCorrectionShowsMozcsCandidatesInMozcsOrder) {
  // 既介 is far down Mozc's candidates of きかい: 機会, 機械, ...
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t既介\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "既介");

  // Mozc's result again, with no candidates open yet.
  const commands::Output mozc = SendKeys({"Space"});
  EXPECT_EQ(Preedit(mozc), "機会");
  EXPECT_FALSE(mozc.has_candidate_window());
  // Then Mozc's second: the correction left behind does not come next.
  const commands::Output next = SendKeys({"Space"});
  EXPECT_EQ(Preedit(next), "機械");
  EXPECT_TRUE(next.has_candidate_window());
}

TEST_P(ZenzTurnedDownTest, PickingZenzsWordAfterShowingMozcIsLearned) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機械") << "Mozc's second";
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  // The user chose the word after all: it is not refused.
  EXPECT_FALSE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));
  EXPECT_FALSE(floatype::ZenzMemory::RefusedOnce().Contains("きかい", "機械"));

  StartSession(/*zenz=*/false);
  EXPECT_EQ(Preedit(ConvertKikai()), "機械");
}

TEST_F(FloatypeZenzTest, SpaceOnALiveCorrectionShowsMozcsResultAndStaysLive) {
  floatype::EnableSessionFeatures(true);
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(live))), "機械");

  const commands::Output mozc = SendKeys({"Space"});
  EXPECT_EQ(Preedit(mozc), "機会");
  EXPECT_EQ(Indicator(mozc), commands::Output::Indicator::MOZC);
  // Typing on edits the reading, as after any live conversion.
  EXPECT_EQ(Preedit(SendKeys({"g", "a"})), "機会が");
}

TEST_P(ZenzTurnedDownTest, LeavingALiveCorrectionTwiceTurnsItDown) {
  floatype::EnableSessionFeatures(true);
  for (int i = 0; i < 2; ++i) {
    const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
    ASSERT_EQ(Preedit(PollWhileAsked(CallBack(live))), "機械");
    ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
    ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  }
  EXPECT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));

  // The next live conversion keeps Mozc's 機会.
  const commands::Output again = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  EXPECT_EQ(Preedit(PollWhileAsked(CallBack(again))), "機会");
}

TEST_F(FloatypeZenzTest, TheZenzKeyKeepsZenzsCorrectionWhenNothingElseIsLeft) {
  floatype::EnableSessionFeatures(true);
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  const commands::Output again = PollWhileAsked(SendKeys({"Ctrl z"}));
  EXPECT_EQ(Preedit(again), "機械");
  EXPECT_EQ(Indicator(again), commands::Output::Indicator::ZENZ);
}

TEST_F(FloatypeZenzTest, TheZenzKeyOnZenzsCorrectionShowsItsNextBest) {
  floatype::EnableSessionFeatures(true);
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機械\t既介\t機会\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");

  const commands::Output asked = SendKeys({"Ctrl z"});
  EXPECT_EQ(Preedit(asked), "機械") << "shown until zenz answers";
  EXPECT_EQ(Indicator(asked), commands::Output::Indicator::ZENZ_PENDING);
  const commands::Output next = PollWhileAsked(asked);
  EXPECT_EQ(Preedit(next), "既介");
  EXPECT_EQ(Indicator(next), commands::Output::Indicator::ZENZ);

  // zenz's next is Mozc's own conversion, which is Mozc's again.
  const commands::Output mozc = PollWhileAsked(SendKeys({"Ctrl z"}));
  EXPECT_EQ(Preedit(mozc), "機会");
  EXPECT_EQ(Indicator(mozc), commands::Output::Indicator::MOZC);
  // Nothing is left: what is shown stays.
  EXPECT_EQ(Preedit(PollWhileAsked(SendKeys({"Ctrl z"}))), "機会");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "機会");

  // A new conversion starts over.
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  EXPECT_EQ(Preedit(PollWhileAsked(SendKeys({"Ctrl z"}))), "既介");
}

TEST_F(FloatypeZenzTest, SpaceAfterTheNextBestShowsMozcsConversion) {
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機械\t既介\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(Preedit(PollWhileAsked(SendKeys({"Ctrl z"}))), "既介");
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "機会");
}

TEST_F(FloatypeZenzTest, TheZenzKeyAfterZenzAgreedShowsItsNextBest) {
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機会\t機械\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  EXPECT_EQ(Preedit(PollWhileAsked(SendKeys({"Ctrl z"}))), "機械");
}

TEST_F(FloatypeZenzTest, TheZenzKeyAfterADroppedAnswerShowsTheNextBest) {
  // Mozc cannot write 奇怪 here, so zenz's first answer is dropped.
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t奇怪\t機械\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  EXPECT_EQ(Preedit(PollWhileAsked(SendKeys({"Ctrl z"}))), "機械");
}

TEST_F(FloatypeZenzTest, TheZenzKeySkipsACorrectionTheUserStopped) {
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機械\t既介\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(Preedit(SendKeys({"Ctrl Delete"})), "機会");
  EXPECT_EQ(Preedit(PollWhileAsked(SendKeys({"Ctrl z"}))), "既介");
}

TEST_F(FloatypeZenzTest, TheZenzKeyAfterATimeoutAsksAgain) {
  // The fake answers readings starting with スロウ after 200ms, but at once
  // to "not this".
  ASSERT_OK(FileUtil::SetContents(answers_, "スロウキカイ\tすろう機会\n"));
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_zenz_timeout_millisec(50);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  ASSERT_EQ(Preedit(PollWhileAsked(SendKeys(
                {"s", "u", "r", "o", "u", "k", "i", "k", "a", "i", "Space"}))),
            "すろう機械")
      << "zenz's answer came too late";
  // The late answer is out of the way before the key.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(Preedit(PollWhileAsked(SendKeys({"Ctrl z"}))), "すろう機会");
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

TEST_F(FloatypeZenzTest, TeachingMozcStillKeepsCorrectionsOutOfSuggestions) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_zenz_teaches_mozc(true);
  config.set_zenz_suggestion_learning(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  CommitCorrectedSentence();
  EXPECT_FALSE(Suggests(TypeKikaiga(), "機械が機械"));
}

// Picking a suggestion is a choice like picking candidates: Mozc's ranking
// learns the words that its own conversion of the reading can write. Only
// きかいが is typed, so it is the suggestion's whole reading that converts.
TEST_F(FloatypeZenzTest, PickingASuggestionTeachesMozcItsWords) {
  SuggestTheCorrectedSentence();
  ASSERT_TRUE(Suggests(TypeKikaiga(), "機械が機械"));
  SelectPrediction("機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  EXPECT_EQ(MozcsKikai(), "機械");
}

TEST_F(FloatypeZenzTest, BackspaceRightAfterPickingASuggestionTakesItBack) {
  SuggestTheCorrectedSentence();
  TypeKikaiga();
  SelectPrediction("機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  SendKeys({"Backspace"});
  EXPECT_EQ(MozcsKikai(), "機会");
}

TEST_F(FloatypeZenzTest, UndoAfterPickingASuggestionTakesItBack) {
  SuggestTheCorrectedSentence();
  TypeKikaiga();
  SelectPrediction("機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  // The key asks the client to call back with the Undo command.
  CallBack(SendKeys({"Ctrl Backspace"}));
  EXPECT_EQ(MozcsKikai(), "機会");
}

TEST_F(FloatypeZenzTest, RevertAfterPickingASuggestionTakesItBack) {
  SuggestTheCorrectedSentence();
  TypeKikaiga();
  SelectPrediction("機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  SendCommand(commands::SessionCommand::REVERT);
  EXPECT_EQ(MozcsKikai(), "機会");
}

// What the macOS candidate window sends on a click, with no candidate
// focused.
TEST_F(FloatypeZenzTest, ClickingASuggestionTeachesMozcItsWords) {
  SuggestTheCorrectedSentence();
  const int id = SuggestionId(TypeKikaiga(), "機械が機械");
  ASSERT_EQ(SendCommand(commands::SessionCommand::SUBMIT_CANDIDATE, id)
                .result()
                .value(),
            "機械が機械");
  EXPECT_EQ(MozcsKikai(), "機械");
}

TEST_F(FloatypeZenzTest, SelectingASuggestionAndEnterTeachesMozcItsWords) {
  SuggestTheCorrectedSentence();
  const int id = SuggestionId(TypeKikaiga(), "機械が機械");
  ASSERT_EQ(
      Preedit(SendCommand(commands::SessionCommand::SELECT_CANDIDATE, id)),
      "機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  EXPECT_EQ(MozcsKikai(), "機械");
}

// With a candidate of the list focused, a click commits through the
// converter's commit of the focused segment.
TEST_F(FloatypeZenzTest, ClickingAFocusedSuggestionTeachesMozcItsWords) {
  SuggestTheCorrectedSentence();
  TypeKikaiga();
  const commands::Output tab = SendKeys({"Tab"});
  ASSERT_EQ(tab.candidate_window().category(), commands::PREDICTION);
  int id = -1;
  for (const commands::CandidateWindow::Candidate& candidate :
       tab.candidate_window().candidate()) {
    if (candidate.value() == "機械が機械") {
      id = candidate.id();
    }
  }
  ASSERT_GE(id, 0);
  ASSERT_EQ(SendCommand(commands::SessionCommand::SUBMIT_CANDIDATE, id)
                .result()
                .value(),
            "機械が機械");
  EXPECT_EQ(MozcsKikai(), "機械");
}

// While a live conversion shows, the suggestions stay in their window and a
// click on one commits it.
TEST_F(FloatypeZenzTest, ClickingASuggestionOverALiveConversionTeachesMozc) {
  SuggestTheCorrectedSentence();
  floatype::EnableSessionFeatures(true);
  const commands::Output live = CallBack(TypeKikaiga());
  int id = -1;
  for (const commands::CandidateWindow::Candidate& candidate :
       live.candidate_window().candidate()) {
    if (candidate.value() == "機械が機械") {
      id = candidate.id();
    }
  }
  ASSERT_GE(id, 0) << "the suggestions stay after the live conversion";
  ASSERT_EQ(SendCommand(commands::SessionCommand::SUBMIT_CANDIDATE, id)
                .result()
                .value(),
            "機械が機械");
  floatype::EnableSessionFeatures(false);
  EXPECT_EQ(MozcsKikai(), "機械");
}

// The word a picked suggestion displaces is not among the turned-down words.
// What the history ranked first is kept by the rules about it, which are off
// where zenz teaches Mozc.
TEST_F(FloatypeZenzTest, ZenzLeavesAPickedSuggestionsWordUnlessItTeachesMozc) {
  // The fake also answers 機会 for きかい.
  CommitCorrectedSentence("キカイ\t機会\n");
  StartSession(/*zenz=*/false);
  TypeKikaiga();
  SelectPrediction("機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  EXPECT_FALSE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機会"));

  StartSession(/*zenz=*/true);
  commands::Output converted = ConvertKikai();
  ASSERT_EQ(Preedit(converted), "機械") << "learned from the pick";
  ASSERT_TRUE(converted.has_callback());
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機械");

  SetZenzTeachesMozc(true);
  StartSession(/*zenz=*/true);
  converted = ConvertKikai();
  ASSERT_EQ(Preedit(converted), "機械");
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機会");
}

TEST_F(FloatypeZenzTest, PickingASuggestionByItsNumberTeachesMozcItsWords) {
  SuggestTheCorrectedSentence();
  TypeKikaiga();
  const commands::Output tab = SendKeys({"Tab"});
  int number = 0;
  for (int i = 0; i < tab.candidate_window().candidate_size(); ++i) {
    if (tab.candidate_window().candidate(i).value() == "機械が機械") {
      number = i + 1;
    }
  }
  ASSERT_GE(number, 1);
  ASSERT_LE(number, 9);
  const std::string key = std::to_string(number);
  ASSERT_EQ(Preedit(SendKeys({key})), "機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  EXPECT_EQ(MozcsKikai(), "機械");
}

TEST_F(FloatypeZenzTest, PickingASuggestionAfterALiveConversionTeachesMozc) {
  SuggestTheCorrectedSentence();
  floatype::EnableSessionFeatures(true);
  const commands::Output typed = TypeKikaiga();
  ASSERT_TRUE(Suggests(typed, "機械が機械"));
  CallBack(typed);  // The live conversion.
  SelectPrediction("機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  floatype::EnableSessionFeatures(false);
  EXPECT_EQ(MozcsKikai(), "機械");
}

// Where the history is read and not written, the suggestion still shows and
// picking it teaches nothing. (In incognito mode nothing from the history is
// suggested; the ranking refuses to learn there by the check next to this
// one.)
TEST_F(FloatypeZenzTest, PickingASuggestionTeachesNothingWithLearningReadOnly) {
  SuggestTheCorrectedSentence();
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_history_learning_level(config::Config::READ_ONLY);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  ASSERT_TRUE(Suggests(TypeKikaiga(), "機械が機械"));
  SelectPrediction("機械が機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械が機械");
  SetZenzTeachesMozc(false);  // The default settings: learning on again.
  EXPECT_EQ(MozcsKikai(), "機会");
}

// zenz's own 百千年 is suggested once committed, and Mozc's conversion of
// ひゃくせんねん has no candidate that writes it: nothing is guessed at.
TEST_F(FloatypeZenzTest, PickingASuggestionMozcCannotWriteTeachesNothing) {
  ASSERT_OK(FileUtil::SetContents(answers_, "ヒャクセンネン\t百千年\n"));
  const std::initializer_list<absl::string_view> reading = {
      "h", "y", "a", "k", "u", "s", "e", "n", "n", "n", "e", "n", "n"};
  SendKeys(reading);
  ASSERT_EQ(Preedit(PollWhileAsked(SendKeys({"Space"}))), "百千年");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "百千年");
  StartSession(/*zenz=*/false);
  SendKeys(reading);
  const std::string mozc = Preedit(SendKeys({"Space"}));
  ASSERT_NE(mozc, "百千年");

  StartSession(/*zenz=*/false);
  ASSERT_TRUE(Suggests(SendKeys({"h", "y", "a", "k", "u"}), "百千年"));
  SelectPrediction("百千年");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "百千年");
  StartSession(/*zenz=*/false);
  SendKeys(reading);
  EXPECT_EQ(Preedit(SendKeys({"Space"})), mozc);
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

TEST_F(FloatypeZenzTest, ALiveConversionConvertsAfreshAsMozkeyDoes) {
  floatype::EnableSessionFeatures(true);
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機械\nキカイガ\t機械が\n"));
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(live))), "機械");
  // While typing on, what was shown stays in front of the new kana.
  const commands::Output typed = SendKeys({"g", "a"});
  EXPECT_EQ(Preedit(typed), "機械が");
  // The pause converts the whole reading afresh; zenz then corrects it again.
  const commands::Output converted = CallBack(typed);
  EXPECT_EQ(Preedit(converted), "機会が");
  EXPECT_EQ(Preedit(PollWhileAsked(CallBack(converted))), "機械が");
}

TEST_F(FloatypeZenzTest, ZenzCorrectsWithTheMemorySetting) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_zenz_stop_when_idle(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  // The first key starts the scorer with these settings, and the correction
  // after the pause comes from it.
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  EXPECT_EQ(Preedit(PollWhileAsked(CallBack(live))), "機械");
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
  // Reading until live conversion takes the composition (two characters).
  EXPECT_EQ(Indicator(SendKeys({"k", "i"})),
            commands::Output::Indicator::READING);
  const commands::Output typed = SendKeys({"k", "a", "i"});
  EXPECT_EQ(Indicator(typed), commands::Output::Indicator::MOZC);
  EXPECT_EQ(typed.indicator().style().mozc_color(), 0x586b85u);
  // The live conversion books zenz: zenz's from then on, through the delay.
  EXPECT_EQ(Indicator(CallBack(typed)),
            commands::Output::Indicator::ZENZ_PENDING);
  SendKeys({"Escape", "Escape"});
  SendKeys({"k", "i", "k", "a", "i"});

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
  EXPECT_EQ(Preedit(k), "きかいk");
  EXPECT_EQ(Suggestions(k), shown);
  const commands::Output ky = SendKeys({"y"});
  EXPECT_EQ(Preedit(ky), "きかいky");
  EXPECT_EQ(Suggestions(ky), shown) << "Mozc alone suggests one candidate";
  // A finished sequence suggests afresh.
  EXPECT_EQ(Preedit(SendKeys({"o"})), "きかいきょ");
}

// Letters that romaji left over, and a period beside digits, are typed half
// width by default; the width table can still make the letters full width.
TEST_F(FloatypeZenzTest, TypesLettersAndPeriodsBesideDigitsHalfWidth) {
  floatype::EnableSessionFeatures(true);
  EXPECT_EQ(Preedit(SendKeys({"u", "p", "d"})), "うpd");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "うpd");
  EXPECT_EQ(Preedit(SendKeys({"v", "1", ".", "2"})), "v1.2");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "v1.2");

  config::Config config = config::ConfigHandler::DefaultConfig();
  for (config::Config::CharacterFormRule& rule :
       *config.mutable_character_form_rules()) {
    if (rule.group() == "A") {
      rule.set_preedit_character_form(config::Config::FULL_WIDTH);
    }
  }
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  EXPECT_EQ(Preedit(SendKeys({"u", "p", "d"})), "うｐｄ");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "うｐｄ");
}

TEST_F(FloatypeZenzTest, PickingAHeldSuggestionCommitsItAsMozcWould) {
  floatype::EnableSessionFeatures(true);
  SendKeys({"k", "i", "k", "a", "i", "k", "y"});
  // Mozc alone suggests 機会 for きかいky too, dropping the unfinished ky.
  ASSERT_EQ(Preedit(SendKeys({"Down"})), "機会");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  // Enter without picking commits the composition as typed.
  SendKeys({"k", "i", "k", "a", "i", "k", "y"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "きかいky");
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

// Enter with nothing composed goes to the app, and Mozc drops what was
// committed: the line before does not stand in.
TEST_F(FloatypeZenzTest, NothingStandsInAfterEnterEndedTheLine) {
  ASSERT_OK(
      FileUtil::SetContents(answers_, "キカイ\t機会\n機会|キカイ\t機械\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  EXPECT_FALSE(SendKeys({"Enter"}).consumed()) << "the app ends the line";
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
}

// The app says that nothing precedes the caret: Mozc drops what was
// committed, and nothing stands in.
TEST_F(FloatypeZenzTest, NothingStandsInWhereTheAppTellsWhatPrecedes) {
  ASSERT_OK(
      FileUtil::SetContents(answers_, "キカイ\t機会\n機会|キカイ\t機械\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  commands::Input first;
  first.set_type(commands::Input::SEND_KEY);
  ASSERT_TRUE(KeyParser::ParseKey("k", first.mutable_key()));
  first.mutable_context()->set_preceding_text("");
  Eval(std::move(first));
  EXPECT_EQ(Preedit(PollWhileAsked(SendKeys({"i", "k", "a", "i", "Space"}))),
            "機会");
}

TEST_F(FloatypeZenzTest, NothingStandsInWithTheSettingOff) {
  ASSERT_OK(
      FileUtil::SetContents(answers_, "キカイ\t機会\n機会|キカイ\t機械\n"));
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_zenz_context_from_commits(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
}

// What was typed with secure input on is not shown to zenz once it is off.
TEST_F(FloatypeZenzTest, WhatWasCommittedInSecureInputDoesNotStandIn) {
  ASSERT_OK(
      FileUtil::SetContents(answers_, "キカイ\t機会\n機会|キカイ\t機械\n"));
  floatype::ZenzCorrector::PretendSecureInputForTesting(true);
  ASSERT_EQ(Preedit(ConvertKikai()), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  floatype::ZenzCorrector::PretendSecureInputForTesting(false);
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  // Once the field is left, what is committed stands in again.
  commands::Input reset;
  reset.set_type(commands::Input::SEND_COMMAND);
  reset.mutable_command()->set_type(commands::SessionCommand::RESET_CONTEXT);
  Eval(std::move(reset));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
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

TEST_F(FloatypeZenzTest, ALiveConversionKeepsTheSuggestionsShown) {
  floatype::EnableSessionFeatures(true);
  const commands::Output typed = SendKeys({"k", "i", "k", "a", "i"});
  const std::vector<std::string> shown = Suggestions(typed);
  ASSERT_GE(shown.size(), 2);
  const commands::Output live = CallBack(typed);
  ASSERT_EQ(Preedit(live), "機会");
  EXPECT_EQ(live.candidate_window().category(), commands::SUGGESTION);
  EXPECT_FALSE(live.candidate_window().has_focused_index());
  EXPECT_EQ(Suggestions(live), shown);
  // They stay through zenz's correction.
  const commands::Output corrected = PollWhileAsked(CallBack(live));
  ASSERT_EQ(Preedit(corrected), "機械");
  EXPECT_EQ(Suggestions(corrected), shown);
  // Typing on suggests for the longer reading as usual.
  EXPECT_EQ(Preedit(SendKeys({"g", "a"})), "機械が");
}

TEST_F(FloatypeZenzTest, SuggestionsShowWhileALiveConversionWaitsForZenz) {
  floatype::EnableSessionFeatures(true);
  SetLiveConversionWait(250);
  const commands::Output typed = SendKeys({"k", "i", "k", "a", "i"});
  const commands::Output waiting = CallBack(typed);
  ASSERT_EQ(Preedit(waiting), "きかい");
  EXPECT_EQ(Suggestions(waiting), Suggestions(typed));
  EXPECT_EQ(Suggestions(PollWhileAsked(waiting)), Suggestions(typed));
}

TEST_F(FloatypeZenzTest, PickingASuggestionAfterTheLiveConversionCommitsIt) {
  floatype::EnableSessionFeatures(true);
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  // The third suggestion, 機械化, shares its id with the conversion's きかい.
  ASSERT_GE(live.candidate_window().candidate_size(), 3);
  const commands::CandidateWindow::Candidate& third =
      live.candidate_window().candidate(2);
  ASSERT_EQ(third.value(), "機械化");
  commands::Input input;
  input.set_type(commands::Input::SEND_COMMAND);
  input.mutable_command()->set_type(commands::SessionCommand::SUBMIT_CANDIDATE);
  input.mutable_command()->set_id(third.id());
  EXPECT_EQ(Eval(std::move(input)).result().value(), "機械化");
}

TEST_F(FloatypeZenzTest, ClickingASuggestionAfterTheLiveConversionPicksIt) {
  floatype::EnableSessionFeatures(true);
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  ASSERT_GE(live.candidate_window().candidate_size(), 3);
  const commands::CandidateWindow::Candidate& third =
      live.candidate_window().candidate(2);
  ASSERT_EQ(third.value(), "機械化");
  // What the macOS candidate window sends on a click.
  commands::Input input;
  input.set_type(commands::Input::SEND_COMMAND);
  input.mutable_command()->set_type(commands::SessionCommand::SELECT_CANDIDATE);
  input.mutable_command()->set_id(third.id());
  EXPECT_EQ(Preedit(Eval(std::move(input))), "機械化");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "機械化");
}

TEST_F(FloatypeZenzTest, TabAfterTheLiveConversionGoesIntoTheSuggestions) {
  floatype::EnableSessionFeatures(true);
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  const commands::Output tab = SendKeys({"Tab"});
  EXPECT_EQ(tab.candidate_window().category(), commands::PREDICTION);
  EXPECT_EQ(Preedit(tab), live.candidate_window().candidate(0).value());
  // There the number keys pick, as Mozc's shortcuts do.
  EXPECT_EQ(Preedit(SendKeys({"2"})),
            live.candidate_window().candidate(1).value());
  EXPECT_EQ(SendKeys({"Enter"}).result().value(),
            live.candidate_window().candidate(1).value());
  // Down goes there too, rather than to the conversion's candidates.
  const commands::Output again = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  const commands::Output down = SendKeys({"Down"});
  EXPECT_EQ(down.candidate_window().category(), commands::PREDICTION);
  EXPECT_EQ(Preedit(down), again.candidate_window().candidate(0).value());
}

TEST_F(FloatypeZenzTest, SpaceAfterTheLiveConversionStillConverts) {
  floatype::EnableSessionFeatures(true);
  CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  const commands::Output space = SendKeys({"Space"});
  EXPECT_EQ(space.candidate_window().category(), commands::CONVERSION);
  EXPECT_TRUE(space.candidate_window().has_focused_index());
}

TEST_F(FloatypeZenzTest, TheSuggestionsGetTheirStyle) {
  floatype::EnableSessionFeatures(true);
  EXPECT_FALSE(SendKeys({"k", "i", "k", "a", "i"}).has_suggest_style())
      << "the default look is left unsaid";
  SendKeys({"Escape"});
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.mutable_suggest_style()->set_size_percent(150);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const commands::Output suggested = SendKeys({"k", "i", "k", "a", "i"});
  ASSERT_TRUE(suggested.has_candidate_window());
  EXPECT_EQ(suggested.suggest_style().size_percent(), 150);
  EXPECT_FALSE(suggested.has_candidate_style());
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

TEST_F(FloatypeZenzTest, ARememberedCorrectionShowsAtOnceFromTheLengthSet) {
  CommitCorrectedSentence();  // きかいがきかい: seven characters.
  const auto convert = [this] {
    return SendKeys(
        {"k", "i", "k", "a", "i", "g", "a", "k", "i", "k", "a", "i", "Space"});
  };
  SetRememberedMinChars(7);
  StartSession(/*zenz=*/true);
  EXPECT_EQ(Preedit(convert()), "機械が機械");

  SetRememberedMinChars(8);
  StartSession(/*zenz=*/true);
  const commands::Output converted = convert();
  EXPECT_NE(Preedit(converted), "機械が機械") << "not before zenz answers";
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機械が機械");
}

TEST_F(FloatypeZenzTest, AShortCorrectionIsRememberedWhileNotShownAtOnce) {
  SetRememberedMinChars(7);
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  StartSession(/*zenz=*/true);
  const commands::Output converted = ConvertKikai();
  EXPECT_EQ(Preedit(converted), "機会") << "three characters: not at once";
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機械");

  // It was remembered all along.
  SetRememberedMinChars(1);
  StartSession(/*zenz=*/true);
  EXPECT_EQ(Preedit(ConvertKikai()), "機械");
}

// Incognito mode writes nothing down: neither a correction committed as it
// was shown, nor one stopped (which is still stopped for the moment).
TEST_F(FloatypeZenzTest, IncognitoModeRemembersNoCorrection) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_incognito_mode(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  EXPECT_FALSE(floatype::ZenzMemory::Accepted().Contains("きかい", "機械"));

  // In a new session: after 機械, Mozc's test data writes 機械 by itself.
  StartSession(/*zenz=*/true);
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  EXPECT_EQ(Preedit(SendKeys({"Ctrl Delete"})), "機会") << "stopped for now";
  EXPECT_FALSE(floatype::ZenzMemory::Stopped().Contains("きかい", "機械"));
}

// As Mozc leaves its history unused in incognito mode, a correction
// committed before is not shown ahead of zenz's answer there.
TEST_F(FloatypeZenzTest, IncognitoModeShowsNoRememberedCorrectionAtOnce) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  ASSERT_TRUE(floatype::ZenzMemory::Accepted().Contains("きかい", "機械"));

  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_incognito_mode(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  StartSession(/*zenz=*/true);
  const commands::Output converted = ConvertKikai();
  EXPECT_EQ(Preedit(converted), "機会");
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機械") << "zenz still answers";
}

// With the history read and not written, zenz's memory is not written
// either, and what it holds is still used.
TEST_F(FloatypeZenzTest, ReadOnlyLearningWritesNoCorrectionAndShowsOldOnes) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  ASSERT_TRUE(floatype::ZenzMemory::Accepted().Contains("きかい", "機械"));
  floatype::ZenzMemory::Accepted().Clear();

  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_history_learning_level(config::Config::READ_ONLY);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  StartSession(/*zenz=*/true);
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  EXPECT_FALSE(floatype::ZenzMemory::Accepted().Contains("きかい", "機械"));

  floatype::ZenzMemory::Accepted().Add("きかい", "機械");
  StartSession(/*zenz=*/true);
  EXPECT_EQ(Preedit(ConvertKikai()), "機械") << "shown from memory";
}

// What the user stopped stays away in incognito mode too.
TEST_F(FloatypeZenzTest, IncognitoModeKeepsAStoppedCorrectionAway) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(Preedit(SendKeys({"Ctrl Delete"})), "機会");
  ASSERT_TRUE(floatype::ZenzMemory::Stopped().Contains("きかい", "機械"));
  SendKeys({"Escape", "Escape"});

  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_incognito_mode(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
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

TEST_F(FloatypeZenzTest, CommittingMozcsCandidateRemembersNoCorrection) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  StartSession(/*zenz=*/true);
  EXPECT_EQ(Preedit(ConvertKikai()), "機会") << "not shown before zenz answers";
}

TEST_P(ZenzTurnedDownTest, LeavingACorrectionForMozcsResultTwiceTurnsItDown) {
  LeaveZenzsKikaiForMozcs();
  EXPECT_TRUE(floatype::ZenzMemory::RefusedOnce().Contains("きかい", "機械"));
  EXPECT_FALSE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));

  // Once may have been a slip of the Space key: zenz writes 機械 again.
  LeaveZenzsKikaiForMozcs();
  EXPECT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));
  EXPECT_FALSE(floatype::ZenzMemory::RefusedOnce().Contains("きかい", "機械"));

  // zenz still answers 機械, which is no longer shown.
  const commands::Output converted = ConvertKikai();
  ASSERT_TRUE(converted.has_callback());
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機会");
}

TEST_P(ZenzTurnedDownTest,
       ChoosingAnotherCandidateAfterSpaceTurnsItDownAtOnce) {
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t既介\n"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "既介");
  ASSERT_EQ(Preedit(SendKeys({"Space", "Space"})), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  EXPECT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "既介"));

  // zenz still answers 既介; Mozc's result, 機械 from now on, stays.
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
}

TEST_P(ZenzTurnedDownTest, TakingTheCorrectionAfterLeavingItOnceStartsOver) {
  LeaveZenzsKikaiForMozcs();
  ASSERT_TRUE(floatype::ZenzMemory::RefusedOnce().Contains("きかい", "機械"));
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  EXPECT_FALSE(floatype::ZenzMemory::RefusedOnce().Contains("きかい", "機械"));
}

// Mozc's test data writes きかいがひゃく as 機会が|百.
TEST_P(ZenzTurnedDownTest, ChoosingOneSegmentAgainLeavesZenzsOtherWordsAlone) {
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイガヒャク\t機械が100\n"));
  ASSERT_EQ(
      Preedit(PollWhileAsked(SendKeys({"k", "i", "k", "a", "i", "g", "a", "h",
                                       "y", "a", "k", "u", "Space"}))),
      "機械が100");
  // Mozc's result again, then another candidate for the first segment.
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会が百");
  SendKeys({"Space"});
  ASSERT_TRUE(SendKeys({"Enter"}).has_result());
  // What was chosen again says which word was wrong; 100 is not it.
  EXPECT_FALSE(floatype::ZenzMemory::TurnedDown().Contains("ひゃく", "100"));
  EXPECT_FALSE(floatype::ZenzMemory::RefusedOnce().Contains("ひゃく", "100"));
}

TEST_P(ZenzTurnedDownTest, ATurnedDownWordStaysAwayInAnotherSentence) {
  // What Mozc alone makes of the other sentence.
  StartSession(/*zenz=*/false);
  const std::string mozc = Preedit(SendKeys(
      {"k", "i", "k", "a", "i", "g", "a", "k", "i", "k", "a", "i", "Space"}));
  ASSERT_NE(mozc, "機械が機械");
  StartSession(/*zenz=*/true);

  // 機械 is turned down in きかい alone.
  LeaveZenzsKikaiForMozcs();
  LeaveZenzsKikaiForMozcs();
  ASSERT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));

  // It is the word that is remembered, so zenz's 機械が is not shown either:
  // Mozc's result stays.
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイガキカイ\t機械が機械\n"));
  const commands::Output corrected = PollWhileAsked(SendKeys(
      {"k", "i", "k", "a", "i", "g", "a", "k", "i", "k", "a", "i", "Space"}));
  EXPECT_EQ(Preedit(corrected), mozc);
}

TEST_P(ZenzTurnedDownTest, ChoosingATurnedDownWordLetsZenzWriteItAgain) {
  LeaveZenzsKikaiForMozcs();
  LeaveZenzsKikaiForMozcs();
  ASSERT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));

  // Choosing 機械 over 機会 turns the two around.
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  EXPECT_FALSE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));
  EXPECT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機会"));
}

TEST_P(ZenzTurnedDownTest, CommittingTheFirstCandidateForgetsNothing) {
  LeaveZenzsKikaiForMozcs();
  LeaveZenzsKikaiForMozcs();
  // 機会 as Mozc shows it, with no choice made.
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機会");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  EXPECT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));
}

TEST_P(ZenzTurnedDownTest,
       BackspaceRightAfterTheCommitTakesBackWhatItToldZenz) {
  LeaveZenzsKikaiForMozcs();
  LeaveZenzsKikaiForMozcs();
  ASSERT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));

  // The second commit goes back: left once, as before it.
  SendKeys({"Backspace"});
  EXPECT_FALSE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));
  EXPECT_TRUE(floatype::ZenzMemory::RefusedOnce().Contains("きかい", "機械"));
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
}

TEST_F(FloatypeZenzTest,
       ARememberedCorrectionIsNotShownOnceItsWordIsTurnedDown) {
  ASSERT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  for (int i = 0; i < 2; ++i) {
    StartSession(/*zenz=*/true);
    ASSERT_EQ(Preedit(ConvertKikai()), "機械") << "shown from memory";
    ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
    ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  }

  StartSession(/*zenz=*/true);
  const commands::Output converted = ConvertKikai();
  EXPECT_EQ(Preedit(converted), "機会");
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機会");
}

TEST_F(FloatypeZenzTest, ZenzMayChangeALearnedCandidateToAnother) {
  // Picking 機械 by hand makes Mozc put it first, over its own 機会.
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  EXPECT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機会"));

  // A candidate that is neither is zenz's to choose: a word learned by
  // mistake does not shut zenz out.
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t既介\n"));
  StartSession(/*zenz=*/true);
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "既介");
}

TEST_F(FloatypeZenzTest, KeepsALearnedCandidateWithNothingRemembered) {
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  // As a word learned before the memory of turned-down words was kept.
  floatype::ZenzMemory::TurnedDown().Clear();

  // Mozc's own first candidate is what the learning displaced.
  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機会\n"));
  StartSession(/*zenz=*/true);
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
}

// What keeps a word the user chose over another is the memory of turned-down
// words, which holds where zenz teaches Mozc too.
TEST_P(ZenzTurnedDownTest, ZenzDoesNotBringBackWhatTheUserChoseOver) {
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  ASSERT_TRUE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機会"));

  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機会\n"));
  StartSession(/*zenz=*/true);
  const commands::Output converted = ConvertKikai();
  ASSERT_EQ(Preedit(converted), "機械");
  ASSERT_TRUE(converted.has_callback());
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機械");
}

// The other side of KeepsALearnedCandidateWithNothingRemembered: where zenz
// teaches Mozc, a word learned with nothing remembered about it is zenz's to
// change, as zenz's own teaching looks the same.
TEST_F(FloatypeZenzTest,
       ChangesALearnedCandidateWithNothingRememberedIfTeaching) {
  SetZenzTeachesMozc(true);
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  floatype::ZenzMemory::TurnedDown().Clear();

  ASSERT_OK(FileUtil::SetContents(answers_, "キカイ\t機会\n"));
  StartSession(/*zenz=*/true);
  const commands::Output converted = ConvertKikai();
  ASSERT_EQ(Preedit(converted), "機械");
  EXPECT_EQ(Preedit(PollWhileAsked(converted)), "機会");
}

TEST_P(ZenzTurnedDownTest, TurnsNothingDownWithLearningOff) {
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_zenz_learning(false);
  config.set_zenz_teaches_mozc(GetParam());
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  LeaveZenzsKikaiForMozcs();
  LeaveZenzsKikaiForMozcs();
  EXPECT_EQ(Preedit(PollWhileAsked(ConvertKikai())), "機械");
}

TEST_P(ZenzTurnedDownTest, SpaceThenTypingOnTurnsNothingDown) {
  floatype::EnableSessionFeatures(true);
  const commands::Output live = CallBack(SendKeys({"k", "i", "k", "a", "i"}));
  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(live))), "機械");
  ASSERT_EQ(Preedit(SendKeys({"Space"})), "機会");
  // The reading goes on, and what is committed is another conversion.
  ASSERT_EQ(Preedit(SendKeys({"g", "a"})), "機会が");
  SendKeys({"Enter"});
  EXPECT_FALSE(floatype::ZenzMemory::TurnedDown().Contains("きかい", "機械"));
  EXPECT_FALSE(floatype::ZenzMemory::RefusedOnce().Contains("きかい", "機械"));
}

// Mozc's own command (DeleteSelectedCandidate), where no correction of
// zenz's is on screen for the key to stop.
TEST_F(FloatypeZenzTest, CtrlDeleteOnACandidateForgetsWhatMozcLearned) {
  StartSession(/*zenz=*/false);
  ASSERT_EQ(Preedit(SendKeys({"k", "i", "k", "a", "i", "Space", "Space"})),
            "機械");
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機械");
  ASSERT_EQ(Preedit(ConvertKikai()), "機械") << "learned";

  SendKeys({"Ctrl Delete"});
  SendKeys({"Escape"});
  SendKeys({"Escape"});
  EXPECT_EQ(Preedit(ConvertKikai()), "機会");
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
    config.set_use_direct_commit(true);
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
  config.set_use_direct_commit(true);
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
  EXPECT_EQ(live.ruby_style().font_size(), 11) << "the style goes with it";
}

TEST_F(FloatypeZenzTest, MixedEnglishKeepsAnEnglishWordInLetters) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  // Its Henkan key converts; Space after an English word types a space.
  config.set_session_keymap(config::Config::MSIME);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  const commands::Output typed =
      SendKeys({"k", "e", "y", "b", "o", "a", "r", "d"});
  EXPECT_NE(Preedit(typed), "keyboard") << "still kana while typing";
  EXPECT_TRUE(typed.indicator().english());
  const commands::Output converted = SendKeys({"Henkan"});
  EXPECT_EQ(Preedit(converted), "keyboard");
  EXPECT_TRUE(converted.indicator().english());
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "keyboard");

  // A Japanese word converted another way afterwards is not taken as English.
  SendKeys({"k", "i", "k", "a", "i"});
  EXPECT_FALSE(SendKeys({"F7"}).indicator().english());
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // Japanese stays Japanese.
  const commands::Output japanese = SendKeys({"k", "i", "k", "a", "i"});
  EXPECT_FALSE(japanese.indicator().english());
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "機会");
}

TEST_F(FloatypeZenzTest, MixedEnglishKeepsAWordWhenTypingOn) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(
                SendKeys({"u", "p", "d", "a", "t", "e"})))),
            "update");
  // Typing on stays in kana, after the word.
  const commands::Output typed = SendKeys({"w", "o"});
  EXPECT_EQ(Preedit(typed), "updateを");
  EXPECT_FALSE(typed.indicator().english()) << "the caret follows を";
  EXPECT_EQ(Preedit(PollWhileAsked(CallBack(typed))), "updateを");
  // Backspace leaves the word in letters while it is whole. A letter off it,
  // it is the keys typed again, so that typing goes on in kana (see
  // MixedEnglishGoesOnInKanaAfterBackspace).
  SendKeys({"Escape"});
  const commands::Output back = SendKeys({"Backspace"});
  EXPECT_EQ(Preedit(back), "update");
  EXPECT_TRUE(back.indicator().english());
  EXPECT_EQ(Preedit(SendKeys({"Backspace"})), "うpだt");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "うpだt");
}

// Enter before the pause that would show the word in letters commits the
// letters too, not the kana and the letters left over of them (あｐだて).
TEST_F(FloatypeZenzTest, MixedEnglishEnterBeforeThePauseCommitsTheWord) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  SendKeys({"u", "p", "d", "a", "t", "e"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "update");
  // Japanese is committed as typed: nothing converted it yet.
  SendKeys({"k", "i", "k", "a", "i"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "きかい");
  // A name with digits and symbols is its keys, not their full-width forms.
  SendKeys({"v", "1", ".", "2", ".", "3"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "v1.2.3");
  // So are letters alone that made no kana.
  SendKeys({"k", "w", "s", "k"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "kwsk");
  // Japanese punctuation and digits alone stay as the settings write them.
  const commands::Output typed = SendKeys({"1", ".", "5"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), Preedit(typed));
}

// The parts of mixed English input that have their own settings, turned off
// one at a time.
TEST_F(FloatypeZenzTest, MixedEnglishWithOnePartTurnedOff) {
  floatype::EnableSessionFeatures(true);
  const auto set = [&](void (config::Config::*part)(bool)) {
    config::Config config = config::ConfigHandler::DefaultConfig();
    config.set_mixed_english(true);
    config.set_use_live_conversion(false);
    if (part != nullptr) {
      (config.*part)(false);
    }
    commands::Input input;
    input.set_type(commands::Input::SET_CONFIG);
    *input.mutable_config() = config;
    Eval(std::move(input));
  };
  const auto type = [&](absl::string_view keys) {
    commands::Output output;
    for (const char key : keys) {
      output = SendKeys({std::string(1, key)});
    }
    return output;
  };
  const auto space = [&]() {
    return PollWhileAsked(SendKeys({"Space"})).result().value();
  };

  // Space converts the word, as Mozc's Space does; Enter commits it with no
  // space after it.
  set(&config::Config::set_mixed_english_space_commits);
  type("update");
  const commands::Output converted = SendKeys({"Space"});
  EXPECT_FALSE(converted.has_result());
  EXPECT_EQ(Preedit(converted), "update");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "update");
  // The other two are as they were.
  type("update");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "update");
  type("/review");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "/review");

  // Enter before the pause commits what the screen shows.
  set(&config::Config::set_mixed_english_enter_commits);
  const std::string typed = Preedit(type("update"));
  EXPECT_NE(typed, "update");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), typed);
  type("update");
  EXPECT_EQ(space(), "update ");

  // The symbol before the first word is the symbol settings'; one between
  // words is as typed.
  set(&config::Config::set_mixed_english_leading_symbols);
  type("/review");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "・review");
  type("--help");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "ーーhelp");
  type("@src/app.ts");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "＠src/app.ts");
  type("src/main");
  EXPECT_EQ(space(), "src/main ");
  type("update");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "update");

  // With all of them on, the head symbol is as typed.
  set(nullptr);
  type("/review");
  EXPECT_EQ(space(), "/review ");
}

// English written word by word: the last word, with Enter before the pause,
// is committed as the pause would have shown it.
TEST_F(FloatypeZenzTest, MixedEnglishEnterBeforeThePauseEndsARunOfEnglish) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  const auto type = [this](bool pause) {
    std::string committed;
    for (const absl::string_view key : {"m", "e", "e", "t", "Space"}) {
      committed += SendKeys({key}).result().value();
    }
    commands::Output last = SendKeys({"y", "o", "u"});
    if (pause) {
      last = PollWhileAsked(CallBack(last));
    }
    committed += SendKeys({"Enter"}).result().value();
    return committed;
  };
  const std::string paused = type(/*pause=*/true);
  EXPECT_EQ(paused, "meet you");
  EXPECT_EQ(type(/*pause=*/false), paused);
}

// Undo after such a commit brings the composition back.
TEST_F(FloatypeZenzTest, MixedEnglishUndoAfterEnterBeforeThePause) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  // The client can delete what was committed, as most apps can; it says so
  // with its first key.
  commands::Input first;
  first.set_type(commands::Input::SEND_KEY);
  ASSERT_TRUE(KeyParser::ParseKey("u", first.mutable_key()));
  first.mutable_capability()->set_text_deletion(
      commands::Capability::DELETE_PRECEDING_TEXT);
  Eval(std::move(first));
  SendKeys({"p", "d", "a", "t", "e"});
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "update");
  const commands::Output undone = CallBack(SendKeys({"Ctrl Backspace"}));
  EXPECT_TRUE(undone.has_deletion_range());
  EXPECT_FALSE(Preedit(undone).empty()) << "the composition is back";
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "update");
}

TEST_F(FloatypeZenzTest, WithoutMixedEnglishEnterCommitsWhatWasTyped) {
  floatype::EnableSessionFeatures(true);
  const commands::Output typed = SendKeys({"u", "p", "d", "a", "t", "e"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), Preedit(typed));
}

TEST_F(FloatypeZenzTest, MixedEnglishKeepsAWordInsideASentence) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  const commands::Output word =
      SendKeys({"k", "y", "o", "u", "h", "a", "u", "p", "d", "a", "t", "e"});
  EXPECT_TRUE(word.indicator().english()) << "update is before the caret";
  const commands::Output typed = SendKeys({"s", "u", "r", "u"});
  EXPECT_FALSE(typed.indicator().english());
  // Half width, as typed, though Mozc writes letters full width by default.
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "今日はupdateする");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "今日はupdateする");

  // email + を shows えまいlwお: the romaji held l and w together.
  SendKeys({"e", "m", "a", "i", "l", "w", "o"});
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "emailを");
}

TEST_F(FloatypeZenzTest, MixedEnglishUndoesAWordWhenTheSyllableGoesOn) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  // A pause inside し: きょうは + s reads "has" until the next key.
  ASSERT_EQ(Preedit(PollWhileAsked(
                CallBack(SendKeys({"k", "y", "o", "u", "h", "a", "s"})))),
            "今日has");
  EXPECT_EQ(Preedit(SendKeys({"i", "r", "u"})), "きょうはしる");
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // A word ending in a consonant still takes kana after it.
  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(
                SendKeys({"k", "e", "y", "b", "o", "a", "r", "d"})))),
            "keyboard");
  EXPECT_EQ(Preedit(PollWhileAsked(CallBack(SendKeys({"w", "o"})))),
            "keyboardを");
}

// Backspace after a word was kept in letters: typing goes on in kana, not in
// letters for good (sits, Backspace, agaki stayed sitagaki).
TEST_F(FloatypeZenzTest, MixedEnglishGoesOnInKanaAfterBackspace) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  // A typo of した that is a word: one letter goes, as it shows, and the rest
  // is the kana typed.
  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(SendKeys({"s", "i", "t", "s"})))),
            "sits");
  EXPECT_EQ(Preedit(SendKeys({"Backspace"})), "しt");
  EXPECT_EQ(Preedit(SendKeys({"a", "g", "a", "k", "i"})), "したがき");
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // The keys before the letter are the kana they typed.
  ASSERT_EQ(
      Preedit(PollWhileAsked(CallBack(SendKeys({"n", "o", "t", "e", "s"})))),
      "notes");
  EXPECT_EQ(Preedit(SendKeys({"Backspace"})), "のて");
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // Kana deleted after a word leaves the word in letters, and the next keys
  // are kana again.
  ASSERT_EQ(Preedit(PollWhileAsked(
                CallBack(SendKeys({"u", "p", "d", "a", "t", "e"})))),
            "update");
  SendKeys({"w", "o"});
  EXPECT_EQ(Preedit(SendKeys({"Backspace"})), "update");
  EXPECT_EQ(Preedit(SendKeys({"n", "o"})), "updateの");
  // Deleted back into the word, it is the keys typed.
  SendKeys({"Backspace"});
  EXPECT_EQ(Preedit(SendKeys({"Backspace"})), "うpだt");
  EXPECT_EQ(Preedit(PollWhileAsked(CallBack(SendKeys({"e", "w", "o"})))),
            "updateを");
}

TEST_F(FloatypeZenzTest, WithoutMixedEnglishAWordInsideJapaneseIsKana) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  SendKeys({"u", "p", "d", "a", "t", "e", "w", "o"});
  EXPECT_EQ(Preedit(SendKeys({"Space"})).find("update"), std::string::npos);
}

// The test data has no word read べふぉれ, so the keys of "before" are the
// word; まで is a word of the dictionary, so "made" stays Japanese.
TEST_F(FloatypeZenzTest, MixedEnglishKeepsAWordTypedWhollyIntoKana) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  // Its Henkan key converts; Space after an English word types a space.
  config.set_session_keymap(config::Config::MSIME);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  const commands::Output typed = SendKeys({"b", "e", "f", "o", "r", "e"});
  EXPECT_EQ(Preedit(typed), "べふぉれ") << "still kana while typing";
  EXPECT_TRUE(typed.indicator().english());
  EXPECT_EQ(Preedit(SendKeys({"Henkan"})), "before");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "before");

  // before/after: the / after the word is a /, and both words stay letters.
  SendKeys({"b", "e", "f", "o", "r", "e", "/", "a", "f", "t", "e", "r"});
  EXPECT_EQ(Preedit(SendKeys({"Henkan"})), "before/after");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "before/after");

  // Japanese stays Japanese.
  const commands::Output made = SendKeys({"m", "a", "d", "e"});
  EXPECT_FALSE(made.indicator().english());
  EXPECT_NE(Preedit(SendKeys({"Henkan"})), "made");
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // The Japanese is one key away: F6 writes the keys in hiragana again.
  SendKeys({"b", "e", "f", "o", "r", "e", "Henkan"});
  EXPECT_EQ(Preedit(SendKeys({"F6"})), "べふぉれ");
  SendKeys({"Escape"});
  SendKeys({"Escape"});
  // Space then goes through the letters' cases, as for any word in letters.
  SendKeys({"b", "e", "f", "o", "r", "e", "Henkan"});
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "BEFORE");
}

TEST_F(FloatypeZenzTest, WithoutMixedEnglishAWordTypedIntoKanaIsKana) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  SendKeys({"b", "e", "f", "o", "r", "e"});
  EXPECT_NE(Preedit(SendKeys({"Space"})), "before");
  SendKeys({"Escape"});
  SendKeys({"Escape"});
  // And the / after it is the middle dot.
  EXPECT_EQ(Preedit(SendKeys({"b", "e", "f", "o", "r", "e", "/"})),
            "べふぉれ・");
}

TEST_F(FloatypeZenzTest, MixedEnglishUndoesAKanaWordWhenTypingGoesOn) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  // A pause after べふぉれ shows before; a letter typed next goes on in kana.
  ASSERT_EQ(Preedit(PollWhileAsked(
                CallBack(SendKeys({"b", "e", "f", "o", "r", "e"})))),
            "before");
  EXPECT_EQ(Preedit(SendKeys({"n", "i"})), "べふぉれに");
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // A / typed next keeps the word and is a /.
  ASSERT_EQ(Preedit(PollWhileAsked(
                CallBack(SendKeys({"b", "e", "f", "o", "r", "e"})))),
            "before");
  EXPECT_EQ(Preedit(SendKeys({"/"})), "before/");
}

// The test data reads それ, は, と, おなじ, です, を, ひらく, あたらしい and
// みる, but nothing like ふぃぇ, べふぉれ or えゔぇん.
TEST_F(FloatypeZenzTest, MixedEnglishKeepsAKanaWordInsideJapanese) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const auto type = [&](absl::string_view keys) {
    commands::Output output;
    for (const char key : keys) {
      output = SendKeys({std::string(1, key)});
    }
    return output;
  };

  // filewohiraku: the kana stay while typing, and Space keeps file.
  EXPECT_EQ(Preedit(type("filewohiraku")), "ふぃぇをひらく");
  EXPECT_THAT(Preedit(SendKeys({"Space"})), ::testing::StartsWith("fileを"));
  EXPECT_THAT(SendKeys({"Enter"}).result().value(),
              ::testing::StartsWith("fileを"));

  // The word may come anywhere: それはbeforeとおなじです.
  type("sorehabeforetoonajidesu");
  EXPECT_THAT(Preedit(SendKeys({"Space"})),
              ::testing::AllOf(::testing::StartsWith("それはbeforeと"),
                               ::testing::Not(::testing::HasSubstr("ふぉ"))));
  SendKeys({"Enter"});

  // A word may end in the kana after it: eventwomiru is えゔぇんとぉみる.
  EXPECT_EQ(Preedit(type("eventwomiru")), "えゔぇんとぉみる");
  EXPECT_THAT(Preedit(SendKeys({"Space"})), ::testing::StartsWith("eventを"));
  SendKeys({"Enter"});

  // What reads as Japanese stays Japanese: して is no site.
  type("atarasiisitewomiru");
  EXPECT_THAT(Preedit(SendKeys({"Space"})),
              ::testing::Not(::testing::HasSubstr("site")));
  SendKeys({"Enter"});
}

TEST_F(FloatypeZenzTest, MixedEnglishSpaceAfterAnEnglishWordTypesASpace) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const auto type = [&](absl::string_view keys) {
    commands::Output output;
    for (const char key : keys) {
      output = SendKeys({std::string(1, key)});
    }
    return output;
  };

  // English word by word: each Space commits the word and a half-width space.
  type("this");
  commands::Output spaced = SendKeys({"Space"});
  EXPECT_EQ(spaced.result().value(), "this ");
  EXPECT_EQ(Preedit(spaced), "");
  EXPECT_TRUE(spaced.consumed());
  // A word typed wholly into kana that does not read as Japanese (おぺん).
  type("open");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "open ");
  // The marks of English prose after the word are typed as they are.
  type("hello,");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "hello, ");

  // After Japanese too: the Japanese converts, and commits with the word.
  type("korehapull");
  spaced = PollWhileAsked(SendKeys({"Space"}));
  EXPECT_EQ(spaced.result().value(), "これはpull ");
  EXPECT_EQ(Preedit(spaced), "");

  // Japanese still converts: no kana and a space.
  type("kyouha");
  spaced = SendKeys({"Space"});
  EXPECT_FALSE(spaced.has_result());
  EXPECT_EQ(Preedit(spaced), "今日は");
  SendKeys({"Enter"});
  // A word that reads as Japanese is Japanese: まで, not made.
  type("made");
  spaced = SendKeys({"Space"});
  EXPECT_FALSE(spaced.has_result());
  EXPECT_THAT(Preedit(spaced), ::testing::Not(::testing::HasSubstr("made")));
  SendKeys({"Enter"});
  // After Japanese, a mark at the end closes the Japanese sentence.
  type("korehatest.");
  spaced = SendKeys({"Space"});
  EXPECT_FALSE(spaced.has_result());
  SendKeys({"Enter"});
}

TEST_F(FloatypeZenzTest, MixedEnglishGoesOnInEnglishAfterAWordAndASpace) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const auto type = [&](absl::string_view keys) {
    commands::Output output;
    for (const char key : keys) {
      output = SendKeys({std::string(1, key)});
    }
    return output;
  };

  // how are you: あれ and よう are Japanese, but not after "how ".
  type("how");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "how ");
  type("are");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "are ");
  // Space commits the word as typed; Enter commits what shows.
  type("you.");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "you. ");
  // Japanese after it converts, and ends the run.
  type("kyouha");
  commands::Output spaced = SendKeys({"Space"});
  EXPECT_FALSE(spaced.has_result());
  EXPECT_EQ(Preedit(spaced), "今日は");
  SendKeys({"Enter"});
  type("are");
  spaced = SendKeys({"Space"});
  EXPECT_FALSE(spaced.has_result()) << "あれ again";
  SendKeys({"Enter"});

  // A key that leaves nothing composed ends the run too.
  type("how");
  SendKeys({"Space"});
  SendKeys({"Left"});
  type("are");
  spaced = SendKeys({"Space"});
  EXPECT_FALSE(spaced.has_result());
  SendKeys({"Enter"});

  // So does a caret the app reports elsewhere (a click), and not one after
  // the word and its space.
  const auto type_at = [&](absl::string_view before, absl::string_view keys) {
    for (const char key : keys) {
      commands::Input input;
      input.set_type(commands::Input::SEND_KEY);
      EXPECT_TRUE(
          KeyParser::ParseKey(std::string(1, key), input.mutable_key()));
      input.mutable_context()->set_preceding_text(before);
      Eval(std::move(input));
    }
  };
  type("how");
  SendKeys({"Space"});
  type_at("今日は", "are");
  spaced = SendKeys({"Space"});
  EXPECT_FALSE(spaced.has_result());
  SendKeys({"Enter"});
  type("how");
  SendKeys({"Space"});
  type_at("how ", "are");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "are ");
  SendKeys({"Left"});

  // Short words with a letter no kana takes, and commands of letters only.
  type("of");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "of ");
  type("course");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "course ");
  SendKeys({"Left"});
  type("ls");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "ls ");
  type("-la");
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "-la ");
}

TEST_F(FloatypeZenzTest, MixedEnglishKeepsWordsRunTogetherAndJoinedNames) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  // Its Henkan key converts; Space after an English word types a space.
  config.set_session_keymap(config::Config::MSIME);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const auto convert = [&](absl::string_view keys) {
    for (const char key : keys) {
      SendKeys({std::string(1, key)});
    }
    const std::string shown = Preedit(SendKeys({"Henkan"}));
    SendKeys({"Enter"});
    return shown;
  };
  EXPECT_EQ(convert("timeismoney"), "timeismoney");
  EXPECT_EQ(convert("helloworld"), "helloworld");
  EXPECT_EQ(convert("node_modules"), "node_modules");
  EXPECT_EQ(convert("main.cc"), "main.cc");
  EXPECT_EQ(convert("taro@example.com"), "taro@example.com");
  // Japanese beside a word stays Japanese: まで is a word of the test data.
  EXPECT_THAT(convert("pushmade"),
              ::testing::AllOf(::testing::StartsWith("push"),
                               ::testing::Not(::testing::HasSubstr("made"))));
  EXPECT_THAT(convert("made.sore"),
              ::testing::Not(::testing::HasSubstr("made")));
}

TEST_F(FloatypeZenzTest, MixedEnglishKeepsAWordBeforeACapital) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  // camelCase: Shift+C turns the mode to letters, where nothing converts,
  // but camel was typed in kana (かめl).
  commands::Output output;
  for (const char key : std::string_view("camelCase")) {
    output = SendKeys({std::string(1, key)});
  }
  EXPECT_EQ(Preedit(PollWhileAsked(CallBack(output))), "camelCase");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "camelCase");
  // Letters after Japanese are left to the user, as before.
  for (const char key : std::string_view("kikaiOK")) {
    output = SendKeys({std::string(1, key)});
  }
  EXPECT_FALSE(output.has_callback());
  SendKeys({"Escape"});
  SendKeys({"Escape"});
}

TEST_F(FloatypeZenzTest, MixedEnglishUndoTakesBackTheSpaceToo) {
  floatype::EnableSessionFeatures(true);
  // A client that lets the converter delete what it committed.
  commands::Input create;
  create.set_type(commands::Input::CREATE_SESSION);
  create.mutable_capability()->set_text_deletion(
      commands::Capability::DELETE_PRECEDING_TEXT);
  id_ = Eval(std::move(create)).id();
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  for (const char key : std::string_view("korehapull")) {
    SendKeys({std::string(1, key)});
  }
  EXPECT_EQ(PollWhileAsked(SendKeys({"Space"})).result().value(),
            "これはpull ");
  // The key asks the client to send the command.
  const commands::Output undone = CallBack(SendKeys({"Ctrl Backspace"}));
  EXPECT_EQ(undone.deletion_range().length(), 8);
  EXPECT_EQ(undone.deletion_range().offset(), -8);
  EXPECT_EQ(Preedit(undone), "これはpull");
}

TEST_F(FloatypeZenzTest, MixedEnglishSpaceAfterALiveConversionTypesASpace) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  commands::Output output;
  for (const char key : std::string_view("korehapull")) {
    output = SendKeys({std::string(1, key)});
  }
  // The pause shows これはpull; Space then commits it and a space.
  EXPECT_EQ(Preedit(PollWhileAsked(CallBack(output))), "これはpull");
  const commands::Output spaced = PollWhileAsked(SendKeys({"Space"}));
  EXPECT_EQ(spaced.result().value(), "これはpull ");
  EXPECT_EQ(Preedit(spaced), "");
}

TEST_F(FloatypeZenzTest, MixedEnglishKeepsSymbolsBetweenWordsAsTyped) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  // Its Henkan key converts; Space after an English word types a space.
  config.set_session_keymap(config::Config::MSIME);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const auto convert = [&](absl::string_view keys) {
    for (const char key : keys) {
      SendKeys({std::string(1, key)});
    }
    const std::string shown = Preedit(SendKeys({"Henkan"}));
    SendKeys({"Enter"});
    return shown;
  };
  EXPECT_EQ(convert("config.toml"), "config.toml");
  EXPECT_EQ(convert("foo_bar"), "foo_bar");
  EXPECT_EQ(convert("--help"), "--help");
  EXPECT_EQ(convert("localhost:3000"), "localhost:3000");
  // Inside Japanese too.
  EXPECT_EQ(convert("kyouhaconfig.tomlwohiraku"), "今日はconfig.tomlを開く");
  // Brackets stay Japanese, and so does a mark between kana.
  EXPECT_EQ(convert("[file]"), "「file」");
  EXPECT_THAT(convert("kyouha.asita"), ::testing::StartsWith("今日は。"));
  // Space commits a name as it shows.
  for (const char key : std::string_view("config.toml")) {
    SendKeys({std::string(1, key)});
  }
  EXPECT_EQ(SendKeys({"Space"}).result().value(), "config.toml ");
}

TEST_F(FloatypeZenzTest, WithoutMixedEnglishAKanaWordInsideJapaneseIsKana) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  for (const char key : std::string("filewohiraku")) {
    SendKeys({std::string(1, key)});
  }
  EXPECT_THAT(Preedit(SendKeys({"Space"})),
              ::testing::Not(::testing::HasSubstr("file")));
}

TEST_F(FloatypeZenzTest, MixedEnglishKeepsAKanaWordInsideJapaneseAtAPause) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  const auto pause_after = [&](absl::string_view keys) {
    commands::Output output;
    for (const char key : keys) {
      output = SendKeys({std::string(1, key)});
    }
    return Preedit(PollWhileAsked(CallBack(output)));
  };

  // A word still being typed is left alone: べ may go on to べんり.
  EXPECT_THAT(pause_after("sorehabe"),
              ::testing::Not(::testing::HasSubstr("sore")));
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // file with を after it is kept for good: typing on does not bring ふぃぇ
  // back, and a second word is found beside the kept one.
  EXPECT_THAT(pause_after("filewo"), ::testing::StartsWith("fileを"));
  EXPECT_THAT(pause_after("deletesuru"),
              ::testing::AllOf(::testing::StartsWith("fileをdelete"),
                               ::testing::Not(::testing::HasSubstr("ぇ"))));
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // A word that ends the composition can still go on in kana.
  EXPECT_THAT(pause_after("sorehabefore"),
              ::testing::StartsWith("それはbefore"));
  EXPECT_THAT(Preedit(SendKeys({"n", "i"})), ::testing::EndsWith("べふぉれに"));
}

// の + テーマ starts with のて, the kana of the preset word note.
TEST_F(FloatypeZenzTest, MixedEnglishGoesOnInKanaWithTheLongVowelKey) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(SendKeys({"n", "o", "t", "e"})))),
            "note");
  EXPECT_EQ(Preedit(SendKeys({"-"})), "のてー");
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // After a word with letters left over, ー does not bring the kana back.
  ASSERT_EQ(Preedit(PollWhileAsked(CallBack(SendKeys({"t", "e", "s", "t"})))),
            "test");
  EXPECT_THAT(Preedit(SendKeys({"-"})), ::testing::StartsWith("test"));
}

// Sets the config for the tests of / after a letter or a digit.
// The MS-IME key map has a key that converts an English word (Henkan):
// Space after one types a space.
#define SET_SLASH_CONFIG(mixed_english, slash)                       \
  floatype::EnableSessionFeatures(true);                             \
  {                                                                  \
    config::Config config = config::ConfigHandler::DefaultConfig(); \
    config.set_mixed_english(mixed_english);                         \
    config.set_slash_after_ascii(slash);                             \
    config.set_use_live_conversion(false);                           \
    config.set_session_keymap(config::Config::MSIME);                \
    commands::Input input;                                           \
    input.set_type(commands::Input::SET_CONFIG);                     \
    *input.mutable_config() = config;                                \
    Eval(std::move(input));                                          \
  }

TEST_F(FloatypeZenzTest, SlashAfterADigitIsASlash) {
  SET_SLASH_CONFIG(false, true);
  EXPECT_EQ(Preedit(SendKeys({"2", "0", "2", "6", "/", "1", "1", "/", "2", "0"})),
            "2026/11/20");
  EXPECT_EQ(Preedit(SendKeys({"Space"})), "2026/11/20");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "2026/11/20");

  // Committed from the composition as it is.
  SendKeys({"1", "/", "2"});
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "1/2");
}

TEST_F(FloatypeZenzTest, SlashAfterKanaIsStillTheMiddleDot) {
  SET_SLASH_CONFIG(true, true);
  EXPECT_EQ(Preedit(SendKeys({"a", "/"})), "あ・");
  SendKeys({"Escape"});
  EXPECT_EQ(Preedit(SendKeys({"/"})), "・") << "nothing before it";
  SendKeys({"Escape"});
  EXPECT_EQ(Preedit(SendKeys({"z", "/"})), "・") << "the rule z/ stays";
  SendKeys({"Escape"});
  EXPECT_EQ(Preedit(SendKeys({"a", "z", "/"})), "あ・");
}

TEST_F(FloatypeZenzTest, SlashAfterAnEnglishWordIsASlash) {
  SET_SLASH_CONFIG(true, true);
  SendKeys({"t", "e", "s", "t", "/", "s", "p", "e", "c"});
  EXPECT_EQ(Preedit(SendKeys({"Henkan"})), "test/spec");
  EXPECT_EQ(SendKeys({"Enter"}).result().value(), "test/spec");

  // Loose letters that are no word in the list (src), two or more.
  const commands::Output typed = SendKeys({"s", "r", "c", "/"});
  EXPECT_TRUE(Preedit(typed).ends_with("/")) << Preedit(typed);
  SendKeys({"Escape"});
}

TEST_F(FloatypeZenzTest, SlashRightAfterCommittingAnEnglishWordIsASlash) {
  SET_SLASH_CONFIG(true, true);
  // No app tells what precedes the caret here, as in a terminal.
  SendKeys({"k", "e", "y", "b", "o", "a", "r", "d", "Henkan"});
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "keyboard");
  EXPECT_EQ(Preedit(SendKeys({"/"})), "/");
  SendKeys({"Escape"});

  // Typing / on the conversion commits it and goes on.
  SendKeys({"k", "e", "y", "b", "o", "a", "r", "d", "Henkan"});
  const commands::Output slash = SendKeys({"/"});
  EXPECT_EQ(slash.result().value(), "keyboard");
  EXPECT_EQ(Preedit(slash), "/");
  SendKeys({"Escape"});

  // After Japanese, and after a key since the commit, it is the dot.
  SendKeys({"k", "i", "k", "a", "i", "Space"});
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "機会");
  EXPECT_EQ(Preedit(SendKeys({"/"})), "・");
  SendKeys({"Escape"});
  SendKeys({"k", "e", "y", "b", "o", "a", "r", "d", "Henkan"});
  ASSERT_EQ(SendKeys({"Enter"}).result().value(), "keyboard");
  SendKeys({"a", "Backspace"});
  EXPECT_EQ(Preedit(SendKeys({"/"})), "・");
}

TEST_F(FloatypeZenzTest, SlashAfterALetterTheAppTellsIsASlash) {
  SET_SLASH_CONFIG(false, true);
  for (const auto& [before, expected] :
       {std::pair{"see README", "/"}, std::pair{"日本語", "・"}}) {
    commands::Input input;
    input.set_type(commands::Input::SEND_KEY);
    ASSERT_TRUE(KeyParser::ParseKey("/", input.mutable_key()));
    input.mutable_context()->set_preceding_text(before);
    EXPECT_EQ(Preedit(Eval(std::move(input))), expected) << before;
    SendKeys({"Escape"});
  }
}

TEST_F(FloatypeZenzTest, WithKanaInputTheSlashKeyTypesItsKana) {
  SET_SLASH_CONFIG(false, true);
  SendKeys({"2", "0", "2", "6"});
  commands::Input input;
  input.set_type(commands::Input::SEND_KEY);
  input.mutable_key()->set_key_code('/');
  input.mutable_key()->set_key_string("め");
  EXPECT_EQ(Preedit(Eval(std::move(input))), "2026め");
}

TEST_F(FloatypeZenzTest, SlashStaysTheMiddleDotWithTheSettingOff) {
  SET_SLASH_CONFIG(true, false);
  EXPECT_EQ(Preedit(SendKeys({"2", "0", "2", "6", "/"})), "2026・");
  SendKeys({"Escape"});
  SendKeys({"t", "e", "s", "t", "/"});
  EXPECT_FALSE(Preedit(SendKeys({"Space"})).ends_with("/"));
}

#undef SET_SLASH_CONFIG

TEST_F(FloatypeZenzTest, MixedEnglishLeavesATrailingNJapanese) {
  floatype::EnableSessionFeatures(true);
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_mixed_english(true);
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  // Words of the test data's dictionary: てん, せん, けん, ぺん (it has no
  // ぱん, so pan would be the English word there).
  for (const absl::string_view word : {"ten", "sen", "ken", "pen"}) {
    SendKeys({word.substr(0, 1), word.substr(1, 1), word.substr(2, 1)});
    EXPECT_NE(Preedit(SendKeys({"Space"})), word);
    SendKeys({"Escape"});
    SendKeys({"Escape"});
  }
}

TEST_F(FloatypeZenzTest, AKeyCanRunSeveralCommands) {
  // MS-IME's table with two keys bound to sequences.
  std::unique_ptr<std::istream> stream = ConfigFileStream::LegacyOpen(
      keymap::KeyMapManager::GetKeyMapFileName(config::Config::MSIME));
  ASSERT_NE(stream, nullptr);
  std::string table((std::istreambuf_iterator<char>(*stream)),
                    std::istreambuf_iterator<char>());
  table +=
      "Composition\tCtrl Enter\tCommit|IMEOff\n"
      "Composition\tCtrl Space\tConvert|ConvertNext\n"
      "Composition\tCtrl Tab\tCommit|ConvertNext\n";
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_session_keymap(config::Config::CUSTOM);
  config.set_custom_keymap_table(table);
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));

  const commands::Output committed =
      SendKeys({"k", "i", "k", "a", "i", "Ctrl Enter"});
  EXPECT_EQ(committed.result().value(), "きかい");
  EXPECT_EQ(committed.mode(), commands::DIRECT) << "and the IME is off";

  SendKeys({"Hankaku/Zenkaku"});
  const commands::Output first = SendKeys({"k", "i", "k", "a", "i", "Space"});
  SendKeys({"Escape"});
  const commands::Output next = SendKeys({"Ctrl Space"});
  EXPECT_NE(Preedit(next), Preedit(first)) << "converted, then the next one";
  EXPECT_TRUE(next.has_candidate_window());
  SendKeys({"Escape"});
  SendKeys({"Escape"});

  // ConvertNext has nothing to do after Commit; the commit stands.
  const commands::Output partly = SendKeys({"k", "a", "s", "a", "Ctrl Tab"});
  EXPECT_EQ(partly.result().value(), "かさ");
  EXPECT_TRUE(partly.consumed());
}

TEST_F(FloatypeZenzTest, AmbiguousRomajiCanShowItsKana) {
  EXPECT_EQ(Preedit(SendKeys({"k", "a", "n"})), "かn") << "off by default";
  SendKeys({"Escape"});
  config::Config config = config::ConfigHandler::DefaultConfig();
  config.set_display_ambiguous_romaji(true);
  config.set_use_live_conversion(false);
  commands::Input input;
  input.set_type(commands::Input::SET_CONFIG);
  *input.mutable_config() = config;
  Eval(std::move(input));
  EXPECT_EQ(Preedit(SendKeys({"k", "a", "n"})), "かん");
  EXPECT_EQ(Preedit(SendKeys({"a"})), "かな");
  EXPECT_EQ(Preedit(SendKeys({"n", "k", "a"})), "かなんか");
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
