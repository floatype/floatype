// replay_main: replays Space conversions through Mozc's session handler with
// the real engine and the real scorer, calling back the way the macOS client
// does, and reports what the user would see and when.
//
// Input (stdin): "<left context>\t<hiragana reading>" per line.
// With --live, see the flag for the output.
// Output (stdout), one line each:
//   <mozc>\t<final>\t<space ms>\t<final ms>\t<polls>
// <mozc> is shown right after Space and <final> once the session stops asking
// to be called back. <space ms> is the Space key's own latency, <final ms> the
// time from pressing Space to <final>. With --not_this N, each of N presses of
// the zenz key that follow adds <shown>\t<ms>.
//
// Run from src/ after building the scorer (floatype/scorer/CMakeLists.txt).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "base/file/temp_dir.h"
#include "base/init_mozc.h"
#include "base/strings/japanese.h"
#include "base/strings/unicode.h"
#include "base/system_util.h"
#include "base/util.h"
#include "config/config_handler.h"
#include "engine/engine.h"
#include "engine/eval_engine_factory.h"
#include "floatype/zenz/scorer_client.h"
#include "floatype/zenz/zenz_corrector.h"
#include "protocol/commands.pb.h"
#include "session/session_handler.h"

ABSL_FLAG(std::string, data_file, "bazel-bin/data_manager/oss/mozc.data",
          "Mozc's data file");
ABSL_FLAG(std::string, data_type, "oss", "type of the data file");
ABSL_FLAG(std::string, scorer, "floatype/scorer/build/floatype_scorer",
          "the scorer process");
ABSL_FLAG(std::string, model,
          "floatype/scorer/build/zenz-v3.2-small-Q5_K_M.gguf",
          "the zenz model");
ABSL_FLAG(bool, zenz, true, "correct conversions with zenz");
ABSL_FLAG(int, not_this, 0,
          "press the zenz key (Ctrl+z) this many times after each Space "
          "conversion settles");
ABSL_FLAG(bool, live, false,
          "type each reading key by key with live conversion on, pausing "
          "after 、 and 。 and at the end, and commit it with Enter; prints "
          "<committed>\t<reading committed>\t<polls>\t<changes>, or SKIP for "
          "a reading with characters that have no key here. <changes> counts "
          "how often the text on screen changed while the user paused");
ABSL_FLAG(int, pause_every, 0,
          "live mode: also pause after every this many kana (0: only at 、, 。 "
          "and the end)");
ABSL_FLAG(bool, pause_every_key, false,
          "live mode: pause after every key, inside syllables too");
ABSL_FLAG(int, wait_for_zenz, 0,
          "live mode: live_conversion_waits_for_zenz_millisec");
ABSL_FLAG(bool, suggest, false,
          "type each reading key by key and print the suggestions shown "
          "after the last key, separated by |; with --live, after the pause "
          "that follows it");
ABSL_FLAG(bool, mixed_english, false, "live mode: turn mixed English input on");
ABSL_FLAG(bool, keys, false,
          "live mode: each reading is the keys to type, so a space is the "
          "Space key and an upper-case letter is typed as it is; the text "
          "committed is everything the keys commit, not only the last Enter");
ABSL_FLAG(bool, suggest_added_words, false,
          "suggest the words of the added dictionaries too");
ABSL_FLAG(bool, display_ambiguous, false,
          "live mode: show ambiguous romaji as kana");

namespace mozc {
namespace {

std::string Preedit(const commands::Output& output) {
  std::string text;
  for (const commands::Preedit::Segment& segment : output.preedit().segment()) {
    text += segment.value();
  }
  return text;
}

double MillisecondsSince(absl::Time start) {
  return absl::ToDoubleMilliseconds(absl::Now() - start);
}

class Replayer {
 public:
  explicit Replayer(std::unique_ptr<Engine> engine)
      : handler_(std::move(engine)) {
    commands::Input input;
    input.set_type(commands::Input::CREATE_SESSION);
    id_ = Eval(std::move(input)).id();
    CHECK_NE(id_, 0);
  }

  // Converts `reading` with Space after `left_context` and calls back until the
  // session stops asking, then reverts the conversion so nothing is learned.
  std::string Replay(absl::string_view left_context,
                     absl::string_view reading) {
    // The first key carries the context, which the session keeps as the
    // client's; then the composition becomes exactly the reading.
    commands::Input first;
    first.set_type(commands::Input::SEND_KEY);
    first.mutable_key()->set_key_code('a');
    first.mutable_context()->set_preceding_text(left_context);
    Eval(std::move(first));
    commands::Input compose;
    compose.set_type(commands::Input::SEND_COMMAND);
    compose.mutable_command()->set_type(
        commands::SessionCommand::UPDATE_COMPOSITION);
    commands::SessionCommand::CompositionEvent* event =
        compose.mutable_command()->add_composition_events();
    event->set_composition_string(reading);
    event->set_probability(1.0);
    Eval(std::move(compose));

    commands::Input space;
    space.set_type(commands::Input::SEND_KEY);
    space.mutable_key()->set_special_key(commands::KeyEvent::SPACE);
    const absl::Time start = absl::Now();
    commands::Output output = Eval(std::move(space));
    const double space_ms = MillisecondsSince(start);
    const std::string mozc = Preedit(output);
    int polls = 0;
    const auto call_back = [&] {
      while (output.has_callback()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(output.callback().delay_millisec()));
        commands::Input poll;
        poll.set_type(commands::Input::SEND_COMMAND);
        *poll.mutable_command() = output.callback().session_command();
        output = Eval(std::move(poll));
        ++polls;
      }
    };
    call_back();
    const double final_ms = MillisecondsSince(start);
    const std::string final = Preedit(output);
    const int final_polls = polls;
    std::string not_this;
    for (int i = 0; i < absl::GetFlag(FLAGS_not_this); ++i) {
      commands::Input key;
      key.set_type(commands::Input::SEND_KEY);
      key.mutable_key()->set_key_code('z');
      key.mutable_key()->add_modifier_keys(commands::KeyEvent::CTRL);
      const absl::Time pressed = absl::Now();
      output = Eval(std::move(key));
      CHECK(output.consumed()) << "Ctrl+z is not the zenz key";
      call_back();
      absl::StrAppend(&not_this, "\t", Preedit(output), "\t",
                      MillisecondsSince(pressed));
    }

    commands::Input revert;
    revert.set_type(commands::Input::SEND_COMMAND);
    revert.mutable_command()->set_type(commands::SessionCommand::REVERT);
    Eval(std::move(revert));

    return mozc + '\t' + final + '\t' + std::to_string(space_ms) + '\t' +
           std::to_string(final_ms) + '\t' + std::to_string(final_polls) +
           not_this;
  }

  // Types `reading` key by key as romaji and lists what the suggestion window
  // shows after the last key; Escape then drops the composition unlearned.
  std::string Suggest(absl::string_view reading) {
    const std::optional<std::vector<std::pair<char, bool>>> keys =
        Keys(reading);
    if (!keys.has_value()) {
      return "SKIP";
    }
    commands::Output output;
    for (const auto& [key_code, pause] : *keys) {
      commands::Input input;
      input.set_type(commands::Input::SEND_KEY);
      input.mutable_key()->set_key_code(key_code);
      output = Eval(std::move(input));
    }
    if (absl::GetFlag(FLAGS_live)) {
      int changes = 0;
      RunCallbacks(output, changes);
    }
    std::vector<std::string> values;
    for (const commands::CandidateWindow::Candidate& candidate :
         output.candidate_window().candidate()) {
      values.push_back(candidate.value());
    }
    for (int i = 0; i < 2; ++i) {
      commands::Input escape;
      escape.set_type(commands::Input::SEND_KEY);
      escape.mutable_key()->set_special_key(commands::KeyEvent::ESCAPE);
      Eval(std::move(escape));
    }
    return absl::StrJoin(values, "|");
  }

  // Types `reading` key by key as romaji after `left_context`, lets the timer
  // callbacks run where the user pauses, and commits with Enter.
  std::string ReplayLive(absl::string_view left_context,
                         absl::string_view reading) {
    const std::optional<std::vector<std::pair<char, bool>>> keys =
        Keys(reading);
    if (!keys.has_value()) {
      return "SKIP";
    }
    int polls = 0;
    int changes = 0;
    // What the keys before Enter commit (with --keys, Space may commit).
    std::string before;
    for (const auto& [key_code, pause] : *keys) {
      commands::Input input;
      input.set_type(commands::Input::SEND_KEY);
      if (key_code == ' ') {
        input.mutable_key()->set_special_key(commands::KeyEvent::SPACE);
      } else {
        input.mutable_key()->set_key_code(key_code);
      }
      // What was committed on the way comes before the caret too.
      input.mutable_context()->set_preceding_text(
          absl::StrCat(left_context, before));
      commands::Output output = Eval(std::move(input));
      before += output.result().value();
      if (pause && output.has_callback()) {
        polls += RunCallbacks(output, changes);
        // A callback may commit too (a direct commit that waited for zenz).
        before += output.result().value();
      }
    }
    commands::Input enter;
    enter.set_type(commands::Input::SEND_KEY);
    enter.mutable_key()->set_special_key(commands::KeyEvent::ENTER);
    const commands::Output committed = Eval(std::move(enter));
    // Takes back what the commit taught, so items stay independent.
    commands::Input undo;
    undo.set_type(commands::Input::SEND_KEY);
    undo.mutable_key()->set_special_key(commands::KeyEvent::BACKSPACE);
    Eval(std::move(undo));
    // Nor does the commit stand in for the next item's context.
    commands::Input reset;
    reset.set_type(commands::Input::SEND_COMMAND);
    reset.mutable_command()->set_type(commands::SessionCommand::RESET_CONTEXT);
    Eval(std::move(reset));
    return before + committed.result().value() + '\t' +
           committed.result().key() + '\t' + std::to_string(polls) + '\t' +
           std::to_string(changes);
  }

  void SetConfig(int wait_for_zenz) {
    commands::Input input;
    input.set_type(commands::Input::SET_CONFIG);
    *input.mutable_config() = config::ConfigHandler::DefaultConfig();
    input.mutable_config()->set_live_conversion_waits_for_zenz_millisec(
        wait_for_zenz);
    input.mutable_config()->set_mixed_english(
        absl::GetFlag(FLAGS_mixed_english));
    input.mutable_config()->set_display_ambiguous_romaji(
        absl::GetFlag(FLAGS_display_ambiguous));
    input.mutable_config()->set_suggest_added_words(
        absl::GetFlag(FLAGS_suggest_added_words));
    Eval(std::move(input));
  }

 private:
  // The romaji keys for `reading`, each marked when the user pauses after it.
  static std::optional<std::vector<std::pair<char, bool>>> Keys(
      absl::string_view reading) {
    std::vector<std::pair<char, bool>> keys;
    std::string kana;
    int typed = 0;
    auto flush = [&]() -> bool {
      // ん before a vowel or y needs nn; HiraganaToRomanji writes n.
      const int pause_every = absl::GetFlag(FLAGS_pause_every);
      for (const char32_t c : Utf8AsChars32(kana)) {
        std::string romaji =
            japanese::HiraganaToRomanji(Util::CodepointToUtf8(c));
        if (c == U'ん') romaji = "nn";
        for (const char key : romaji) {
          if (!absl::ascii_islower(static_cast<unsigned char>(key))) {
            return false;
          }
          keys.emplace_back(key, false);
        }
        // Small kana join the kana before them, so the count skips them.
        if (pause_every > 0 && !romaji.starts_with('x') &&
            ++typed % pause_every == 0) {
          keys.back().second = true;
        }
      }
      kana.clear();
      return true;
    };
    for (const char32_t c : Utf8AsChars32(reading)) {
      // Lower-case letters, digits and ASCII symbols are typed as they are:
      // an English word, 2026, before/after.
      if (c < 0x7F &&
          (absl::GetFlag(FLAGS_keys) ? c >= U' '
                                     : c > U' ' && !(c >= U'A' && c <= U'Z'))) {
        if (!flush()) return std::nullopt;
        keys.emplace_back(static_cast<char>(c), false);
        continue;
      }
      // 、 and 。 end a phrase, where the user pauses; ー is its own key.
      const char key = c == U'、'   ? ','
                       : c == U'。' ? '.'
                       : c == U'ー' ? '-'
                                    : 0;
      if (key == 0) {
        Util::CodepointToUtf8Append(c, &kana);
        continue;
      }
      if (!flush()) return std::nullopt;
      keys.emplace_back(key, key != '-');
    }
    if (!flush() || keys.empty()) return std::nullopt;
    keys.back().second = true;
    if (absl::GetFlag(FLAGS_pause_every_key)) {
      for (auto& key : keys) key.second = true;
    }
    return keys;
  }

  // Calls back the way the client's timer does until the session stops
  // asking, leaving the last output in `output`. Only polls wait: they wait
  // for the scorer.
  int RunCallbacks(commands::Output& output, int& changes) {
    int polls = 0;
    std::string shown = Preedit(output);
    while (output.has_callback()) {
      if (output.callback().session_command().type() ==
          commands::SessionCommand::POLL_ZENZ) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(output.callback().delay_millisec()));
        ++polls;
      }
      commands::Input input;
      input.set_type(commands::Input::SEND_COMMAND);
      *input.mutable_command() = output.callback().session_command();
      output = Eval(std::move(input));
      if (std::string now = Preedit(output); now != shown) {
        ++changes;
        shown = std::move(now);
      }
    }
    return polls;
  }

  commands::Output Eval(commands::Input input) {
    commands::Command command;
    *command.mutable_input() = std::move(input);
    command.mutable_input()->set_id(id_);
    CHECK(handler_.EvalCommand(&command));
    return command.output();
  }

  SessionHandler handler_;
  uint64_t id_ = 0;
};

}  // namespace
}  // namespace mozc

int main(int argc, char** argv) {
  mozc::InitMozc(argv[0], &argc, &argv);
  // Keep the user's own profile out of it.
  absl::StatusOr<mozc::TempDirectory> profile =
      mozc::TempDirectory::Default().CreateTempDirectory();
  CHECK_OK(profile);
  mozc::SystemUtil::SetUserProfileDirectory(profile->path());

  std::unique_ptr<mozc::floatype::ScorerClient> client;
  if (absl::GetFlag(FLAGS_zenz)) {
    client = std::make_unique<mozc::floatype::ScorerClient>(
        std::vector<std::string>{absl::GetFlag(FLAGS_scorer), "--model",
                                 absl::GetFlag(FLAGS_model)});
  }
  // Sessions take the scorer when they are created, so set it first.
  mozc::floatype::ZenzCorrector::SetDefaultClientForTesting(client.get());
  // A locked screen turns secure input on; the measurement asks zenz anyway.
  mozc::floatype::ZenzCorrector::IgnoreSecureInputForTesting(true);

  absl::StatusOr<std::unique_ptr<mozc::Engine>> engine = mozc::CreateEvalEngine(
      absl::GetFlag(FLAGS_data_file), absl::GetFlag(FLAGS_data_type));
  CHECK_OK(engine);
  // Live conversion and the indicator run only where turned on, as in the
  // converter process.
  const bool live = absl::GetFlag(FLAGS_live);
  mozc::floatype::EnableSessionFeatures(live);
  mozc::Replayer replayer(*std::move(engine));
  // Without a config, CharacterFormManager falls back to Mozc's own rules,
  // not Floatype's defaults (half-width digits) that the converter reads.
  replayer.SetConfig(absl::GetFlag(FLAGS_wait_for_zenz));

  // The first conversion starts the scorer and loads the model.
  replayer.Replay("", "きょうはいしゃにいく");

  std::string line;
  while (std::getline(std::cin, line)) {
    const size_t tab = line.find('\t');
    const absl::string_view context =
        tab == std::string::npos ? "" : absl::string_view(line).substr(0, tab);
    const absl::string_view reading =
        tab == std::string::npos ? absl::string_view(line)
                                 : absl::string_view(line).substr(tab + 1);
    std::cout << (absl::GetFlag(FLAGS_suggest) ? replayer.Suggest(reading)
                  : live ? replayer.ReplayLive(context, reading)
                         : replayer.Replay(context, reading))
              << std::endl;
  }
  mozc::floatype::ZenzCorrector::SetDefaultClientForTesting(nullptr);
  return 0;
}
