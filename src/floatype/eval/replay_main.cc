// replay_main: replays Space conversions through Mozc's session handler with
// the real engine and the real scorer, calling back the way the macOS client
// does, and reports what the user would see and when.
//
// Input (stdin): "<left context>\t<hiragana reading>" per line.
// Output (stdout), one line each:
//   <mozc>\t<final>\t<space ms>\t<final ms>\t<polls>
// <mozc> is shown right after Space and <final> once the session stops asking
// to be called back. <space ms> is the Space key's own latency, <final ms> the
// time from pressing Space to <final>.
//
// Run from src/ after building the scorer (floatype/scorer/CMakeLists.txt).

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "base/file/temp_dir.h"
#include "base/init_mozc.h"
#include "base/system_util.h"
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
    while (output.has_callback()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(output.callback().delay_millisec()));
      commands::Input poll;
      poll.set_type(commands::Input::SEND_COMMAND);
      *poll.mutable_command() = output.callback().session_command();
      output = Eval(std::move(poll));
      ++polls;
    }
    const double final_ms = MillisecondsSince(start);
    const std::string final = Preedit(output);

    commands::Input revert;
    revert.set_type(commands::Input::SEND_COMMAND);
    revert.mutable_command()->set_type(commands::SessionCommand::REVERT);
    Eval(std::move(revert));

    return mozc + '\t' + final + '\t' + std::to_string(space_ms) + '\t' +
           std::to_string(final_ms) + '\t' + std::to_string(polls);
  }

 private:
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

  absl::StatusOr<std::unique_ptr<mozc::Engine>> engine = mozc::CreateEvalEngine(
      absl::GetFlag(FLAGS_data_file), absl::GetFlag(FLAGS_data_type));
  CHECK_OK(engine);
  mozc::Replayer replayer(*std::move(engine));

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
    std::cout << replayer.Replay(context, reading) << std::endl;
  }
  mozc::floatype::ZenzCorrector::SetDefaultClientForTesting(nullptr);
  return 0;
}
