// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Session class of Mozc server.

#include "session/session.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/port.h"
#include "base/strings/japanese.h"
#include "base/strings/unicode.h"
#include "base/util.h"
#include "composer/composer.h"
#include "composer/key_event_util.h"
#include "composer/table.h"
#include "converter/attribute.h"
#include "engine/engine_converter_interface.h"
#include "engine/engine_interface.h"
#include "floatype/english/english_words.h"
#include "floatype/zenz/zenz_memory.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/ime_context.h"
#include "session/key_event_transformer.h"
#include "session/keymap.h"
#include "transliteration/transliteration.h"

#ifdef __APPLE__
#include <TargetConditionals.h>  // for TARGET_OS_IPHONE
#endif                           // __APPLE__

namespace mozc {
namespace session {
namespace {

// floatype: whether `key` extends an unfinished Roman sequence (ｋｙ in
// きかいｋｙ) whose suggestions would come and go. The first two letters of a
// composition still suggest, so that typing from nothing shows candidates.
// Mozkey holds suggestions the same way.
bool HoldsSuggestions(const commands::KeyEvent& key,
                      const composer::Composer& composer) {
  const transliteration::TransliterationType mode = composer.GetInputMode();
  if (key.modifier_keys_size() != 0 || key.key_code() < 'a' ||
      key.key_code() > 'z' ||
      (mode != transliteration::HIRAGANA &&
       mode != transliteration::FULL_KATAKANA &&
       mode != transliteration::HALF_KATAKANA)) {
    return false;
  }
  size_t length = 0;
  size_t pending = 0;
  for (const char32_t c : Utf8AsChars32(composer.GetStringForPreedit())) {
    ++length;
    const bool roman = (c >= 'a' && c <= 'z') || (c >= U'ａ' && c <= U'ｚ');
    pending = roman ? pending + 1 : 0;
  }
  return pending > 0 && (pending < length || pending > 2);
}

using ::mozc::engine::ConversionPreferences;
using ::mozc::engine::EngineConverterInterface;

// Maximum size of multiple undo stack.
const size_t kMultipleUndoMaxSize = 10;

// Set input mode if the current input mode is not the given mode.
void SwitchInputMode(const transliteration::TransliterationType mode,
                     composer::Composer* composer) {
  if (composer->GetInputMode() != mode) {
    composer->SetInputMode(mode);
  }
  composer->SetNewInput();
}

// Set input mode to the |composer| if the input mode of |composer| is not
// the given |mode|.
void ApplyCompositionMode(const commands::CompositionMode mode,
                          composer::Composer* composer) {
  switch (mode) {
    case commands::HIRAGANA:
      SwitchInputMode(transliteration::HIRAGANA, composer);
      break;
    case commands::FULL_KATAKANA:
      SwitchInputMode(transliteration::FULL_KATAKANA, composer);
      break;
    case commands::HALF_KATAKANA:
      SwitchInputMode(transliteration::HALF_KATAKANA, composer);
      break;
    case commands::FULL_ASCII:
      SwitchInputMode(transliteration::FULL_ASCII, composer);
      break;
    case commands::HALF_ASCII:
      SwitchInputMode(transliteration::HALF_ASCII, composer);
      break;
    default:
      LOG(DFATAL) << "ime on with invalid mode";
  }
}

// Return true if the specified key event consists of any modifier key only.
bool IsPureModifierKeyEvent(const commands::KeyEvent& key) {
  if (key.has_key_code()) {
    return false;
  }
  if (key.has_special_key()) {
    return false;
  }
  if (key.modifier_keys_size() == 0) {
    return false;
  }
  return true;
}

bool IsPureSpaceKey(const commands::KeyEvent& key) {
  if (key.has_key_code()) {
    return false;
  }
  if (key.modifier_keys_size() > 0) {
    return false;
  }
  if (!key.has_special_key()) {
    return false;
  }
  if (key.special_key() != commands::KeyEvent::SPACE) {
    return false;
  }
  return true;
}

// Set session state to the given state and also update related status.
void SetSessionState(const ImeContext::State state, ImeContext* context) {
  const ImeContext::State prev_state = context->state();
  context->set_state(state);
  switch (state) {
    case ImeContext::DIRECT:
    case ImeContext::PRECOMPOSITION:
      context->mutable_composer()->Reset();
      break;
    case ImeContext::CONVERSION:
      context->mutable_composer()->ResetInputMode();
      break;
    case ImeContext::COMPOSITION:
      if (prev_state == ImeContext::PRECOMPOSITION) {
        // Notify the start of composition to the converter so that internal
        // state can be refreshed by the client context (especially by
        // preceding text).
        context->mutable_converter()->OnStartComposition(
            context->client_context());
      }
      break;
    default:
      // Do nothing.
      break;
  }
}

void SetStateToPredompositionAndCancel(ImeContext* context) {
  SetSessionState(ImeContext::PRECOMPOSITION, context);
  // mutable_converter's internal state should be updated by calling Cancel().
  // Internal state contains:
  // - candidate list
  // - result text to commit
  if (!context->mutable_converter()->CheckState(
          EngineConverterInterface::COMPOSITION)) {
    context->mutable_converter()->Cancel();
  }
}

commands::CompositionMode ToCompositionMode(
    mozc::transliteration::TransliterationType type) {
  commands::CompositionMode mode = commands::HIRAGANA;
  switch (type) {
    case transliteration::HIRAGANA:
      mode = commands::HIRAGANA;
      break;
    case transliteration::FULL_KATAKANA:
      mode = commands::FULL_KATAKANA;
      break;
    case transliteration::HALF_KATAKANA:
      mode = commands::HALF_KATAKANA;
      break;
    case transliteration::FULL_ASCII:
      mode = commands::FULL_ASCII;
      break;
    case transliteration::HALF_ASCII:
      mode = commands::HALF_ASCII;
      break;
    default:
      LOG(ERROR) << "Unknown input mode: " << type;
      // use HIRAGANA as a default.
  }
  return mode;
}

ImeContext::State GetEffectiveStateForTestSendKey(const commands::KeyEvent& key,
                                                  ImeContext::State state) {
  if (!key.has_activated()) {
    return state;
  }
  if (state == ImeContext::DIRECT && key.activated()) {
    // Indirect IME On found.
    return ImeContext::PRECOMPOSITION;
  }
  if (state != ImeContext::DIRECT && !key.activated()) {
    // Indirect IME Off found.
    return ImeContext::DIRECT;
  }
  return state;
}

}  // namespace

Session::Session(const EngineInterface& engine)
    : context_(CreateContext(engine)) {}

std::unique_ptr<ImeContext> Session::CreateContext(
    const EngineInterface& engine) const {
  auto context = std::make_unique<ImeContext>(engine.CreateEngineConverter());
  context->set_create_time(Clock::GetAbslTime());

  if constexpr (port::IsWindows()) {
    // On Windows session is started with direct mode.
    // FIXME(toshiyuki): Ditto for Mac after verifying on Mac.
    context->set_state(ImeContext::DIRECT);
  } else {
    context->set_state(ImeContext::PRECOMPOSITION);
  }

  // TODO(team): Remove #if based behavior change for cascading window.
  // Tests for session layer (session_handler_scenario_test, etc) can be
  // unstable.
  if constexpr (port::IsIos() || port::IsLinuxBase() || port::IsWasm()) {
    context->mutable_converter()->set_use_cascading_window(false);
  }

  return context;
}

void Session::PushUndoContext() {
  // Copy the current context and push it to the undo stack.
  undo_contexts_.emplace_back(std::make_unique<ImeContext>(*context_));
  // If the stack size exceeds the limitation, purge the oldest entries.
  while (undo_contexts_.size() > kMultipleUndoMaxSize) {
    undo_contexts_.pop_front();
  }
  DCHECK_LE(undo_contexts_.size(), kMultipleUndoMaxSize);
  // What the commit before this one told zenz can no longer be taken back.
  turned_down_changes_.clear();
}

void Session::RememberTurnedDown(size_t count) {
  turned_down_changes_.clear();
  const std::vector<ZenzWord> refused = std::exchange(zenz_refused_, {});
  const config::Config& config = context_->GetConfig();
  // Where the commit teaches Mozc nothing, it tells zenz nothing.
  if (!config.zenz_learning() || config.incognito_mode() ||
      config.history_learning_level() != config::Config::DEFAULT_HISTORY) {
    return;
  }
  floatype::ZenzMemory& turned_down = floatype::ZenzMemory::TurnedDown();
  floatype::ZenzMemory& once = floatype::ZenzMemory::RefusedOnce();
  const auto add = [this](floatype::ZenzMemory& memory, const std::string& key,
                          const std::string& value) {
    if (key.empty() || value.empty()) {
      return;
    }
    if (!memory.Contains(key, value)) {
      turned_down_changes_.push_back({&memory, true, key, value});
    }
    memory.Add(key, value);
  };
  const auto remove = [this](floatype::ZenzMemory& memory,
                             const std::string& key, const std::string& value) {
    if (memory.Contains(key, value)) {
      memory.Remove(key, value);
      turned_down_changes_.push_back({&memory, false, key, value});
    }
  };
  // A candidate chosen over the one shown first: what was shown is turned
  // down, and what is chosen is zenz's to write again.
  const std::vector<EngineConverterInterface::ReplacedCandidate> replaced =
      context_->converter().ReplacedCandidates();
  bool chose = false;
  for (const EngineConverterInterface::ReplacedCandidate& choice : replaced) {
    if (choice.segment >= count || !choice.learned) {
      continue;
    }
    chose = true;
    add(turned_down, choice.shown_key, choice.shown_value);
    remove(turned_down, choice.chosen_key, choice.chosen_value);
    remove(once, choice.chosen_key, choice.chosen_value);
  }
  // The words of a correction Space left for Mozc's result. Where the user
  // went on to choose, the choice says which word was wrong: the word of a
  // segment chosen again is turned down at once, and the others are left
  // alone (後 of 完治に戻った後, when 完治 is what was chosen over). Where
  // Mozc's result was committed as it was, Space may have been a slip: a
  // word is turned down the second time.
  const Segments& segments = context_->converter().GetSegments();
  for (const ZenzWord& word : refused) {
    if (word.segment >= count ||
        word.segment >= segments.conversion_segments_size() ||
        segments.conversion_segment(word.segment).key() != word.segment_key) {
      continue;
    }
    if (!chose) {
      if (once.Contains(word.key, word.value)) {
        remove(once, word.key, word.value);
        add(turned_down, word.key, word.value);
      } else {
        add(once, word.key, word.value);
      }
      continue;
    }
    const auto choice = std::find_if(
        replaced.begin(), replaced.end(),
        [&word](const EngineConverterInterface::ReplacedCandidate& choice) {
          return choice.segment == word.segment;
        });
    if (choice != replaced.end() && choice->learned &&
        (choice->chosen_key != word.key ||
         choice->chosen_value != word.value)) {
      remove(once, word.key, word.value);
      add(turned_down, word.key, word.value);
    }
  }
}

void Session::ForgetLastTurnedDown() {
  for (auto change = turned_down_changes_.rbegin();
       change != turned_down_changes_.rend(); ++change) {
    if (change->added) {
      change->memory->Remove(change->key, change->value);
    } else {
      change->memory->Add(change->key, change->value);
    }
  }
  turned_down_changes_.clear();
}

void Session::PopUndoContext() {
  if (!HasUndoContext()) {
    return;
  }
  context_ = std::move(undo_contexts_.back());
  undo_contexts_.pop_back();
}

void Session::ClearUndoContext() {
  undo_contexts_.clear();
  turned_down_changes_.clear();
}

bool Session::HasUndoContext() const { return !undo_contexts_.empty(); }

void Session::MaybeSetUndoStatus(commands::Command* command) const {
  if (HasUndoContext()) {
    command->mutable_output()->mutable_status()->set_undo_available(true);
  }
}

void Session::EnsureIMEIsOn() {
  if (context_->state() == ImeContext::DIRECT) {
    SetSessionState(ImeContext::PRECOMPOSITION, context_.get());
  }
}

bool Session::SendCommand(commands::Command* command) {
  UpdateTime();
  UpdatePreferences(command);
  if (!command->input().has_command()) {
    return false;
  }
  TransformInput(command->mutable_input());

  const commands::SessionCommand& session_command = command->input().command();
  switch (session_command.type()) {
    case commands::SessionCommand::POLL_ZENZ:
    case commands::SessionCommand::LIVE_CONVERT:
    case commands::SessionCommand::REQUEST_ZENZ:
    case commands::SessionCommand::GET_STATUS:
      break;  // Timer callbacks and queries, not user actions.
    default:
      // floatype: with a live conversion the window shown holds the
      // suggestions, so a candidate id from it is theirs.
      if (std::unique_ptr<ImeContext> suggestions =
              std::move(live_suggestions_);
          suggestions != nullptr && live_conversion_ && !commit_after_zenz_ &&
          (session_command.type() ==
               commands::SessionCommand::SELECT_CANDIDATE ||
           session_command.type() ==
               commands::SessionCommand::SUBMIT_CANDIDATE ||
           session_command.type() ==
               commands::SessionCommand::HIGHLIGHT_CANDIDATE)) {
        context_ = std::move(suggestions);
        waiting_for_zenz_ = false;
      }
      english_undo_.reset();
      zenz_.Cancel();
      StopWaitingForZenz();
      commit_after_zenz_ = false;
      space_after_commit_ = false;
      english_run_ = false;
      zenz_indices_.clear();
      mozc_segments_.reset();
      live_conversion_ = false;
      live_reading_.clear();
  }
  bool result = false;
  if (session_command.type() ==
      commands::SessionCommand::SWITCH_COMPOSITION_MODE) {
    if (!session_command.has_composition_mode()) {
      return false;
    }
    switch (session_command.composition_mode()) {
      case commands::DIRECT:
        // TODO(komatsu): Implement here.
        break;
      case commands::HIRAGANA:
        result = CompositionModeHiragana(command);
        break;
      case commands::FULL_KATAKANA:
        result = CompositionModeFullKatakana(command);
        break;
      case commands::HALF_ASCII:
        result = CompositionModeHalfASCII(command);
        break;
      case commands::FULL_ASCII:
        result = CompositionModeFullASCII(command);
        break;
      case commands::HALF_KATAKANA:
        result = CompositionModeHalfKatakana(command);
        break;
      default:
        LOG(ERROR) << "Unknown mode: " << session_command.composition_mode();
        break;
    }
    MaybeSetUndoStatus(command);
    return result;
  }

  DCHECK_EQ(false, result);
  switch (command->input().command().type()) {
    case commands::SessionCommand::NONE:
      result = DoNothing(command);
      break;
    case commands::SessionCommand::REVERT:
      result = Revert(command);
      break;
    case commands::SessionCommand::SUBMIT:
      result = Commit(command);
      break;
    case commands::SessionCommand::SELECT_CANDIDATE:
      result = SelectCandidate(command);
      break;
    case commands::SessionCommand::SUBMIT_CANDIDATE:
      result = CommitCandidate(command);
      break;
    case commands::SessionCommand::HIGHLIGHT_CANDIDATE:
      result = HighlightCandidate(command);
      break;
    case commands::SessionCommand::GET_STATUS:
      result = GetStatus(command);
      break;
    case commands::SessionCommand::CONVERT_REVERSE:
      result = ConvertReverse(command);
      break;
    case commands::SessionCommand::UNDO:
      result = Undo(command);
      break;
    case commands::SessionCommand::RESET_CONTEXT:
      result = ResetContext(command);
      break;
    case commands::SessionCommand::MOVE_CURSOR:
      result = MoveCursorTo(command);
      break;
    case commands::SessionCommand::SWITCH_INPUT_FIELD_TYPE:
      result = SwitchInputFieldType(command);
      break;
    case commands::SessionCommand::UNDO_OR_REWIND:
      result = UndoOrRewind(command);
      break;
    case commands::SessionCommand::COMMIT_RAW_TEXT:
      result = CommitRawText(command);
      break;
    case commands::SessionCommand::CONVERT_PREV_PAGE:
      result = ConvertPrevPage(command);
      break;
    case commands::SessionCommand::CONVERT_NEXT_PAGE:
      result = ConvertNextPage(command);
      break;
    case commands::SessionCommand::TURN_ON_IME:
      result = MakeSureIMEOn(command);
      break;
    case commands::SessionCommand::TURN_OFF_IME:
      result = MakeSureIMEOff(command);
      break;
    case commands::SessionCommand::DELETE_CANDIDATE_FROM_HISTORY:
      result = DeleteCandidateFromHistory(command);
      break;
    case commands::SessionCommand::STOP_KEY_TOGGLING:
      result = StopKeyToggling(command);
      break;
    case commands::SessionCommand::UPDATE_COMPOSITION:
      result = UpdateComposition(command);
      break;
    case commands::SessionCommand::REQUEST_NWP: {
      ConversionPreferences conversion_preferences =
          context_->converter().conversion_preferences();
      conversion_preferences.request_suggestion =
          command->input().request_suggestion();
      // Resets converter's state (e.g. previous segments).
      // NWP will be generated from surrounding text given by the client.
      context_->mutable_converter()->Reset();
      result = context_->mutable_converter()->Suggest(
          context_->composer(), command->input().context(),
          conversion_preferences);
      if (result) {
        Output(command);
      }
      break;
    }
    case commands::SessionCommand::POLL_ZENZ:
      result = PollZenz(command);
      break;
    case commands::SessionCommand::LIVE_CONVERT:
      result = LiveConvert(command);
      break;
    case commands::SessionCommand::REQUEST_ZENZ:
      result = RequestZenzAfterLiveConversion(command);
      break;
    default:
      LOG(WARNING) << "Unknown command" << *command;
      result = DoNothing(command);
      break;
  }
  SetIndicator(command);
  SetCandidateLook(command);
  MaybeSetUndoStatus(command);
  return result;
}

bool Session::TestSendKey(commands::Command* command) {
  UpdateTime();
  UpdatePreferences(command);
  TransformInput(command->mutable_input());

  if (context_->state() == ImeContext::NONE) {
    // This must be an error.
    LOG(ERROR) << "Invalid state: NONE";
    return false;
  }

  const commands::KeyEvent& key = command->input().key();

  // To support indirect IME on/off by using KeyEvent::activated, use effective
  // state instead of directly using context_->state().
  const ImeContext::State state =
      GetEffectiveStateForTestSendKey(key, context_->state());

  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();

  // Direct input
  if (state == ImeContext::DIRECT) {
    keymap::DirectInputState::Commands key_command;
    if (!keymap->GetCommandDirect(key, &key_command) ||
        key_command == keymap::DirectInputState::NONE) {
      return EchoBack(command);
    }
    return DoNothing(command);
  }

  // Precomposition
  if (state == ImeContext::PRECOMPOSITION) {
    keymap::PrecompositionState::Commands key_command;
    const bool is_suggestion =
        context_->converter().CheckState(EngineConverterInterface::SUGGESTION);
    const bool result =
        is_suggestion ? keymap->GetCommandZeroQuerySuggestion(key, &key_command)
                      : keymap->GetCommandPrecomposition(key, &key_command);
    if (!result || key_command == keymap::PrecompositionState::NONE) {
      // Clear undo context just in case. b/5529702.
      // Note that the undo context will not be cleared in
      // EchoBackAndClearUndoContext if the key event consists of modifier keys
      // only.
      return EchoBackAndClearUndoContext(command);
    }
    // If the input_style is DIRECT_INPUT, KeyEvent is not consumed
    // and done echo back.  It works only when key_string is equal to
    // key_code.  We should fix this limitation when the as_is flag is
    // used for rather than numpad characters.
    if (key_command == keymap::PrecompositionState::INSERT_CHARACTER &&
        key.input_style() == commands::KeyEvent::DIRECT_INPUT) {
      return EchoBack(command);
    }

    // TODO(komatsu): This is a hack to work around the problem with
    // the inconsistency between TestSendKey and SendKey.
    switch (key_command) {
      case keymap::PrecompositionState::INSERT_SPACE:
        if (!IsFullWidthInsertSpace(command->input()) && IsPureSpaceKey(key)) {
          return EchoBackAndClearUndoContext(command);
        }
        return DoNothing(command);
      case keymap::PrecompositionState::INSERT_ALTERNATE_SPACE:
        if (IsFullWidthInsertSpace(command->input()) && IsPureSpaceKey(key)) {
          return EchoBackAndClearUndoContext(command);
        }
        return DoNothing(command);
      case keymap::PrecompositionState::INSERT_HALF_SPACE:
        if (IsPureSpaceKey(key)) {
          return EchoBackAndClearUndoContext(command);
        }
        return DoNothing(command);
      case keymap::PrecompositionState::INSERT_FULL_SPACE:
        return DoNothing(command);
      default:
        // Do nothing.
        break;
    }

    if (key_command == keymap::PrecompositionState::REVERT) {
      return Revert(command);
    }

    // If undo context is empty, echoes back the key event so that it can be
    // handled by the application. b/5553298
    if (key_command == keymap::PrecompositionState::UNDO && !HasUndoContext()) {
      return EchoBack(command);
    }

    return DoNothing(command);
  }

  // Do nothing.
  return DoNothing(command);
}

bool Session::SendKey(commands::Command* command) {
  // floatype: a letter right after a live conversion that kept a word ending
  // in loose letters goes on with the keys as typed.
  if (std::optional<composer::Composer> typed = std::exchange(english_undo_, std::nullopt);
      typed.has_value() && live_conversion_) {
    const commands::KeyEvent& key = command->input().key();
    // ー follows kana, never letters: after a word typed wholly into kana
    // (のて shown as note), its key goes on in kana too (のてーま).
    const auto all_kana = [&typed]() {
      const std::string query = typed->GetQueryForConversion();
      return std::none_of(query.begin(), query.end(), [](char c) {
        return absl::ascii_isalpha(static_cast<unsigned char>(c));
      });
    };
    if (key.has_key_code() && key.modifier_keys_size() == 0 &&
        (absl::ascii_isalpha(static_cast<unsigned char>(key.key_code())) ||
         (key.key_code() == '-' && all_kana()))) {
      *context_->mutable_composer() = *std::move(typed);
    }
  }
  // floatype: a key that predicts while a live conversion shows suggestions
  // picks from them, as it did before the pause.
  std::unique_ptr<ImeContext> suggestions = std::move(live_suggestions_);
  if (keymap::CompositionState::Commands key_command;
      suggestions != nullptr && live_conversion_ && !commit_after_zenz_ &&
      context_->GetKeyMapManager().GetCommandSuggestion(command->input().key(),
                                                        &key_command) &&
      key_command == keymap::CompositionState::PREDICT_AND_CONVERT) {
    context_ = std::move(suggestions);
    live_conversion_ = false;
    waiting_for_zenz_ = false;
    live_reading_.clear();
  }
  zenz_.Cancel();
  zenz_scheduled_ = false;
  StopWaitingForZenz();
  // The commit waiting for zenz is the word of the Space before this key.
  const bool space_pending = space_after_commit_;
  // floatype: a key before zenz answered a direct commit commits what is on
  // screen; Enter does only that, other keys go on as typed after it.
  if (std::exchange(commit_after_zenz_, false) &&
      context_->state() == ImeContext::CONVERSION) {
    keymap::ConversionState::Commands key_command;
    if (context_->GetKeyMapManager().GetCommandConversion(
            command->input().key(), &key_command) &&
        key_command == keymap::ConversionState::COMMIT) {
      const bool committed = Commit(command);
      AppendCommittedSpace(command);
      return committed;
    }
    CommitNotTriggeringZeroQuerySuggest(command);
    AppendCommittedSpace(command);
    live_conversion_ = false;
  }
  space_after_commit_ = false;
  english_kept_ = false;
  const bool run_committed = space_pending && command->output().has_result();
  // A click or a change of focus ends a run of English with no key: the text
  // the app then reports before the caret is not the word and its space.
  if (english_run_ && !run_committed &&
      context_->state() == ImeContext::PRECOMPOSITION &&
      command->input().context().has_preceding_text()) {
    const std::string& before = command->input().context().preceding_text();
    if (before.size() < 2 || before.back() != ' ' ||
        !absl::ascii_isgraph(
            static_cast<unsigned char>(before[before.size() - 2]))) {
      english_run_ = false;
    }
  }
  // The key that starts a composition knows best what comes before it.
  if (context_->state() == ImeContext::PRECOMPOSITION) {
    follows_japanese_ = FollowsJapanese(*command);
  }
  const std::vector<int> zenz_indices = std::exchange(zenz_indices_, {});
  // What Space refused of a correction is told at the commit of that
  // conversion, not of a later one.
  if (context_->state() != ImeContext::CONVERSION) {
    zenz_refused_.clear();
  }
  const bool live = live_conversion_;
  live_conversion_ = false;
  UpdateTime();
  UpdatePreferences(command);
  TransformInput(command->mutable_input());
  // To support indirect IME on/off by using KeyEvent::activated, use effective
  // state instead of directly using context_->state().
  HandleIndirectImeOnOff(command);

  // floatype: typing on or deleting after a live conversion edits the reading,
  // so the conversion turns back into the composition first.
  if (live && context_->state() == ImeContext::CONVERSION) {
    const commands::KeyEvent& key = command->input().key();
    keymap::ConversionState::Commands key_command;
    const bool backspace = key.special_key() == commands::KeyEvent::BACKSPACE &&
                           key.modifier_keys_size() == 0;
    if (backspace ||
        (context_->GetKeyMapManager().GetCommandConversion(key, &key_command) &&
         key_command == keymap::ConversionState::INSERT_CHARACTER)) {
      context_->mutable_converter()->Cancel();
      SetSessionState(ImeContext::COMPOSITION, context_.get());
    }
  }

  // floatype: Backspace right after a commit takes back what the commit
  // taught the converter; the key still reaches the app. Escape is left out:
  // it follows a commit routinely in vim and terminals. Ctrl+Backspace (Undo)
  // also brings the composition back.
  if (const commands::KeyEvent& key = command->input().key();
      context_->state() == ImeContext::PRECOMPOSITION && HasUndoContext() &&
      key.modifier_keys_size() == 0 &&
      key.special_key() == commands::KeyEvent::BACKSPACE) {
    context_->mutable_converter()->Revert();
    ForgetLastTurnedDown();
    ClearUndoContext();
  }

  // floatype: Space after an English word commits and types a space.
  const bool spaced = TypeSpaceAfterEnglish(live, command);

  // floatype: while zenz's correction is shown, Space (any key for the next
  // candidate) shows Mozc's result again, as Mozkey does; the Space after it
  // goes through Mozc's candidates in Mozc's order. The words zenz had
  // changed are noted: committing the conversion without them tells zenz not
  // to write them again (RememberTurnedDown()).
  if (keymap::ConversionState::Commands key_command;
      !spaced && !zenz_indices.empty() &&
      context_->state() == ImeContext::CONVERSION &&
      context_->GetKeyMapManager().GetCommandConversion(command->input().key(),
                                                        &key_command) &&
      key_command == keymap::ConversionState::CONVERT_NEXT) {
    zenz_refused_ = zenz_words_;
    return ShowMozcAgain(live, command);
  }
  // Ctrl+Delete (the key that deletes a suggestion) while zenz's correction is
  // shown stops it, and zenz does not make it again for this reading.
  if (keymap::ConversionState::Commands key_command;
      !zenz_indices.empty() && context_->state() == ImeContext::CONVERSION &&
      context_->GetKeyMapManager().GetCommandConversion(command->input().key(),
                                                        &key_command) &&
      key_command == keymap::ConversionState::DELETE_SELECTED_CANDIDATE) {
    if (context_->GetConfig().zenz_learning()) {
      floatype::ZenzMemory::Stopped().Add(zenz_reading_, zenz_text_);
      floatype::ZenzMemory::Accepted().Remove(zenz_reading_, zenz_text_);
    }
    return ShowMozcAgain(live, command);
  }
  // The manual zenz key changes nothing on screen: a live conversion stays
  // live, and zenz's correction stays zenz's until a new answer comes.
  if (keymap::ConversionState::Commands key_command;
      context_->state() == ImeContext::CONVERSION &&
      context_->GetKeyMapManager().GetCommandConversion(command->input().key(),
                                                        &key_command) &&
      key_command == keymap::ConversionState::ZENZ_CORRECT) {
    live_conversion_ = live;
    zenz_indices_ = zenz_indices;
    live_suggestions_ = std::move(suggestions);
  }

  // floatype: Enter commits what is on screen, kept live conversion included.
  if (const std::string shown = ShownAfterLiveConversion(); !shown.empty()) {
    keymap::CompositionState::Commands key_command;
    if (context_->GetKeyMapManager().GetCommandComposition(
            command->input().key(), &key_command) &&
        key_command == keymap::CompositionState::COMMIT) {
      const std::string reading = context_->composer().GetQueryForConversion();
      live_reading_.clear();
      CommitStringDirectly(reading, shown, command);
      return true;
    }
  }

  // floatype: the commands after the first of a key bound to several.
  const std::vector<std::string> following =
      context_->GetKeyMapManager().GetFollowingCommands(KeymapStateName(),
                                                        command->input().key());
  bool result = spaced;
  switch (spaced ? ImeContext::NONE : context_->state()) {
    case ImeContext::DIRECT:
      result = SendKeyDirectInputState(command);
      break;

    case ImeContext::PRECOMPOSITION:
      result = SendKeyPrecompositionState(command);
      break;

    case ImeContext::COMPOSITION:
      result = SendKeyCompositionState(command);
      break;

    case ImeContext::CONVERSION:
      result = SendKeyConversionState(command);
      break;

    case ImeContext::NONE:
      break;
  }
  // A later command the state does not have is skipped; the key stays as the
  // first command left it.
  for (const std::string& name : following) {
    if (spaced) {
      break;
    }
    RunNamedCommand(name, command);
  }

  if (const std::string shown = ShownAfterLiveConversion(); !shown.empty()) {
    commands::Preedit* preedit = command->mutable_output()->mutable_preedit();
    preedit->Clear();
    commands::Preedit::Segment* segment = preedit->add_segment();
    segment->set_annotation(commands::Preedit::Segment::UNDERLINE);
    segment->set_value(shown);
    segment->set_value_length(Util::CharsLen(shown));
    segment->set_key(context_->composer().GetStringForPreedit());
    preedit->set_cursor(Util::CharsLen(shown));
  } else if (!live_conversion_) {
    live_reading_.clear();
  }
  // floatype: committing zenz's correction as it was shown teaches it.
  if (!zenz_indices.empty() && command->output().has_result() &&
      context_->GetConfig().zenz_learning() &&
      japanese::FullWidthAsciiToHalfWidthAscii(
          command->output().result().value()) ==
          japanese::FullWidthAsciiToHalfWidthAscii(zenz_text_)) {
    floatype::ZenzMemory::Accepted().Add(zenz_reading_, zenz_text_);
    // A word left once for Mozc's result and now taken starts over.
    for (const ZenzWord& word : zenz_words_) {
      floatype::ZenzMemory::RefusedOnce().Remove(word.key, word.value);
    }
  }
  // A run of English goes on while its next word is typed: any other commit,
  // and any key that leaves nothing composed, ends it.
  english_run_ = spaced ||
                 (english_run_ &&
                  context_->state() != ImeContext::PRECOMPOSITION &&
                  (run_committed || !command->output().has_result()));
  WarmZenz();
  if (MaybeCommitOnCharacter(command)) {
    SetIndicator(command);
    SetCandidateLook(command);
    MaybeSetUndoStatus(command);
    return true;
  }
  MaybeScheduleLiveConversion(command);
  SetIndicator(command);
  SetCandidateLook(command);
  MaybeSetUndoStatus(command);
  return result;
}

bool Session::UpdateCompositionInternal(commands::Command* command) {
  command->mutable_output()->set_consumed(true);

  context_->mutable_composer()->Reset();
  // Use the top entry for now.
  context_->mutable_composer()->SetCompositionsForHandwriting(
      command->input().command().composition_events());
  ClearUndoContext();
  SetSessionState(ImeContext::COMPOSITION, context_.get());

  if (Suggest(command->input())) {
    Output(command);
    return true;
  }

  OutputComposition(command);
  return true;
}

bool Session::UpdateComposition(commands::Command* command) {
  bool result = false;
  switch (context_->state()) {
    case ImeContext::DIRECT:
      result = EchoBackAndClearUndoContext(command);
      break;

    case ImeContext::PRECOMPOSITION:
      [[fallthrough]];
    case ImeContext::COMPOSITION:
      result = UpdateCompositionInternal(command);
      break;

    case ImeContext::CONVERSION:
      result = false;
      break;

    case ImeContext::NONE:
      result = false;
      break;
  }
  return result;
}

bool Session::SendKeyDirectInputState(commands::Command* command) {
  keymap::DirectInputState::Commands key_command;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  if (!keymap->GetCommandDirect(command->input().key(), &key_command)) {
    return EchoBackAndClearUndoContext(command);
  }

  return RunDirectInputCommand(key_command, command);
}

bool Session::RunDirectInputCommand(
    keymap::DirectInputState::Commands key_command,
    commands::Command* command) {
  switch (key_command) {
    case keymap::DirectInputState::IME_ON:
      return IMEOn(command);
    case keymap::DirectInputState::COMPOSITION_MODE_HIRAGANA:
      return CompositionModeHiragana(command);
    case keymap::DirectInputState::COMPOSITION_MODE_FULL_KATAKANA:
      return CompositionModeFullKatakana(command);
    case keymap::DirectInputState::COMPOSITION_MODE_HALF_KATAKANA:
      return CompositionModeHalfKatakana(command);
    case keymap::DirectInputState::COMPOSITION_MODE_FULL_ALPHANUMERIC:
      return CompositionModeFullASCII(command);
    case keymap::DirectInputState::COMPOSITION_MODE_HALF_ALPHANUMERIC:
      return CompositionModeHalfASCII(command);
    case keymap::DirectInputState::NONE:
      return EchoBackAndClearUndoContext(command);
    case keymap::DirectInputState::RECONVERT:
      return RequestConvertReverse(command);
  }
  return false;
}

bool Session::SendKeyPrecompositionState(commands::Command* command) {
  keymap::PrecompositionState::Commands key_command;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::SUGGESTION)
          ? keymap->GetCommandZeroQuerySuggestion(command->input().key(),
                                                  &key_command)
          : keymap->GetCommandPrecomposition(command->input().key(),
                                             &key_command);

  if (!result) {
    return EchoBackAndClearUndoContext(command);
  }

  // Update the client context (if any) for later use. Note that the client
  // context is updated only here. In other words, we will stop updating the
  // client context once a conversion starts (mainly for performance reasons).
  if (command->has_input() && command->input().has_context()) {
    *context_->mutable_client_context() = command->input().context();
  } else {
    context_->mutable_client_context()->Clear();
  }
  // floatype: after an English word and the space this session typed, the
  // next word converts as the start of a phrase. Mozc would take the word
  // before the space for the one before it: に after "push " came out 二.
  if (english_run_) {
    context_->mutable_client_context()->clear_preceding_text();
  }

  return RunPrecompositionCommand(key_command, command);
}

bool Session::RunPrecompositionCommand(
    keymap::PrecompositionState::Commands key_command,
    commands::Command* command) {
  switch (key_command) {
    case keymap::PrecompositionState::INSERT_CHARACTER:
      return InsertCharacter(command);
    case keymap::PrecompositionState::INSERT_SPACE:
      return InsertSpace(command);
    case keymap::PrecompositionState::INSERT_ALTERNATE_SPACE:
      return InsertSpaceToggled(command);
    case keymap::PrecompositionState::INSERT_HALF_SPACE:
      return InsertSpaceHalfWidth(command);
    case keymap::PrecompositionState::INSERT_FULL_SPACE:
      return InsertSpaceFullWidth(command);
    case keymap::PrecompositionState::TOGGLE_ALPHANUMERIC_MODE:
      return ToggleAlphanumericMode(command);
    case keymap::PrecompositionState::REVERT:
      return Revert(command);
    case keymap::PrecompositionState::UNDO:
      return RequestUndo(command);
    case keymap::PrecompositionState::IME_OFF:
      return IMEOff(command);
    case keymap::PrecompositionState::IME_ON:
      return DoNothing(command);

    case keymap::PrecompositionState::COMPOSITION_MODE_HIRAGANA:
      return CompositionModeHiragana(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_FULL_KATAKANA:
      return CompositionModeFullKatakana(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_HALF_KATAKANA:
      return CompositionModeHalfKatakana(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_FULL_ALPHANUMERIC:
      return CompositionModeFullASCII(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_HALF_ALPHANUMERIC:
      return CompositionModeHalfASCII(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_SWITCH_KANA_TYPE:
      return CompositionModeSwitchKanaType(command);

    case keymap::PrecompositionState::LAUNCH_CONFIG_DIALOG:
      return LaunchConfigDialog(command);
    case keymap::PrecompositionState::LAUNCH_DICTIONARY_TOOL:
      return LaunchDictionaryTool(command);
    case keymap::PrecompositionState::LAUNCH_WORD_REGISTER_DIALOG:
      return LaunchWordRegisterDialog(command);

    // For zero query suggestion
    case keymap::PrecompositionState::CANCEL:
      // It is a little kind of abuse of the EditCancel command.  It
      // would be nice to make a new command when EditCancel is
      // extended or the requirement of this command is added.
      return EditCancel(command);
    case keymap::PrecompositionState::CANCEL_AND_IME_OFF:
      // The same to keymap::PrecompositionState::CANCEL.
      return EditCancelAndIMEOff(command);
    // For zero query suggestion
    case keymap::PrecompositionState::COMMIT_FIRST_SUGGESTION:
      return CommitFirstSuggestion(command);
    // For zero query suggestion
    case keymap::PrecompositionState::PREDICT_AND_CONVERT:
      return PredictAndConvert(command);

    case keymap::PrecompositionState::NONE:
      return EchoBackAndClearUndoContext(command);
    case keymap::PrecompositionState::RECONVERT:
      return RequestConvertReverse(command);

    case keymap::PrecompositionState::IME_ACTION:
      return ImeAction(command);
  }
  return false;
}

bool Session::SendKeyCompositionState(commands::Command* command) {
  keymap::CompositionState::Commands key_command;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::SUGGESTION)
          ? keymap->GetCommandSuggestion(command->input().key(), &key_command)
          : keymap->GetCommandComposition(command->input().key(), &key_command);

  if (!result) {
    return DoNothing(command);
  }

  return RunCompositionCommand(key_command, command);
}

bool Session::RunCompositionCommand(
    keymap::CompositionState::Commands key_command,
    commands::Command* command) {
  switch (key_command) {
    case keymap::CompositionState::INSERT_CHARACTER:
      return InsertCharacter(command);

    case keymap::CompositionState::COMMIT:
      return Commit(command);

    case keymap::CompositionState::COMMIT_FIRST_SUGGESTION:
      return CommitFirstSuggestion(command);

    case keymap::CompositionState::CONVERT:
      return Convert(command);

    case keymap::CompositionState::CONVERT_WITHOUT_HISTORY:
      return ConvertWithoutHistory(command);

    case keymap::CompositionState::PREDICT_AND_CONVERT:
      return PredictAndConvert(command);

    case keymap::CompositionState::DEL:
      return Delete(command);

    case keymap::CompositionState::BACKSPACE:
      return Backspace(command);

    case keymap::CompositionState::INSERT_SPACE:
      return InsertSpace(command);

    case keymap::CompositionState::INSERT_ALTERNATE_SPACE:
      return InsertSpaceToggled(command);

    case keymap::CompositionState::INSERT_HALF_SPACE:
      return InsertSpaceHalfWidth(command);

    case keymap::CompositionState::INSERT_FULL_SPACE:
      return InsertSpaceFullWidth(command);

    case keymap::CompositionState::MOVE_CURSOR_LEFT:
      return MoveCursorLeft(command);

    case keymap::CompositionState::MOVE_CURSOR_RIGHT:
      return MoveCursorRight(command);

    case keymap::CompositionState::MOVE_CURSOR_TO_BEGINNING:
      return MoveCursorToBeginning(command);

    case keymap::CompositionState::MOVE_MOVE_CURSOR_TO_END:
      return MoveCursorToEnd(command);

    case keymap::CompositionState::CANCEL:
      return EditCancel(command);

    case keymap::CompositionState::CANCEL_AND_IME_OFF:
      return EditCancelAndIMEOff(command);

    case keymap::CompositionState::UNDO:
      return RequestUndo(command);

    case keymap::CompositionState::IME_OFF:
      return IMEOff(command);

    case keymap::CompositionState::IME_ON:
      return DoNothing(command);

    case keymap::CompositionState::CONVERT_TO_HIRAGANA:
      return ConvertToHiragana(command);

    case keymap::CompositionState::CONVERT_TO_FULL_KATAKANA:
      return ConvertToFullKatakana(command);

    case keymap::CompositionState::CONVERT_TO_HALF_KATAKANA:
      return ConvertToHalfKatakana(command);

    case keymap::CompositionState::CONVERT_TO_HALF_WIDTH:
      return ConvertToHalfWidth(command);

    case keymap::CompositionState::CONVERT_TO_FULL_ALPHANUMERIC:
      return ConvertToFullASCII(command);

    case keymap::CompositionState::CONVERT_TO_HALF_ALPHANUMERIC:
      return ConvertToHalfASCII(command);

    case keymap::CompositionState::SWITCH_KANA_TYPE:
      return SwitchKanaType(command);

    case keymap::CompositionState::DISPLAY_AS_HIRAGANA:
      return DisplayAsHiragana(command);

    case keymap::CompositionState::DISPLAY_AS_FULL_KATAKANA:
      return DisplayAsFullKatakana(command);

    case keymap::CompositionState::DISPLAY_AS_HALF_KATAKANA:
      return DisplayAsHalfKatakana(command);

    case keymap::CompositionState::TRANSLATE_HALF_WIDTH:
      return TranslateHalfWidth(command);

    case keymap::CompositionState::TRANSLATE_FULL_ASCII:
      return TranslateFullASCII(command);

    case keymap::CompositionState::TRANSLATE_HALF_ASCII:
      return TranslateHalfASCII(command);

    case keymap::CompositionState::TOGGLE_ALPHANUMERIC_MODE:
      return ToggleAlphanumericMode(command);

    case keymap::CompositionState::COMPOSITION_MODE_HIRAGANA:
      return CompositionModeHiragana(command);

    case keymap::CompositionState::COMPOSITION_MODE_FULL_KATAKANA:
      return CompositionModeFullKatakana(command);

    case keymap::CompositionState::COMPOSITION_MODE_HALF_KATAKANA:
      return CompositionModeHalfKatakana(command);

    case keymap::CompositionState::COMPOSITION_MODE_FULL_ALPHANUMERIC:
      return CompositionModeFullASCII(command);

    case keymap::CompositionState::COMPOSITION_MODE_HALF_ALPHANUMERIC:
      return CompositionModeHalfASCII(command);

    case keymap::CompositionState::ZENZ_CORRECT:
      return CorrectWithZenzNow(command);

    case keymap::CompositionState::NONE:
      return DoNothing(command);
  }
  return false;
}

bool Session::SendKeyConversionState(commands::Command* command) {
  keymap::ConversionState::Commands key_command;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::PREDICTION)
          ? keymap->GetCommandPrediction(command->input().key(), &key_command)
          : keymap->GetCommandConversion(command->input().key(), &key_command);

  if (!result) {
    return DoNothing(command);
  }

  return RunConversionCommand(key_command, command);
}

bool Session::RunConversionCommand(
    keymap::ConversionState::Commands key_command, commands::Command* command) {
  switch (key_command) {
    case keymap::ConversionState::INSERT_CHARACTER:
      return InsertCharacter(command);

    case keymap::ConversionState::INSERT_SPACE:
      return InsertSpace(command);

    case keymap::ConversionState::INSERT_ALTERNATE_SPACE:
      return InsertSpaceToggled(command);

    case keymap::ConversionState::INSERT_HALF_SPACE:
      return InsertSpaceHalfWidth(command);

    case keymap::ConversionState::INSERT_FULL_SPACE:
      return InsertSpaceFullWidth(command);

    case keymap::ConversionState::COMMIT:
      return Commit(command);

    case keymap::ConversionState::COMMIT_SEGMENT:
      return CommitSegment(command);

    case keymap::ConversionState::CONVERT_NEXT:
      return ConvertNext(command);

    case keymap::ConversionState::CONVERT_PREV:
      return ConvertPrev(command);

    case keymap::ConversionState::CONVERT_NEXT_PAGE:
      return ConvertNextPage(command);

    case keymap::ConversionState::CONVERT_PREV_PAGE:
      return ConvertPrevPage(command);

    case keymap::ConversionState::PREDICT_AND_CONVERT:
      return PredictAndConvert(command);

    case keymap::ConversionState::SEGMENT_FOCUS_LEFT:
      return SegmentFocusLeft(command);

    case keymap::ConversionState::SEGMENT_FOCUS_RIGHT:
      return SegmentFocusRight(command);

    case keymap::ConversionState::SEGMENT_FOCUS_FIRST:
      return SegmentFocusLeftEdge(command);

    case keymap::ConversionState::SEGMENT_FOCUS_LAST:
      return SegmentFocusLast(command);

    case keymap::ConversionState::SEGMENT_WIDTH_EXPAND:
      return SegmentWidthExpand(command);

    case keymap::ConversionState::SEGMENT_WIDTH_SHRINK:
      return SegmentWidthShrink(command);

    case keymap::ConversionState::CANCEL:
      return ConvertCancel(command);

    case keymap::ConversionState::CANCEL_AND_IME_OFF:
      return EditCancelAndIMEOff(command);

    case keymap::ConversionState::UNDO:
      return RequestUndo(command);

    case keymap::ConversionState::IME_OFF:
      return IMEOff(command);

    case keymap::ConversionState::IME_ON:
      return DoNothing(command);

    case keymap::ConversionState::CONVERT_TO_HIRAGANA:
      return ConvertToHiragana(command);

    case keymap::ConversionState::CONVERT_TO_FULL_KATAKANA:
      return ConvertToFullKatakana(command);

    case keymap::ConversionState::CONVERT_TO_HALF_KATAKANA:
      return ConvertToHalfKatakana(command);

    case keymap::ConversionState::CONVERT_TO_HALF_WIDTH:
      return ConvertToHalfWidth(command);

    case keymap::ConversionState::CONVERT_TO_FULL_ALPHANUMERIC:
      return ConvertToFullASCII(command);

    case keymap::ConversionState::CONVERT_TO_HALF_ALPHANUMERIC:
      return ConvertToHalfASCII(command);

    case keymap::ConversionState::SWITCH_KANA_TYPE:
      return SwitchKanaType(command);

    case keymap::ConversionState::DISPLAY_AS_HIRAGANA:
      return DisplayAsHiragana(command);

    case keymap::ConversionState::DISPLAY_AS_FULL_KATAKANA:
      return DisplayAsFullKatakana(command);

    case keymap::ConversionState::DISPLAY_AS_HALF_KATAKANA:
      return DisplayAsHalfKatakana(command);

    case keymap::ConversionState::TRANSLATE_HALF_WIDTH:
      return TranslateHalfWidth(command);

    case keymap::ConversionState::TRANSLATE_FULL_ASCII:
      return TranslateFullASCII(command);

    case keymap::ConversionState::TRANSLATE_HALF_ASCII:
      return TranslateHalfASCII(command);

    case keymap::ConversionState::TOGGLE_ALPHANUMERIC_MODE:
      return ToggleAlphanumericMode(command);

    case keymap::ConversionState::COMPOSITION_MODE_HIRAGANA:
      return CompositionModeHiragana(command);

    case keymap::ConversionState::COMPOSITION_MODE_FULL_KATAKANA:
      return CompositionModeFullKatakana(command);

    case keymap::ConversionState::COMPOSITION_MODE_HALF_KATAKANA:
      return CompositionModeHalfKatakana(command);

    case keymap::ConversionState::COMPOSITION_MODE_FULL_ALPHANUMERIC:
      return CompositionModeFullASCII(command);

    case keymap::ConversionState::COMPOSITION_MODE_HALF_ALPHANUMERIC:
      return CompositionModeHalfASCII(command);

    case keymap::ConversionState::REPORT_BUG:
      return ReportBug(command);

    case keymap::ConversionState::DELETE_SELECTED_CANDIDATE:
      return DeleteCandidateFromHistory(command);

    case keymap::ConversionState::ZENZ_CORRECT:
      return CorrectWithZenzNow(command);

    case keymap::ConversionState::NONE:
      return DoNothing(command);
  }
  return false;
}

void Session::UpdatePreferences(commands::Command* command) {
  DCHECK(command);
  const config::Config& config = command->input().config();
  if (command->input().has_capability()) {
    *context_->mutable_client_capability() = command->input().capability();
  }

  // Update config values modified temporarily.
  // TODO(team): Stop using config for temporary modification.
  if (config.has_selection_shortcut()) {
    context_->mutable_converter()->set_selection_shortcut(
        config.selection_shortcut());
  }

  if constexpr (port::IsIos() || port::IsLinuxBase() || port::IsWasm()) {
    context_->mutable_converter()->set_use_cascading_window(false);
  } else {
    if (config.has_use_cascading_window()) {
      context_->mutable_converter()->set_use_cascading_window(
          config.use_cascading_window());
    }
  }
}

bool Session::IMEOn(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  SetSessionState(ImeContext::PRECOMPOSITION, context_.get());
  if (command->input().has_key() && command->input().key().has_mode()) {
    ApplyCompositionMode(command->input().key().mode(),
                         context_->mutable_composer());
  }
  OutputMode(command);
  return true;
}

bool Session::IMEOff(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  Commit(command);

  // Reset the context.
  context_->mutable_converter()->Reset();

  SetSessionState(ImeContext::DIRECT, context_.get());
  OutputMode(command);
  return true;
}

bool Session::MakeSureIMEOn(mozc::commands::Command* command) {
  if (command->input().has_command() &&
      command->input().command().has_composition_mode() &&
      (command->input().command().composition_mode() == commands::DIRECT)) {
    // This is invalid and unsupported usage.
    return false;
  }

  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::DIRECT) {
    ClearUndoContext();
    SetSessionState(ImeContext::PRECOMPOSITION, context_.get());
  }
  if (command->input().has_command() &&
      command->input().command().has_composition_mode()) {
    ApplyCompositionMode(command->input().command().composition_mode(),
                         context_->mutable_composer());
  }
  OutputMode(command);
  return true;
}

bool Session::MakeSureIMEOff(mozc::commands::Command* command) {
  if (command->input().has_command() &&
      command->input().command().has_composition_mode() &&
      (command->input().command().composition_mode() == commands::DIRECT)) {
    // This is invalid and unsupported usage.
    return false;
  }

  command->mutable_output()->set_consumed(true);
  if (context_->state() != ImeContext::DIRECT) {
    ClearUndoContext();
    Commit(command);
    // Reset the context.
    context_->mutable_converter()->Reset();
    SetSessionState(ImeContext::DIRECT, context_.get());
  }
  if (command->input().has_command() &&
      command->input().command().has_composition_mode()) {
    ApplyCompositionMode(command->input().command().composition_mode(),
                         context_->mutable_composer());
  }
  OutputMode(command);
  return true;
}

bool Session::EchoBack(commands::Command* command) {
  command->mutable_output()->set_consumed(false);
  context_->mutable_converter()->Reset();
  OutputKey(command);
  return true;
}

bool Session::EchoBackAndClearUndoContext(commands::Command* command) {
  command->mutable_output()->set_consumed(false);

  // Don't clear undo context when KeyEvent has a modifier key only.
  // TODO(hsumita): A modifier key may be assigned to another functions.
  //                ex) InsertSpace
  //                We need to check it outside of this function.
  const commands::KeyEvent& key_event = command->input().key();
  if (!IsPureModifierKeyEvent(key_event)) {
    ClearUndoContext();
  }

  return EchoBack(command);
}

bool Session::DoNothing(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  // Quick hack for zero query suggestion.
  // Caveats: Resetting converter causes b/8703702 on Windows.
  // Basically we should not *do* something in DoNothing.
  // TODO(komatsu): Fix this.
  if (context_->GetRequest().zero_query_suggestion() &&
      context_->converter().IsActive() &&
      (context_->state() == ImeContext::PRECOMPOSITION)) {
    context_->mutable_converter()->Reset();
    Output(command);
  }
  if (context_->state() & (ImeContext::COMPOSITION | ImeContext::CONVERSION)) {
    Output(command);
  }
  return true;
}

bool Session::Revert(commands::Command* command) {
  if (context_->state() == ImeContext::PRECOMPOSITION) {
    context_->mutable_converter()->Revert();
    ForgetLastTurnedDown();
    return EchoBackAndClearUndoContext(command);
  }

  if (!(context_->state() &
        (ImeContext::COMPOSITION | ImeContext::CONVERSION))) {
    return DoNothing(command);
  }

  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  SetStateToPredompositionAndCancel(context_.get());
  Output(command);
  return true;
}

bool Session::ResetContext(commands::Command* command) {
  if (context_->state() == ImeContext::PRECOMPOSITION) {
    context_->mutable_converter()->Reset();
    return EchoBackAndClearUndoContext(command);
  }

  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  context_->mutable_converter()->Reset();

  SetStateToPredompositionAndCancel(context_.get());
  Output(command);
  return true;
}

void Session::SetTable(std::shared_ptr<const composer::Table> table) {
  if (!table) {
    return;
  }
  ClearUndoContext();
  context_->mutable_composer()->SetTable(std::move(table));
}

void Session::SetConfig(std::shared_ptr<const config::Config> config) {
  DCHECK(config);
  ClearUndoContext();
  context_->SetConfig(std::move(config));
}

void Session::SetRequest(std::shared_ptr<const commands::Request> request) {
  DCHECK(request);
  ClearUndoContext();
  context_->SetRequest(std::move(request));
}

void Session::SetKeyMapManager(
    std::shared_ptr<const mozc::keymap::KeyMapManager> key_map_manager) {
  DCHECK(key_map_manager);
  context_->SetKeyMapManager(key_map_manager);
}

bool Session::GetStatus(commands::Command* command) {
  OutputMode(command);
  return true;
}

bool Session::RequestConvertReverse(commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION &&
      context_->state() != ImeContext::DIRECT) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  Output(command);

  // Fill callback message.
  commands::SessionCommand* session_command =
      command->mutable_output()->mutable_callback()->mutable_session_command();
  session_command->set_type(commands::SessionCommand::CONVERT_REVERSE);
  return true;
}

bool Session::ConvertReverse(commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION &&
      context_->state() != ImeContext::DIRECT) {
    return DoNothing(command);
  }

  const std::string& composition = command->input().command().text();

  // Validate before requesting reverse conversion
  if (!strings::IsValidUtf8(composition)) {
    DLOG(INFO) << "Input is not valid text as utf8";
    return DoNothing(command);
  }
  for (const char32_t codepoint : Utf8AsChars32(composition)) {
    if (!Util::IsAcceptableCharacterAsCandidate(codepoint)) {
      DLOG(INFO)
          << "Input contains characters not suitable for reverse conversion";
      return DoNothing(command);
    }
  }

  std::string reading;
  if (!context_->mutable_converter()->GetReadingText(composition, &reading)) {
    LOG(ERROR) << "Failed to get reading text";
    return DoNothing(command);
  }

  composer::Composer* composer = context_->mutable_composer();
  composer->Reset();
  ClearUndoContext();
  std::vector<std::string> reading_characters;
  composer->InsertCharacterPreedit(reading);
  composer->set_source_text(composition);
  // start conversion here.
  if (!context_->mutable_converter()->Convert(*composer)) {
    LOG(ERROR) << "Failed to start conversion for reverse conversion";
    return false;
  }

  command->mutable_output()->set_consumed(true);

  SetSessionState(ImeContext::CONVERSION, context_.get());
  context_->mutable_converter()->SetCandidateListVisible(true);
  Output(command);
  return true;
}

bool Session::RequestUndo(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::CONVERSION |
         ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }

  // If undo context is empty, echoes back the key event so that it can be
  // handled by the application. b/5553298
  if (context_->state() == ImeContext::PRECOMPOSITION && !HasUndoContext()) {
    return EchoBack(command);
  }

  command->mutable_output()->set_consumed(true);
  Output(command);

  // Fill callback message.
  commands::SessionCommand* session_command =
      command->mutable_output()->mutable_callback()->mutable_session_command();
  session_command->set_type(commands::SessionCommand::UNDO);
  return true;
}

bool Session::Undo(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::CONVERSION |
         ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  // Check the undo context
  if (!HasUndoContext()) {
    return DoNothing(command);
  }

  // Rollback the last user history.
  context_->mutable_converter()->Revert();
  ForgetLastTurnedDown();

  size_t result_size = 0;
  int32_t cursor_offset = 0;
  if (context_->output().has_result()) {
    // Check the client's capability
    if (!(context_->client_capability().text_deletion() &
          commands::Capability::DELETE_PRECEDING_TEXT)) {
      return DoNothing(command);
    }
    result_size = Util::CharsLen(context_->output().result().value());
    cursor_offset = context_->output().result().cursor_offset();
  }

  PopUndoContext();

  if (result_size > 0) {
    commands::DeletionRange* range =
        command->mutable_output()->mutable_deletion_range();
    range->set_offset(-(static_cast<int32_t>(result_size) + cursor_offset));
    range->set_length(result_size);
  }

  Output(command);
  return true;
}

bool Session::SelectCandidateInternal(commands::Command* command) {
  // If the current state is not conversion, composition or
  // precomposition, the candidate window should not be shown.  (On
  // composition or precomposition, the window is able to be shown as
  // a suggestion window).
  if (!(context_->state() & (ImeContext::CONVERSION | ImeContext::COMPOSITION |
                             ImeContext::PRECOMPOSITION))) {
    return false;
  }
  if (!command->input().has_command() || !command->input().command().has_id()) {
    LOG(WARNING) << "input.command or input.command.id did not exist.";
    return false;
  }
  if (!context_->converter().IsActive()) {
    LOG(WARNING) << "converter is not active. (no candidates)";
    return false;
  }

  command->mutable_output()->set_consumed(true);

  context_->mutable_converter()->CandidateMoveToId(
      command->input().command().id(), context_->composer());
  SetSessionState(ImeContext::CONVERSION, context_.get());

  return true;
}

bool Session::SelectCandidate(commands::Command* command) {
  if (!SelectCandidateInternal(command)) {
    return DoNothing(command);
  }
  Output(command);
  return true;
}

bool Session::CommitCandidate(commands::Command* command) {
  if (!(context_->state() & (ImeContext::COMPOSITION | ImeContext::CONVERSION |
                             ImeContext::PRECOMPOSITION))) {
    return false;
  }
  const commands::Input& input = command->input();
  if (!input.has_command() || !input.command().has_id()) {
    LOG(WARNING) << "input.command or input.command.id did not exist.";
    return false;
  }
  if (!context_->converter().IsActive()) {
    LOG(WARNING) << "converter is not active. (no candidates)";
    return false;
  }
  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  if (context_->state() & ImeContext::CONVERSION) {
    // There is a focused candidate so just select a candidate based on
    // input message and commit first segment.
    context_->mutable_converter()->CandidateMoveToId(input.command().id(),
                                                     context_->composer());
    CommitHeadToFocusedSegmentsInternal(command->input().context());
  } else {
    // No candidate is focused.
    size_t consumed_key_size = 0;
    if (context_->mutable_converter()->CommitSuggestionById(
            input.command().id(), context_->composer(),
            command->input().context(), &consumed_key_size)) {
      if (consumed_key_size < context_->composer().GetLength()) {
        // partial suggestion was committed.
        context_->mutable_composer()->DeleteRange(0, consumed_key_size);
        // Don't clear the undo context, which we've just updated.
        MoveCursorToEndInternal(command, false);
        // Copy the previous output for Undo.
        *context_->mutable_output() = command->output();
        return true;
      }
    }
  }

  if (!context_->converter().IsActive()) {
    // If the converter is not active (ie. the segment size was one.),
    // the state should be switched to precomposition.
    SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

    // Get suggestion if zero_query_suggestion is set.
    // zero_query_suggestion is usually set where the client is a
    // mobile.
    if (context_->GetRequest().zero_query_suggestion()) {
      Suggest(command->input());
    }
  }
  Output(command);
  // Copy the previous output for Undo.
  *context_->mutable_output() = command->output();
  return true;
}

bool Session::HighlightCandidate(commands::Command* command) {
  if (!SelectCandidateInternal(command)) {
    return false;
  }
  context_->mutable_converter()->SetCandidateListVisible(true);
  Output(command);
  return true;
}

bool Session::MaybeSelectCandidate(commands::Command* command) {
  if (context_->state() != ImeContext::CONVERSION) {
    return false;
  }
  // When using special romaji table (== The key event is from a virtual
  // keyboard), don't consume it as a shortcut selection operation.
  if (context_->GetRequest().special_romanji_table() !=
      commands::Request::DEFAULT_TABLE) {
    return false;
  }

  // Note that SHORTCUT_ASDFGHJKL should be handled even when the CapsLock is
  // enabled. This is why we need to normalize the key event here.
  // See b/5655743.
  commands::KeyEvent normalized_keyevent;
  KeyEventUtil::NormalizeModifiers(command->input().key(),
                                   &normalized_keyevent);

  // Check if the input character is in the shortcut.
  // TODO(komatsu): Support non ASCII characters such as Unicode and
  // special keys.
  const char shortcut = static_cast<char>(normalized_keyevent.key_code());
  return context_->mutable_converter()->CandidateMoveToShortcut(shortcut);
}

void Session::set_client_capability(commands::Capability capability) {
  *context_->mutable_client_capability() = std::move(capability);
}

void Session::set_application_info(commands::ApplicationInfo application_info) {
  *context_->mutable_application_info() = std::move(application_info);
}

const commands::ApplicationInfo& Session::application_info() const {
  return context_->application_info();
}

absl::Time Session::create_session_time() const {
  return context_->create_time();
}

absl::Time Session::last_command_time() const {
  return context_->last_command_time();
}

bool Session::InsertCharacter(commands::Command* command) {
  if (!command->input().has_key()) {
    LOG(ERROR) << "No key event: " << command->input();
    return false;
  }

  const commands::KeyEvent& key = command->input().key();

  if (key.input_style() == commands::KeyEvent::DIRECT_INPUT &&
      context_->state() == ImeContext::PRECOMPOSITION) {
    // If the key event represents a half width ascii character (ie.
    // key_code is equal to key_string), that key event is not
    // consumed and done echo back.
    // We must not call |EchoBackAndClearUndoContext| for a half-width space
    // here because it should be done in Session::TestSendKey or
    // Session::InsertSpaceHalfWidth. Note that the |key| comes from
    // Session::InsertSpaceHalfWidth and Session::InsertSpaceFullWidth is
    // different from the original key event.
    // For example, when the client sends a key command like
    //   {key.special_key(): HENKAN, key.modifier_keys(): [SHIFT]},
    // Session::InsertSpaceHalfWidth replaces it with
    //   {key.key_string(): " ", key.key_code(): ' '}
    // when you assign [Shift+HENKAN] to [InsertSpaceHalfWidth].
    // So |key.key_code() == ' '| does not always mean that the original key is
    // a space key w/o any modifier.
    // This is why we cannot call |EchoBackAndClearUndoContext| when
    // |key.key_code() == ' '|. This issue was found in b/5872031.
    if (key.key_string().size() == 1 && key.key_code() == key.key_string()[0] &&
        key.key_code() != ' ') {
      return EchoBackAndClearUndoContext(command);
    }

    context_->mutable_composer()->InsertCharacterKeyEvent(key);
    CommitCompositionDirectly(command);
    ClearUndoContext();  // UndoContext must be invalidated.
    return true;
  }

  command->mutable_output()->set_consumed(true);

  // Handle shortcut keys selecting a candidate from a list.
  if (MaybeSelectCandidate(command)) {
    Output(command);
    return true;
  }

  const std::string composition = context_->composer().GetQueryForConversion();
  bool should_commit = (context_->state() == ImeContext::CONVERSION);

  if (context_->GetRequest().space_on_alphanumeric() ==
          commands::Request::SPACE_OR_CONVERT_COMMITTING_COMPOSITION &&
      context_->state() == ImeContext::COMPOSITION &&
      // TODO(komatsu): Support FullWidthSpace
      composition.ends_with(' ')) {
    should_commit = true;
  }

  // floatype: decided before the commit below ends the conversion.
  std::string committed;
  if (should_commit) {
    CommitNotTriggeringZeroQuerySuggest(command);
    if (key.input_style() == commands::KeyEvent::DIRECT_INPUT) {
      // Do ClearUndoContext() because it is a direct input.
      ClearUndoContext();
      context_->mutable_composer()->InsertCharacterKeyEvent(key);
      CommitCompositionDirectly(command);
      return true;
    }
    committed = command->output().result().value();
  }

  if (IsSlashAfterAscii(key, committed)) {
    // As typed, in the way KeepEnglishWords() writes a word's letters.
    composer::Composer* composer = context_->mutable_composer();
    const transliteration::TransliterationType mode = composer->GetInputMode();
    const transliteration::TransliterationType comeback =
        composer->GetComebackInputMode();
    composer->SetTemporaryInputMode(transliteration::HALF_ASCII);
    composer->InsertCharacter("/");
    composer->SetInputMode(comeback);
    if (mode != comeback) {
      composer->SetTemporaryInputMode(mode);
    }
  } else {
    context_->mutable_composer()->InsertCharacterKeyEvent(key);
  }
  ClearUndoContext();
  if (context_->mutable_composer()->ShouldCommit()) {
    CommitCompositionDirectly(command);
    return true;
  }
  size_t length_to_commit = 0;
  if (context_->composer().ShouldCommitHead(&length_to_commit)) {
    return CommitHead(length_to_commit, command);
  }

  SetSessionState(ImeContext::COMPOSITION, context_.get());
  if (CanStartAutoConversion(key)) {
    return Convert(command);
  }

  // floatype: keeps the suggestions on screen while a Roman sequence is
  // unfinished.
  if (floatype::SessionFeaturesEnabled() && context_->converter().IsActive() &&
      HoldsSuggestions(key, context_->composer())) {
    Output(command);
    return true;
  }

  if (Suggest(command->input())) {
    Output(command);
    return true;
  }

  OutputComposition(command);
  return true;
}

bool Session::IsFullWidthInsertSpace(const commands::Input& input) const {
  // If IME is off, any space has to be half-width.
  if (context_->state() == ImeContext::DIRECT) {
    return false;
  }

  // In this method, we should not update the actual input mode stored in
  // the composer even when |input| has a new input mode. Note that this
  // method can be called from TestSendKey, where internal input mode is
  // is not expected to be changed. This is one of the reasons why this
  // method is a const method.
  // On the other hand, this method should behave as if the new input mode
  // in |input| was applied. For example, this method should behave as if
  // the current input mode was HALF_KATAKANA in the following situation.
  //   composer's input mode: HIRAGANA
  //   input.key().mode()   : HALF_KATAKANA
  // To achieve this, we create a temporary composer object to which the
  // new input mode will be stored when |input| has a new input mode.
  auto get_input_mode = [this, &input]() {
    const bool has_mode = (input.has_key() && input.key().has_mode());
    if (!has_mode) {
      return context_->composer().GetInputMode();
    }

    // Copy the current composer state just in case.
    composer::Composer temporary_composer = context_->composer();
    ApplyCompositionMode(input.key().mode(), &temporary_composer);
    // Refer to this temporary composer in this method.
    return temporary_composer.GetInputMode();
  };

  // Check the current config and the current input status.
  bool is_full_width = false;
  switch (context_->GetConfig().space_character_form()) {
    case config::Config::FUNDAMENTAL_INPUT_MODE: {
      const transliteration::TransliterationType input_mode = get_input_mode();
      if (transliteration::T13n::IsInHalfAsciiTypes(input_mode) ||
          transliteration::T13n::IsInHalfKatakanaTypes(input_mode)) {
        is_full_width = false;
      } else {
        is_full_width = true;
      }
      break;
    }
    case config::Config::FUNDAMENTAL_FULL_WIDTH:
      is_full_width = true;
      break;
    case config::Config::FUNDAMENTAL_HALF_WIDTH:
      is_full_width = false;
      break;
    default:
      LOG(WARNING) << "Unknown input mode";
      is_full_width = false;
      break;
  }

  return is_full_width;
}

bool Session::InsertSpace(commands::Command* command) {
  if (IsFullWidthInsertSpace(command->input())) {
    return InsertSpaceFullWidth(command);
  } else {
    return InsertSpaceHalfWidth(command);
  }
}

bool Session::InsertSpaceToggled(commands::Command* command) {
  if (IsFullWidthInsertSpace(command->input())) {
    return InsertSpaceHalfWidth(command);
  } else {
    return InsertSpaceFullWidth(command);
  }
}

bool Session::InsertSpaceHalfWidth(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::COMPOSITION |
         ImeContext::CONVERSION))) {
    return DoNothing(command);
  }

  if (context_->state() == ImeContext::PRECOMPOSITION) {
    // TODO(komatsu): This is a hack to work around the problem with
    // the inconsistency between TestSendKey and SendKey.
    if (IsPureSpaceKey(command->input().key())) {
      return EchoBackAndClearUndoContext(command);
    }
    // UndoContext will be cleared in |InsertCharacter| in this case.
  }

  const bool has_mode = command->input().key().has_mode();
  const commands::CompositionMode mode = command->input().key().mode();
  command->mutable_input()->clear_key();
  commands::KeyEvent* key_event = command->mutable_input()->mutable_key();
  key_event->set_key_code(' ');
  key_event->set_key_string(" ");
  key_event->set_input_style(commands::KeyEvent::DIRECT_INPUT);
  if (has_mode) {
    key_event->set_mode(mode);
  }
  return InsertCharacter(command);
}

bool Session::InsertSpaceFullWidth(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::COMPOSITION |
         ImeContext::CONVERSION))) {
    return DoNothing(command);
  }

  if (context_->state() == ImeContext::PRECOMPOSITION) {
    // UndoContext will be cleared in |InsertCharacter| in this case.

    // TODO(komatsu): make sure if
    // |context_->mutable_converter()->Reset()| is necessary here.
    context_->mutable_converter()->Reset();
  }

  const bool has_mode = command->input().key().has_mode();
  const commands::CompositionMode mode = command->input().key().mode();
  command->mutable_input()->clear_key();
  commands::KeyEvent* key_event = command->mutable_input()->mutable_key();
  key_event->set_key_code(' ');
  key_event->set_key_string("　");  // full-width space
  key_event->set_input_style(commands::KeyEvent::DIRECT_INPUT);
  if (has_mode) {
    key_event->set_mode(mode);
  }
  return InsertCharacter(command);
}

bool Session::TryCancelConvertReverse(commands::Command* command) {
  // If source_text is set, it usually means this session started by a
  // reverse conversion.
  if (context_->composer().source_text().empty()) {
    return false;
  }
  CommitSourceTextDirectly(command);
  return true;
}

bool Session::EditCancelOnPasswordField(commands::Command* command) {
  if (context_->composer().GetInputFieldType() != commands::Context::PASSWORD) {
    return false;
  }

  // In password mode, we should commit preedit and close keyboard
  // on Android.
  // TODO(matsuzakit): Remove this trick. b/5955618
  if (context_->composer().source_text().empty()) {
    CommitCompositionDirectly(command);
  } else {
    // Commits original text of reverse conversion.
    CommitSourceTextDirectly(command);
  }
  // Passes the key event through to MozcService.java
  // to continue the processes which are invoked by cancel operation.
  command->mutable_output()->set_consumed(false);

  return true;
}

bool Session::EditCancel(commands::Command* command) {
  if (EditCancelOnPasswordField(command)) {
    return true;
  }

  command->mutable_output()->set_consumed(true);

  TryCancelConvertReverse(command);

  SetStateToPredompositionAndCancel(context_.get());
  Output(command);
  return true;
}

bool Session::EditCancelAndIMEOff(commands::Command* command) {
  if (EditCancelOnPasswordField(command)) {
    return true;
  }

  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::COMPOSITION |
         ImeContext::CONVERSION))) {
    return DoNothing(command);
  }

  command->mutable_output()->set_consumed(true);

  TryCancelConvertReverse(command);

  ClearUndoContext();

  // Reset the context.
  context_->mutable_converter()->Reset();

  SetSessionState(ImeContext::DIRECT, context_.get());
  Output(command);
  return true;
}

bool Session::CommitInternal(commands::Command* command,
                             bool trigger_zero_query_suggest) {
  if (!(context_->state() &
        (ImeContext::COMPOSITION | ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  if (context_->state() == ImeContext::COMPOSITION) {
    context_->mutable_converter()->CommitPreedit(context_->composer(),
                                                 command->input().context());
  } else {  // ImeContext::CONVERSION
    RememberTurnedDown(std::numeric_limits<size_t>::max());
    context_->mutable_converter()->Commit(context_->composer(),
                                          command->input().context());
  }

  SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

  if (trigger_zero_query_suggest) {
    Suggest(command->input());
  }

  Output(command);
  // Copy the previous output for Undo.
  *context_->mutable_output() = command->output();
  return true;
}

bool Session::Commit(commands::Command* command) {
  return CommitInternal(command,
                        context_->GetRequest().zero_query_suggestion());
}

bool Session::CommitNotTriggeringZeroQuerySuggest(commands::Command* command) {
  return CommitInternal(command, false);
}

bool Session::CommitHead(size_t count, commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::COMPOSITION | ImeContext::PRECOMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  // TODO(yamaguchi): Support undo feature.
  ClearUndoContext();

  size_t committed_size;
  context_->mutable_converter()->CommitHead(count, context_->composer(),
                                            &committed_size);
  context_->mutable_composer()->DeleteRange(0, committed_size);
  Output(command);
  return true;
}

bool Session::CommitFirstSuggestion(commands::Command* command) {
  if (!(context_->state() == ImeContext::COMPOSITION ||
        context_->state() == ImeContext::PRECOMPOSITION)) {
    return DoNothing(command);
  }
  if (!context_->converter().IsActive()) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  constexpr int kFirstIndex = 0;
  size_t committed_key_size = 0;
  context_->mutable_converter()->CommitSuggestionByIndex(
      kFirstIndex, context_->composer(), command->input().context(),
      &committed_key_size);

  SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

  // Get suggestion if zero_query_suggestion is set.
  // zero_query_suggestion is usually set where the client is a mobile.
  if (context_->GetRequest().zero_query_suggestion()) {
    Suggest(command->input());
  }

  Output(command);
  // Copy the previous output for Undo.
  *context_->mutable_output() = command->output();
  return true;
}

bool Session::CommitSegment(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  // shortcut: committing up to the focused segment
  // (CommitHeadToFocusedSegments) tells zenz nothing; count its segments here
  // if that key comes into use.
  RememberTurnedDown(1);
  size_t size;
  context_->mutable_converter()->CommitFirstSegment(
      context_->composer(), command->input().context(), &size);
  if (size > 0) {
    // Delete the key characters of the first segment from the preedit.
    context_->mutable_composer()->DeleteRange(0, size);
    // The number of segments should be more than one.
    DCHECK_GT(context_->composer().GetLength(), 0);
  }

  if (!context_->converter().IsActive()) {
    // If the converter is not active (ie. the segment size was one.),
    // the state should be switched to precomposition.
    SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

    // Get suggestion if zero_query_suggestion is set.
    // zero_query_suggestion is usually set where the client is a mobile.
    if (context_->GetRequest().zero_query_suggestion()) {
      Suggest(command->input());
    }
  }
  Output(command);
  // Copy the previous output for Undo.
  *context_->mutable_output() = command->output();
  return true;
}

void Session::CommitHeadToFocusedSegmentsInternal(
    const commands::Context& context) {
  size_t size;
  context_->mutable_converter()->CommitHeadToFocusedSegments(
      context_->composer(), context, &size);
  if (size > 0) {
    // Delete the key characters of the first segment from the preedit.
    context_->mutable_composer()->DeleteRange(0, size);
    // The number of segments should be more than one.
    DCHECK_GT(context_->composer().GetLength(), 0);
  }
}

void Session::CommitCompositionDirectly(commands::Command* command) {
  const std::string composition = context_->composer().GetQueryForConversion();
  const std::string conversion = context_->composer().GetStringForSubmission();
  CommitStringDirectly(composition, conversion, command);
}

void Session::CommitSourceTextDirectly(commands::Command* command) {
  // We cannot use a reference since composer will be cleared on
  // CommitStringDirectly.
  absl::string_view copied_source_text = context_->composer().source_text();
  CommitStringDirectly(copied_source_text, copied_source_text, command);
}

void Session::CommitRawTextDirectly(commands::Command* command) {
  const std::string raw_text = context_->composer().GetRawString();
  CommitStringDirectly(raw_text, raw_text, command);
}

void Session::CommitStringDirectly(absl::string_view key,
                                   absl::string_view preedit,
                                   commands::Command* command) {
  if (key.empty() || preedit.empty()) {
    return;
  }

  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->Reset();

  commands::Result* result = command->mutable_output()->mutable_result();
  DCHECK(result != nullptr);
  result->set_type(commands::Result::STRING);
  result->mutable_key()->append(key);
  result->mutable_value()->append(preedit);
  SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

  // Get suggestion if zero_query_suggestion is set.
  // zero_query_suggestion is usually set where the client is a mobile.
  if (context_->GetRequest().zero_query_suggestion()) {
    Suggest(command->input());
  }

  Output(command);
}

namespace {
bool SuppressSuggestion(const commands::Input& input) {
  if (!input.has_context()) {
    return false;
  }
  if (input.context().has_suppress_suggestion() &&
      input.context().suppress_suggestion()) {
    return true;
  }
  // If the target input field is in Chrome's Omnibox or Google
  // search box, the suggest window is hidden.
  for (size_t i = 0; i < input.context().experimental_features_size(); ++i) {
    const std::string& feature = input.context().experimental_features(i);
    if (feature == "chrome_omnibox" || feature == "google_search_box") {
      return true;
    }
  }
  return false;
}
}  // namespace

bool Session::Suggest(const commands::Input& input) {
  if (SuppressSuggestion(input)) {
    return false;
  }

  // |request_suggestion| is not supposed to always ensure suppressing
  // suggestion since this field is used for performance improvement
  // by skipping interim suggestions.  However, the implementation of
  // EngineConverter::Suggest does not perform suggest
  // whenever this flag is on.  So the caller should consider whether
  // this flag should be set or not.  Because the original logic was
  // implemented in Session::InserCharacter, we check the input.type()
  // is SEND_KEY assuming SEND_KEY results InsertCharacter (in most
  // cases).
  //
  // TODO(komatsu): Move the logic into EngineConverter.
  if (input.has_request_suggestion() &&
      input.type() == commands::Input::SEND_KEY) {
    ConversionPreferences conversion_preferences =
        context_->converter().conversion_preferences();
    conversion_preferences.request_suggestion = input.request_suggestion();
    return context_->mutable_converter()->Suggest(
        context_->composer(), input.context(), conversion_preferences);
  }

  return context_->mutable_converter()->Suggest(context_->composer(),
                                                input.context());
}

bool Session::ConvertToTransliteration(
    commands::Command* command,
    const transliteration::TransliterationType type) {
  if (!(context_->state() &
        (ImeContext::CONVERSION | ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  if (!context_->mutable_converter()->ConvertToTransliteration(
          context_->composer(), type)) {
    return false;
  }
  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::ConvertToHiragana(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::HIRAGANA);
}

bool Session::ConvertToFullKatakana(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::FULL_KATAKANA);
}

bool Session::ConvertToHalfKatakana(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::HALF_KATAKANA);
}

bool Session::ConvertToFullASCII(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::FULL_ASCII);
}

bool Session::ConvertToHalfASCII(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::HALF_ASCII);
}

bool Session::SwitchKanaType(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::CONVERSION | ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  if (!context_->mutable_converter()->SwitchKanaType(context_->composer())) {
    return false;
  }
  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::DisplayAsHiragana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToHiragana(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(transliteration::HIRAGANA);
    OutputComposition(command);
    return true;
  }
}

bool Session::DisplayAsFullKatakana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToFullKatakana(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(transliteration::FULL_KATAKANA);
    OutputComposition(command);
    return true;
  }
}

bool Session::DisplayAsHalfKatakana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToHalfKatakana(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(transliteration::HALF_KATAKANA);
    OutputComposition(command);
    return true;
  }
}

bool Session::TranslateFullASCII(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToFullASCII(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(
        transliteration::T13n::ToggleFullAsciiTypes(
            context_->composer().GetOutputMode()));
    OutputComposition(command);
    return true;
  }
}

bool Session::TranslateHalfASCII(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToHalfASCII(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(
        transliteration::T13n::ToggleHalfAsciiTypes(
            context_->composer().GetOutputMode()));
    OutputComposition(command);
    return true;
  }
}

bool Session::CompositionModeHiragana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::HIRAGANA, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeFullKatakana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::FULL_KATAKANA, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeHalfKatakana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::HALF_KATAKANA, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeFullASCII(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::FULL_ASCII, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeHalfASCII(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::HALF_ASCII, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeSwitchKanaType(commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION) {
    return DoNothing(command);
  }

  command->mutable_output()->set_consumed(true);

  transliteration::TransliterationType current_type =
      context_->composer().GetInputMode();
  transliteration::TransliterationType next_type;

  switch (current_type) {
    case transliteration::HIRAGANA:
      next_type = transliteration::FULL_KATAKANA;
      break;

    case transliteration::FULL_KATAKANA:
      next_type = transliteration::HALF_KATAKANA;
      break;

    case transliteration::HALF_KATAKANA:
      next_type = transliteration::HIRAGANA;
      break;

    case transliteration::HALF_ASCII:
    case transliteration::FULL_ASCII:
      next_type = current_type;
      break;

    default:
      LOG(ERROR) << "Unknown input mode: " << current_type;
      // don't change input mode
      next_type = current_type;
      break;
  }

  // The temporary mode should not be overridden.
  SwitchInputMode(next_type, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::ConvertToHalfWidth(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::CONVERSION | ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  if (!context_->mutable_converter()->ConvertToHalfWidth(
          context_->composer())) {
    return false;
  }
  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::TranslateHalfWidth(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToHalfWidth(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    const transliteration::TransliterationType type =
        context_->composer().GetOutputMode();
    if (type == transliteration::HIRAGANA ||
        type == transliteration::FULL_KATAKANA ||
        type == transliteration::HALF_KATAKANA) {
      context_->mutable_composer()->SetOutputMode(
          transliteration::HALF_KATAKANA);
    } else if (type == transliteration::FULL_ASCII) {
      context_->mutable_composer()->SetOutputMode(transliteration::HALF_ASCII);
    } else if (type == transliteration::FULL_ASCII_UPPER) {
      context_->mutable_composer()->SetOutputMode(
          transliteration::HALF_ASCII_UPPER);
    } else if (type == transliteration::FULL_ASCII_LOWER) {
      context_->mutable_composer()->SetOutputMode(
          transliteration::HALF_ASCII_LOWER);
    } else if (type == transliteration::FULL_ASCII_CAPITALIZED) {
      context_->mutable_composer()->SetOutputMode(
          transliteration::HALF_ASCII_CAPITALIZED);
    } else {
      // transliteration::HALF_ASCII_something
      return TranslateHalfASCII(command);
    }
    OutputComposition(command);
    return true;
  }
}

bool Session::LaunchConfigDialog(commands::Command* command) {
  command->mutable_output()->set_launch_tool_mode(
      commands::Output::CONFIG_DIALOG);
  return DoNothing(command);
}

bool Session::LaunchDictionaryTool(commands::Command* command) {
  command->mutable_output()->set_launch_tool_mode(
      commands::Output::DICTIONARY_TOOL);
  return DoNothing(command);
}

bool Session::LaunchWordRegisterDialog(commands::Command* command) {
  command->mutable_output()->set_launch_tool_mode(
      commands::Output::WORD_REGISTER_DIALOG);
  return DoNothing(command);
}

bool Session::UndoOrRewind(commands::Command* command) {
  // Undo is prioritized over rewind otherwise the undo operation for
  // partial commit doesn't work (rewind always consumes the event).
  if (HasUndoContext()) {
    return Undo(command);
  }

  // Rewind if the state is in composition.
  if (!(context_->state() & ImeContext::COMPOSITION)) {
    // Mozc decoder doesn't do anything for UNDO_OR_REWIND.
    // Echo back the event to the client to give it a chance to delegate
    // undo operation to the app.
    return EchoBack(command);
  }

  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->InsertCommandCharacter(
      composer::Composer::REWIND);
  ClearUndoContext();

  // InsertCommandCharacter method updates the preedit text
  // so we need to update suggest candidates.
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::StopKeyToggling(commands::Command* command) {
  if (!(context_->state() & ImeContext::COMPOSITION)) {
    return DoNothing(command);
  }

  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->InsertCommandCharacter(
      composer::Composer::STOP_KEY_TOGGLING);
  ClearUndoContext();

  // Since the output should not be changed on STOP_KEY_TOGGLING,
  // The last output is used instead of calling the converter operations.
  Output(command);
  return true;
}

bool Session::ToggleAlphanumericMode(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->ToggleInputMode();

  OutputFromState(command);
  return true;
}

bool Session::DeleteCandidateFromHistory(commands::Command* command) {
  std::optional<int> id = std::nullopt;
  if (command->input().has_command() && command->input().command().has_id()) {
    id = command->input().command().id();
  }
  if (!context_->mutable_converter()->DeleteCandidateFromHistory(id)) {
    return DoNothing(command);
  }
  return ConvertCancel(command);
}

bool Session::Convert(commands::Command* command) {
  // The Space key has kept them already (TypeSpaceAfterEnglish()).
  if (!std::exchange(english_kept_, false)) {
    KeepEnglishWords(false);
  }
  if (MaybeConvertAsEnglish(command)) {
    return true;
  }
  command->mutable_output()->set_consumed(true);
  const std::string composition = context_->composer().GetQueryForConversion();

  // TODO(komatsu): Make a function like ConvertOrSpace.
  // Handle a space key on the ASCII composition mode.
  if (context_->state() == ImeContext::COMPOSITION &&
      (context_->composer().GetInputMode() == transliteration::HALF_ASCII ||
       context_->composer().GetInputMode() == transliteration::FULL_ASCII) &&
      command->input().key().has_special_key() &&
      command->input().key().special_key() == commands::KeyEvent::SPACE) {
    // TODO(komatsu): Consider FullWidth Space too.
    if (!composition.ends_with(' ') ||
        context_->composer().GetLength() != context_->composer().GetCursor()) {
      if (context_->GetRequest().space_on_alphanumeric() ==
          commands::Request::COMMIT) {
        // Space is committed with the composition
        context_->mutable_composer()->InsertCharacterPreedit(" ");
        // Don't push the context to the undo context here.
        // It'll be done in Commit() below.
        return Commit(command);
      } else {
        // SPACE_OR_CONVERT_KEEPING_COMPOSITION or
        // SPACE_OR_CONVERT_COMMITTING_COMPOSITION.

        // If the last character is not space, space is inserted to the
        // composition.
        command->mutable_input()->mutable_key()->set_key_code(' ');
        return InsertCharacter(command);
      }
    }

    if (!composition.empty()) {
      DCHECK_EQ(' ', composition[composition.size() - 1]);
      // Delete the last space.
      context_->mutable_composer()->Backspace();
      ClearUndoContext();
    }
  }

  if (!context_->mutable_converter()->Convert(context_->composer(),
                                              context_->client_context())) {
    LOG(ERROR) << "Conversion failed for some reasons.";
    OutputComposition(command);
    return true;
  }

  SetSessionState(ImeContext::CONVERSION, context_.get());
  PreferTypedLetters();
  Output(command);
  RequestZenz(command);
  return true;
}

namespace {

void SetCallback(commands::Command* command,
                 commands::SessionCommand::CommandType type,
                 uint32_t delay_millisec) {
  commands::Output::Callback* callback =
      command->mutable_output()->mutable_callback();
  callback->mutable_session_command()->set_type(type);
  callback->set_delay_millisec(delay_millisec);
}

}  // namespace

void Session::MaybeScheduleLiveConversion(commands::Command* command) {
  const config::Config& config = context_->GetConfig();
  const transliteration::TransliterationType mode =
      context_->composer().GetInputMode();
  if (!floatype::SessionFeaturesEnabled() || !config.use_live_conversion() ||
      context_->state() != ImeContext::COMPOSITION ||
      command->output().has_callback() ||
      mode == transliteration::FULL_ASCII ||
      context_->composer().GetLength() < config.live_conversion_min_chars()) {
    return;
  }
  // In a mode of letters there is nothing to convert, but for an English
  // word typed in kana before a capital turned the mode (camel of camelCase).
  if (mode == transliteration::HALF_ASCII &&
      (!config.mixed_english() ||
       context_->composer().GetComebackInputMode() == mode ||
       !HasEnglishWords())) {
    return;
  }
  SetCallback(command, commands::SessionCommand::LIVE_CONVERT,
              config.live_conversion_delay_millisec());
}

absl::string_view Session::KeymapStateName() const {
  const EngineConverterInterface& converter = context_->converter();
  switch (context_->state()) {
    case ImeContext::DIRECT:
      return "DirectInput";
    case ImeContext::PRECOMPOSITION:
      return converter.CheckState(EngineConverterInterface::SUGGESTION)
                 ? "ZeroQuerySuggestion"
                 : "Precomposition";
    case ImeContext::COMPOSITION:
      return converter.CheckState(EngineConverterInterface::SUGGESTION)
                 ? "Suggestion"
                 : "Composition";
    case ImeContext::CONVERSION:
      return converter.CheckState(EngineConverterInterface::PREDICTION)
                 ? "Prediction"
                 : "Conversion";
    case ImeContext::NONE:
      break;
  }
  return "";
}

bool Session::RunNamedCommand(const std::string& name,
                              commands::Command* command) {
  const keymap::KeyMapManager& keymap = context_->GetKeyMapManager();
  switch (context_->state()) {
    case ImeContext::DIRECT: {
      keymap::DirectInputState::Commands key_command;
      return keymap.ParseCommandDirect(name, &key_command) &&
             RunDirectInputCommand(key_command, command);
    }
    case ImeContext::PRECOMPOSITION: {
      keymap::PrecompositionState::Commands key_command;
      return keymap.ParseCommandPrecomposition(name, &key_command) &&
             RunPrecompositionCommand(key_command, command);
    }
    case ImeContext::COMPOSITION: {
      keymap::CompositionState::Commands key_command;
      return keymap.ParseCommandComposition(name, &key_command) &&
             RunCompositionCommand(key_command, command);
    }
    case ImeContext::CONVERSION: {
      keymap::ConversionState::Commands key_command;
      return keymap.ParseCommandConversion(name, &key_command) &&
             RunConversionCommand(key_command, command);
    }
    case ImeContext::NONE:
      break;
  }
  return false;
}

bool Session::MaybeConvertAsEnglish(commands::Command* command) {
  if (!floatype::SessionFeaturesEnabled() ||
      !context_->GetConfig().mixed_english() ||
      context_->state() != ImeContext::COMPOSITION) {
    return false;
  }
  const composer::Composer& composer = context_->composer();
  if (!floatype::LooksEnglish(composer.GetRawString(),
                              composer.GetQueryForConversion(),
                              *floatype::CurrentEnglishWords())) {
    return false;
  }
  return ConvertToHalfASCII(command);
}

namespace {

// The composition one character at a time, and the English words in it.
// With `reads_as_japanese`, words typed wholly into kana count too, and with
// `search`, those inside Japanese (EnglishSpans()).
struct EnglishView {
  explicit EnglishView(
      const composer::Composer& composer,
      const std::function<bool(absl::string_view)>& reads_as_japanese = nullptr,
      const floatype::KanaSearch* search = nullptr) {
    // Without a letter, kept or loose, and without the keys of a word, there
    // is no English word: the common case, cut short on every key.
    const std::string query_text = composer.GetQueryForConversion();
    if (search == nullptr &&
        std::none_of(query_text.begin(), query_text.end(), [](char c) {
          return absl::ascii_isalpha(static_cast<unsigned char>(c));
        })) {
      if (!reads_as_japanese) {
        return;
      }
      const std::shared_ptr<const floatype::EnglishWords> words =
          floatype::CurrentEnglishWords();
      // The keys between characters that are no letter.
      const std::string raw = composer.GetRawString();
      bool word = false;
      for (size_t begin = 0; begin < raw.size() && !word;) {
        size_t end = begin;
        while (end < raw.size() &&
               absl::ascii_isalpha(static_cast<unsigned char>(raw[end]))) {
          ++end;
        }
        word = end - begin >= 3 &&
               words->Contains(absl::string_view(raw).substr(begin, end - begin));
        begin = end + 1;
      }
      if (!word) {
        return;
      }
    }
    for (size_t i = 0; i < composer.GetLength(); ++i) {
      raws.push_back(composer.GetRawSubString(i, 1));
    }
    for (const absl::string_view c : Utf8AsChars(query_text)) {
      query.emplace_back(c);
    }
    const std::string preedit_text = composer.GetStringForPreedit();
    for (const absl::string_view c : Utf8AsChars(preedit_text)) {
      preedit.emplace_back(c);
    }
    spans = floatype::EnglishSpans(raws, query, preedit,
                                   *floatype::CurrentEnglishWords(),
                                   reads_as_japanese, search);
  }
  // Whether the word before `position` is English: one found, or letters kept
  // as typed.
  bool EnglishBefore(size_t position) const {
    if (position == 0 || position > preedit.size()) {
      return false;
    }
    const std::string& last = preedit[position - 1];
    return std::any_of(spans.begin(), spans.end(),
                       [&](const floatype::EnglishSpan& span) {
                         return span.end == position && span.retyped.empty();
                       }) ||
           (last.size() == 1 &&
            absl::ascii_isalpha(static_cast<unsigned char>(last[0])) &&
            raws[position - 1] == last);
  }
  std::vector<std::string> raws, query, preedit;
  std::vector<floatype::EnglishSpan> spans;
};

}  // namespace

floatype::ReadingLookup Session::DictionaryReadings() const {
  return [this](absl::string_view key,
                absl::FunctionRef<void(absl::string_view)> reading) {
    context_->converter().LookupWords(
        key, [&](absl::string_view word_reading, absl::string_view) {
          reading(word_reading);
        });
  };
}

std::function<bool(absl::string_view)> Session::KanaReadsAsJapanese() const {
  return [this](absl::string_view kana) {
    return floatype::ReadsAsJapanese(kana, DictionaryReadings());
  };
}

bool Session::IsSlashAfterAscii(const commands::KeyEvent& key,
                                absl::string_view committed) const {
  const config::Config& config = context_->GetConfig();
  if (!floatype::SessionFeaturesEnabled() || !config.slash_after_ascii() ||
      !key.has_key_code() || key.key_code() != '/' ||
      key.modifier_keys_size() != 0 ||
      key.input_style() != commands::KeyEvent::FOLLOW_MODE) {
    return false;
  }
  // With kana input the key comes with its kana (め), which it types.
  if (key.has_key_string() && key.key_string() != "/") {
    return false;
  }
  const composer::Composer& composer = context_->composer();
  const transliteration::TransliterationType mode = composer.GetInputMode();
  if (mode == transliteration::HALF_ASCII ||
      mode == transliteration::FULL_ASCII) {
    return false;  // / is / there already.
  }
  const auto ends_in_ascii = [](absl::string_view text) {
    return !text.empty() &&
           absl::ascii_isalnum(static_cast<unsigned char>(text.back()));
  };
  if (!composer.Empty()) {
    const size_t cursor = composer.GetCursor();
    if (cursor == 0) {
      return false;
    }
    std::vector<std::string> preedit;
    const std::string preedit_text = composer.GetStringForPreedit();
    for (const absl::string_view c : Utf8AsChars(preedit_text)) {
      preedit.emplace_back(c);
    }
    if (cursor > preedit.size()) {
      return false;
    }
    // A letter or a digit kept as typed (an English word, 2026).
    if (ends_in_ascii(preedit[cursor - 1])) {
      return true;
    }
    if (!config.mixed_english()) {
      return false;
    }
    // An English word still in kana and loose letters (てst for test), or
    // two or more loose letters (src). One alone may start a rule (z/).
    if (EnglishView(composer, KanaReadsAsJapanese()).EnglishBefore(cursor)) {
      return true;
    }
    const auto loose = [](const std::string& c) {
      const std::string half = japanese::FullWidthAsciiToHalfWidthAscii(c);
      return c.size() > 1 && half.size() == 1 &&
             absl::ascii_isalpha(static_cast<unsigned char>(half[0]));
    };
    return cursor >= 2 && loose(preedit[cursor - 1]) &&
           loose(preedit[cursor - 2]);
  }
  if (!committed.empty()) {
    return ends_in_ascii(committed);
  }
  if (const std::string& before =
          context_->client_context().preceding_text();
      !before.empty()) {
    return ends_in_ascii(before);
  }
  // Where the app does not tell (terminals), what was committed by the key
  // before, known while that commit can still be undone.
  if (!HasUndoContext()) {
    return false;
  }
  const Segments& segments = context_->converter().GetSegments();
  const size_t history = segments.history_segments_size();
  return history > 0 &&
         segments.history_segment(history - 1).candidates_size() > 0 &&
         ends_in_ascii(segments.history_segment(history - 1).candidate(0).value);
}

bool Session::FollowsJapanese(const commands::Command& command) const {
  const auto ends_in_japanese = [](absl::string_view text) {
    if (text.empty()) {
      return false;
    }
    const Utf8AsChars chars(text);
    const Util::ScriptType type = Util::GetScriptType(chars.back());
    return type == Util::HIRAGANA || type == Util::KATAKANA ||
           type == Util::KANJI;
  };
  // What this key has just committed comes before what it starts.
  if (command.output().has_result()) {
    return ends_in_japanese(command.output().result().value());
  }
  if (command.input().context().has_preceding_text()) {
    return ends_in_japanese(command.input().context().preceding_text());
  }
  // The app does not tell (a terminal, a long document): what this session
  // committed with the key before, known while that can still be undone.
  // Else the composition is taken to start afresh.
  if (!HasUndoContext()) {
    return false;
  }
  const Segments& segments = context_->converter().GetSegments();
  const size_t history = segments.history_segments_size();
  return history > 0 &&
         segments.history_segment(history - 1).candidates_size() > 0 &&
         ends_in_japanese(
             segments.history_segment(history - 1).candidate(0).value);
}

bool Session::HasEnglishWords() const {
  return !EnglishView(context_->composer(), KanaReadsAsJapanese())
              .spans.empty();
}

void Session::KeepEnglishWords(bool undoable, bool prose) {
  english_undo_.reset();
  if (!floatype::SessionFeaturesEnabled() ||
      !context_->GetConfig().mixed_english() ||
      context_->state() != ImeContext::COMPOSITION) {
    return;
  }
  composer::Composer typed = context_->composer();
  if (KeepEnglishWordsIn(context_->mutable_composer(), prose) && undoable) {
    english_undo_ = std::move(typed);
  }
}

bool Session::KeepEnglishWordsIn(composer::Composer* composer,
                                 bool prose) const {
  const floatype::KanaSearch search = {
      .lookup = DictionaryReadings(),
      .starts_word =
          [this](absl::string_view kana) {
            return context_->converter().StartsWord(kana);
          },
      .kana_of_keys =
          [composer](absl::string_view keys) {
            composer::Composer typed = *composer;
            typed.EditErase();
            for (const char c : keys) {
              typed.InsertCharacter(std::string(1, c));
            }
            std::string kana = typed.GetQueryForConversion();
            return Util::IsScriptType(kana, Util::HIRAGANA) ? kana
                                                            : std::string();
          },
      .cost =
          [this](absl::string_view text) {
            return context_->converter().ConversionCost(text);
          },
      // English being written has its word and space before it.
      .after_japanese = follows_japanese_ && !english_run_,
  };
  const EnglishView view(*composer, KanaReadsAsJapanese(), &search);
  std::vector<floatype::EnglishSpan> spans = view.spans;
  // The query writes a loose letter half width; the preedit, full width. A
  // word typed wholly into kana may go on as Japanese too (ばせ of ばせい is
  // base).
  bool may_go_on =
      !spans.empty() && spans.back().end == view.raws.size() &&
      ((view.query.back().size() == 1 &&
        absl::ascii_isalpha(
            static_cast<unsigned char>(view.query.back()[0])) &&
        view.preedit.back() != view.raws.back()) ||
       std::none_of(view.query.begin() + spans.back().begin,
                    view.query.end(), [](const std::string& c) {
                      return c.size() == 1 &&
                             absl::ascii_isalpha(
                                 static_cast<unsigned char>(c[0]));
                    }));
  // A word told by its cost is looked at again once more is typed.
  may_go_on = may_go_on ||
              std::any_of(spans.begin(), spans.end(),
                          [](const floatype::EnglishSpan& span) {
                            return span.by_cost;
                          });
  // Where English is written word by word, the keys of a word are the word.
  if (std::optional<floatype::EnglishSpan> word = floatype::WholeEnglishWord(
          view.raws, view.query, view.preedit,
          *floatype::CurrentEnglishWords(), english_run_, prose)) {
    spans = {*std::move(word)};
    may_go_on = true;
  } else if (std::optional<floatype::EnglishSpan> together =
                 floatype::WordsRunTogether(view.raws, view.query, view.preedit,
                                            *floatype::CurrentEnglishWords(),
                                            spans, KanaReadsAsJapanese())) {
    spans = {*std::move(together)};
    may_go_on = true;
  } else {
    for (floatype::EnglishSpan& name : floatype::JoinedNames(
             view.raws, view.query, view.preedit,
             *floatype::CurrentEnglishWords(), spans)) {
      // At the end it may go on in kana, as any word found there.
      may_go_on = may_go_on || name.end == view.raws.size();
      spans.push_back(std::move(name));
    }
  }
  // With no kana left, the symbols between the words stay as typed too.
  for (floatype::EnglishSpan& symbol : floatype::SymbolsAsTyped(
           view.raws, view.query, view.preedit, spans, prose)) {
    spans.push_back(std::move(symbol));
  }
  if (spans.empty()) {
    return false;  // Moving the cursor would turn the mode to the letters'.
  }
  std::sort(spans.begin(), spans.end(),
            [](const floatype::EnglishSpan& a, const floatype::EnglishSpan& b) {
              return a.begin < b.begin;
            });
  size_t cursor = composer->GetCursor();
  const transliteration::TransliterationType mode = composer->GetInputMode();
  const transliteration::TransliterationType comeback =
      composer->GetComebackInputMode();
  // From the end, so the positions before stay where they are.
  for (auto span = spans.rbegin(); span != spans.rend(); ++span) {
    const size_t removed = span->end - span->begin;
    composer->DeleteRange(span->begin, removed);
    composer->MoveCursorTo(span->begin);
    composer->SetTemporaryInputMode(transliteration::HALF_ASCII);
    for (const char c : span->word) {
      composer->InsertCharacter(std::string(1, c));
    }
    composer->SetInputMode(comeback);
    if (mode != comeback) {
      composer->SetTemporaryInputMode(mode);
    }
    for (const char c : span->retyped) {
      composer->InsertCharacter(std::string(1, c));
    }
    const size_t added = composer->GetCursor() - span->begin;
    if (cursor >= span->end) {
      cursor = cursor - removed + added;
    } else if (cursor > span->begin) {
      cursor = span->begin + added;
    }
  }
  composer->MoveCursorTo(cursor);
  // Moving the cursor made the mode follow the letters; typing goes on in
  // the mode it was in.
  composer->SetInputMode(comeback);
  if (mode != comeback) {
    composer->SetTemporaryInputMode(mode);
  }
  return may_go_on;
}

void Session::PreferTypedLetters() {
  const bool letters = context_->GetConfig().mixed_english();
  // A / in the reading was typed as it is (IsSlashAfterAscii()); the symbol
  // setting's own is the middle dot.
  const bool slashes = context_->GetConfig().slash_after_ascii();
  if (!floatype::SessionFeaturesEnabled() || (!letters && !slashes) ||
      context_->state() != ImeContext::CONVERSION) {
    return;
  }
  const auto segments =
      context_->converter().GetSegments().conversion_segments();
  // A segment of symbols that join names (. _ @ : = -), typed in a kana mode.
  const auto joins = [](absl::string_view key) {
    return !key.empty() && std::all_of(key.begin(), key.end(), [](char c) {
      return absl::StrContains("._/@:=-$#+&*%^\\|~<>", c);
    });
  };
  const auto alnum = [](char c) {
    return absl::ascii_isalnum(static_cast<unsigned char>(c));
  };
  const auto has_letter = [](absl::string_view key) {
    return std::any_of(key.begin(), key.end(), [](char c) {
      return absl::ascii_isalpha(static_cast<unsigned char>(c));
    });
  };
  std::vector<int> indices;
  bool promotes = false;
  size_t at = 0;
  for (const Segment& segment : segments) {
    const size_t i = at++;
    int& index = indices.emplace_back(0);
    if (segment.candidates_size() == 0) {
      continue;
    }
    // Symbols between letters typed as they are, or before them at the
    // start, stay as typed too: config.toml, @src/app.ts, key=value.
    if (letters && joins(segment.key())) {
      size_t before = i;
      while (before > 0 && joins(segments[before - 1].key())) {
        --before;
      }
      size_t after = i + 1;
      while (after < segments.size() && joins(segments[after].key())) {
        ++after;
      }
      if (after < segments.size() && !segments[after].key().empty() &&
          alnum(segments[after].key().front()) &&
          (before == 0 || alnum(segments[before - 1].key().back())) &&
          (has_letter(segments[after].key()) ||
           (before > 0 && has_letter(segments[before - 1].key())))) {
        for (int c = 1; c < segment.candidates_size() && index == 0 &&
                        segment.candidate(0).value != segment.key();
             ++c) {
          if (segment.candidate(c).value == segment.key()) {
            index = c;
          }
        }
        promotes = promotes || index > 0;
        continue;
      }
    }
    if (std::none_of(segment.key().begin(), segment.key().end(),
                     [&](char c) {
                       return (letters && absl::ascii_isalpha(
                                              static_cast<unsigned char>(c))) ||
                              (slashes && c == '/');
                     })) {
      continue;
    }
    const std::string want =
        japanese::FullWidthAsciiToHalfWidthAscii(segment.candidate(0).value);
    for (int c = 1; c < segment.candidates_size() && index == 0 &&
                    want != segment.candidate(0).value;
         ++c) {
      if (segment.candidate(c).value == want) {
        index = c;
      }
    }
    promotes = promotes || index > 0;
  }
  if (promotes) {
    context_->mutable_converter()->PromoteCandidates(indices, 0, 0);
  }
}

bool Session::MaybeCommitOnCharacter(commands::Command* command) {
  const std::string& characters =
      context_->GetConfig().direct_commit_characters();
  if (!floatype::SessionFeaturesEnabled() ||
      !context_->GetConfig().use_direct_commit() || characters.empty() ||
      context_->state() != ImeContext::COMPOSITION) {
    return false;
  }
  const transliteration::TransliterationType mode =
      context_->composer().GetInputMode();
  if (mode == transliteration::HALF_ASCII ||
      mode == transliteration::FULL_ASCII) {
    return false;
  }
  const std::string preedit = context_->composer().GetStringForPreedit();
  bool ends = false;
  for (const char32_t c : Utf8AsChars32(characters)) {
    std::string utf8;
    Util::CodepointToUtf8Append(c, &utf8);
    ends = ends || (preedit.size() > utf8.size() && preedit.ends_with(utf8));
  }
  if (!ends) {
    return false;
  }
  return ConvertAndCommit(command);
}

bool Session::ConvertAndCommit(commands::Command* command) {
  // What InsertCharacter showed goes; the conversion is shown afresh.
  commands::Output* output = command->mutable_output();
  output->clear_preedit();
  output->clear_candidate_window();
  output->clear_all_candidate_words();
  output->clear_callback();
  CorrectWithZenzNow(command);
  // A conversion about to commit shows no suggestions.
  live_suggestions_.reset();
  output->clear_candidate_window();
  if (context_->state() != ImeContext::CONVERSION) {
    space_after_commit_ = false;
    return true;
  }
  if (zenz_.pending()) {
    commit_after_zenz_ = true;
    return true;
  }
  // No zenz to wait for.
  output->clear_preedit();
  output->clear_candidate_window();
  output->clear_callback();
  live_conversion_ = false;
  CommitNotTriggeringZeroQuerySuggest(command);
  AppendCommittedSpace(command);
  return true;
}

void Session::AppendCommittedSpace(commands::Command* command) {
  if (!std::exchange(space_after_commit_, false) ||
      !command->output().has_result()) {
    return;
  }
  // The copy the context keeps says how much Undo deletes.
  for (commands::Output* output :
       {command->mutable_output(), context_->mutable_output()}) {
    if (output->has_result()) {
      output->mutable_result()->mutable_value()->push_back(' ');
      output->mutable_result()->mutable_key()->push_back(' ');
    }
  }
  // The space stands between the word and what is typed next, which converts
  // as the start of a phrase: に after "push " is に, not 二.
  context_->mutable_converter()->Reset();
}

bool Session::TypeSpaceAfterEnglish(bool live, commands::Command* command) {
  if (!floatype::SessionFeaturesEnabled() ||
      !context_->GetConfig().mixed_english() ||
      !IsPureSpaceKey(command->input().key())) {
    return false;
  }
  const bool shown_live = live && context_->state() == ImeContext::CONVERSION;
  if (context_->state() != ImeContext::COMPOSITION && !shown_live) {
    return false;
  }
  // In a mode of letters, Space is Mozc's: it joins the composition.
  const transliteration::TransliterationType mode =
      context_->composer().GetInputMode();
  if (mode == transliteration::HALF_ASCII ||
      mode == transliteration::FULL_ASCII ||
      context_->composer().GetCursor() != context_->composer().GetLength()) {
    return false;
  }
  // What Space would commit: the composition with its English as typed.
  composer::Composer kept = context_->composer();
  KeepEnglishWordsIn(&kept, true);
  // Where Space converts after all, the conversion takes this: with kana
  // left, or with no English, it is what the conversion would work out.
  const auto convert_this = [&]() {
    if (!shown_live) {
      *context_->mutable_composer() = std::move(kept);
      english_undo_.reset();
      english_kept_ = true;
    }
    return false;
  };
  // A letter kept as typed shows as one byte; a loose letter, full width.
  std::vector<std::string> shown;
  const std::string preedit = kept.GetStringForPreedit();
  for (const absl::string_view c : Utf8AsChars(preedit)) {
    shown.emplace_back(c);
  }
  size_t end = shown.size();
  while (end > 0 &&
         absl::StrContains(".,!?;:。、．，！？；：", shown[end - 1])) {
    --end;
  }
  const auto ascii = [](const std::string& c) {
    return c.size() == 1 && absl::ascii_isgraph(static_cast<unsigned char>(c[0]));
  };
  if (end == 0 || shown[end - 1].size() != 1 ||
      !absl::ascii_isalnum(static_cast<unsigned char>(shown[end - 1][0])) ||
      std::none_of(shown.begin(), shown.begin() + end,
                   [](const std::string& c) {
                     return c.size() == 1 &&
                            absl::ascii_isalpha(
                                static_cast<unsigned char>(c[0]));
                   })) {
    return convert_this();
  }
  const bool no_kana = std::all_of(shown.begin(), shown.begin() + end, ascii);
  // After Japanese, the marks at the end close a Japanese sentence, and
  // digits are the Japanese sentence's (fileは3 converts).
  if (!no_kana &&
      (end < shown.size() ||
       !absl::ascii_isalpha(static_cast<unsigned char>(shown[end - 1][0])))) {
    return convert_this();
  }
  command->mutable_output()->set_consumed(true);
  if (shown_live) {
    context_->mutable_converter()->Cancel();
    SetSessionState(ImeContext::COMPOSITION, context_.get());
  }
  *context_->mutable_composer() = std::move(kept);
  english_undo_.reset();
  english_run_ = true;
  space_after_commit_ = true;
  if (no_kana) {
    // As it shows: nothing to convert.
    const bool committed = Commit(command);
    AppendCommittedSpace(command);
    return committed;
  }
  return ConvertAndCommit(command);
}

commands::Preedit Session::ShownPreedit() const {
  commands::Command probe;
  OutputComposition(&probe);
  commands::Preedit preedit = probe.output().preedit();
  if (const std::string shown = ShownAfterLiveConversion(); !shown.empty()) {
    preedit.Clear();
    commands::Preedit::Segment* segment = preedit.add_segment();
    segment->set_annotation(commands::Preedit::Segment::UNDERLINE);
    segment->set_value(shown);
    segment->set_value_length(Util::CharsLen(shown));
    segment->set_key(context_->composer().GetStringForPreedit());
    preedit.set_cursor(Util::CharsLen(shown));
  }
  return preedit;
}

bool Session::ApplyAcceptedCorrection() {
  // What zenz taught comes with zenz: not where it is off or missing.
  if (!context_->GetConfig().zenz_learning() || !zenz_.available()) {
    return false;
  }
  const Segments& segments = context_->converter().GetSegments();
  const std::string reading = floatype::ConversionReading(segments);
  const std::string text = floatype::ZenzMemory::Accepted().Latest(reading);
  if (text.empty() || floatype::ZenzMemory::Stopped().Contains(reading, text)) {
    return false;
  }
  std::optional<floatype::Mapping> mapping =
      floatype::MapWithResegmentation(segments, text, ZenzResegmenter(),
                                      ZenzWords());
  return mapping.has_value() && !floatype::UsesTurnedDown(segments, *mapping) &&
         ShowZenzText(reading, text, *std::move(mapping));
}

floatype::WordLookup Session::ZenzWords() const {
  return [this](absl::string_view key,
                absl::FunctionRef<void(absl::string_view, absl::string_view)>
                    word) { context_->converter().LookupWords(key, word); };
}

floatype::Resegmenter Session::ZenzResegmenter() const {
  return [this](absl::Span<const uint8_t> sizes) {
    return context_->converter().ResizedSegments(context_->composer(), sizes);
  };
}

bool Session::ShowZenzText(absl::string_view reading, absl::string_view text,
                           floatype::Mapping mapping) {
  EngineConverterInterface* converter = context_->mutable_converter();
  // zenz's choices never teach the conversion ranking; the suggestion
  // history may learn the committed sentence.
  const uint32_t attributes = context_->GetConfig().zenz_suggestion_learning()
                                  ? converter::Attribute::NO_HISTORY_LEARNING
                                  : converter::Attribute::NO_LEARNING;
  const bool recut = mapping.segments.has_value();
  if (!recut && std::all_of(mapping.candidate_indices.begin(),
                            mapping.candidate_indices.end(),
                            [](int index) { return index == 0; })) {
    return false;
  }
  // Mozc's conversion as it is, for Space and Ctrl+Delete to bring back: its
  // segments, and its candidates in its order.
  Segments mozc = converter->GetSegments();
  if (recut) {
    // On new segments, every candidate shown is zenz's choice, the ones Mozc
    // ranks first included.
    for (size_t i = 0; i < mapping.segments->conversion_segments_size(); ++i) {
      mapping.segments->mutable_conversion_segment(i)
          ->mutable_candidate(mapping.candidate_indices[i])
          ->attributes |= attributes;
    }
    if (!converter->SetSegments(*std::move(mapping.segments))) {
      return false;
    }
  }
  if (!converter->PromoteCandidates(mapping.candidate_indices, attributes, 0)) {
    if (recut) {
      converter->SetSegments(std::move(mozc));
    }
    return false;
  }
  zenz_words_.clear();
  for (size_t i = 0; !recut && i < mapping.candidate_indices.size(); ++i) {
    const Segment& segment = converter->GetSegments().conversion_segment(i);
    if (mapping.candidate_indices[i] == 0 || segment.candidates_size() == 0) {
      continue;
    }
    const converter::Candidate& word = segment.candidate(0);
    if (!word.content_key.empty() && !word.content_value.empty()) {
      zenz_words_.push_back({i, std::string(segment.key()), word.content_key,
                             word.content_value});
    }
  }
  zenz_indices_ = std::move(mapping.candidate_indices);
  zenz_reading_ = std::string(reading);
  zenz_text_ = std::string(text);
  mozc_segments_ = std::move(mozc);
  return true;
}

void Session::StopWaitingForZenz() {
  if (!std::exchange(waiting_for_zenz_, false)) {
    return;
  }
  // Nothing of the conversion was shown: back to what the user sees.
  if (context_->state() == ImeContext::CONVERSION) {
    context_->mutable_converter()->Cancel();
    SetSessionState(ImeContext::COMPOSITION, context_.get());
  }
  live_conversion_ = false;
}

bool Session::CorrectWithZenzNow(commands::Command* command) {
  // On a conversion already shown, Mozc's or zenz's, the key says "not this".
  const bool not_this = context_->state() != ImeContext::COMPOSITION;
  if (!not_this) {
    // Converts first, the way the user has set conversions to look.
    const bool live = floatype::SessionFeaturesEnabled() &&
                      context_->GetConfig().use_live_conversion();
    if (!(live ? LiveConvert(command) : Convert(command)) ||
        context_->state() != ImeContext::CONVERSION) {
      return true;
    }
  } else {
    command->mutable_output()->set_consumed(true);
    if (live_conversion_) {
      OutputLiveConversion(command);
    } else {
      Output(command);
    }
  }
  // Asks zenz now; the callback replaces any scheduled correction.
  RequestZenz(command, not_this);
  return true;
}

void Session::RestoreMozcOrder() {
  // zenz's candidates go back where Mozc ranked them, the user's to pick and
  // learned if picked.
  if (mozc_segments_.has_value()) {
    context_->mutable_converter()->SetSegments(
        *std::exchange(mozc_segments_, std::nullopt));
  }
}

bool Session::ShowMozcAgain(bool live, commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  RestoreMozcOrder();
  live_conversion_ = live;
  if (live) {
    OutputLiveConversion(command);
  } else {
    Output(command);
  }
  SetIndicator(command);
  return true;
}

void Session::SetCandidateLook(commands::Command* command) const {
  const config::Config& config = context_->GetConfig();
  commands::Output* output = command->mutable_output();
  if (!floatype::SessionFeaturesEnabled() || !output->has_candidate_window()) {
    return;
  }
  if (config.has_candidate_style()) {
    *output->mutable_candidate_style() = config.candidate_style();
  }
  if (config.has_suggest_style()) {
    *output->mutable_suggest_style() = config.suggest_style();
  }
  output->set_candidate_kind_mark(config.candidate_kind_mark());
}

void Session::SetIndicator(commands::Command* command) const {
  const config::Config& config = context_->GetConfig();
  commands::Output* output = command->mutable_output();
  if (!floatype::SessionFeaturesEnabled() || !config.show_indicator() ||
      output->preedit().segment_size() == 0) {
    return;
  }
  commands::Output::Indicator::State state;
  if (context_->state() == ImeContext::COMPOSITION) {
    // As Mozkey: Mozc's once live conversion takes the composition, before
    // the pause too.
    state = config.use_live_conversion() &&
                    Util::CharsLen(
                        context_->composer().GetQueryForConversion()) >=
                        config.live_conversion_min_chars()
                ? commands::Output::Indicator::MOZC
                : commands::Output::Indicator::READING;
  } else if (context_->state() != ImeContext::CONVERSION) {
    return;
  } else if (zenz_.pending() || zenz_scheduled_) {
    // As Mozkey: zenz's from the moment it is booked, through the delay.
    state = commands::Output::Indicator::ZENZ_PENDING;
  } else if (!zenz_indices_.empty()) {
    state = commands::Output::Indicator::ZENZ;
  } else {
    state = commands::Output::Indicator::MOZC;
  }
  output->mutable_indicator()->set_state(state);
  *output->mutable_indicator()->mutable_style() = config.indicator_style();
  if (config.mixed_english()) {
    const composer::Composer& composer = context_->composer();
    // The word before the caret; in a conversion, the end of what is shown.
    std::string shown;
    for (const commands::Preedit::Segment& segment :
         output->preedit().segment()) {
      shown += segment.value();
    }
    output->mutable_indicator()->set_english(
        context_->state() == ImeContext::CONVERSION
            ? !shown.empty() &&
                  absl::ascii_isalpha(static_cast<unsigned char>(shown.back()))
            : EnglishView(composer, KanaReadsAsJapanese())
                  .EnglishBefore(composer.GetCursor()));
  }
}

void Session::OutputLiveConversion(commands::Command* command) {
  Output(command);
  commands::Preedit* preedit = command->mutable_output()->mutable_preedit();
  preedit->clear_highlighted_position();
  live_reading_ = context_->composer().GetStringForPreedit();
  live_value_.clear();
  for (commands::Preedit::Segment& segment : *preedit->mutable_segment()) {
    segment.set_annotation(commands::Preedit::Segment::UNDERLINE);
    live_value_ += segment.value();
  }
  if (floatype::SessionFeaturesEnabled() &&
      context_->GetConfig().live_conversion_ruby()) {
    command->mutable_output()->set_live_reading(live_reading_);
    *command->mutable_output()->mutable_ruby_style() =
        context_->GetConfig().ruby_style();
  }
  AttachLiveSuggestions(command);
}

void Session::AttachLiveSuggestions(commands::Command* command) const {
  commands::Output* output = command->mutable_output();
  output->clear_candidate_window();
  if (live_suggestions_ == nullptr) {
    return;
  }
  commands::Output shown;
  live_suggestions_->converter().FillOutput(live_suggestions_->composer(),
                                            &shown);
  if (shown.has_candidate_window()) {
    *output->mutable_candidate_window() = shown.candidate_window();
  }
}

std::string Session::ShownAfterLiveConversion() const {
  const composer::Composer& composer = context_->composer();
  if (live_reading_.empty() || context_->state() != ImeContext::COMPOSITION ||
      composer.GetCursor() != composer.GetLength()) {
    return "";
  }
  const std::string reading = composer.GetStringForPreedit();
  if (!reading.starts_with(live_reading_)) {
    return "";
  }
  return absl::StrCat(live_value_, reading.substr(live_reading_.size()));
}

bool Session::LiveConvert(commands::Command* command) {
  // floatype: while waiting for zenz, the screen keeps what it shows now.
  const uint32_t wait_millisec =
      context_->GetConfig().live_conversion_waits_for_zenz_millisec();
  const commands::Preedit shown_before =
      wait_millisec > 0 ? ShownPreedit() : commands::Preedit();
  KeepEnglishWords(true);
  if (context_->state() == ImeContext::COMPOSITION &&
      MaybeConvertAsEnglish(command)) {
    // Typing on edits the word, as after any live conversion.
    live_conversion_ = true;
    return true;
  }
  // Mozc's Suggest needs the composition state, so the suggestions shown now
  // stay with a copy of the context; the conversion takes the real one.
  std::unique_ptr<ImeContext> suggestions =
      context_->converter().CheckState(EngineConverterInterface::SUGGESTION)
          ? std::make_unique<ImeContext>(*context_)
          : nullptr;
  if (context_->state() != ImeContext::COMPOSITION ||
      !context_->mutable_converter()->Convert(context_->composer(),
                                              context_->client_context())) {
    return DoNothing(command);
  }
  live_suggestions_ = std::move(suggestions);
  command->mutable_output()->set_consumed(true);
  SetSessionState(ImeContext::CONVERSION, context_.get());
  PreferTypedLetters();
  live_conversion_ = true;
  if (wait_millisec > 0) {
    waiting_preedit_ = shown_before;
    waiting_for_zenz_ = true;
    zenz_wait_until_ = Clock::GetAbslTime() + absl::Milliseconds(wait_millisec);
    Output(command);
    *command->mutable_output()->mutable_preedit() = waiting_preedit_;
    AttachLiveSuggestions(command);
    RequestZenz(command);
    if (zenz_.pending()) {
      return true;
    }
    waiting_for_zenz_ = false;  // No zenz: Mozc's result at once.
  }
  OutputLiveConversion(command);
  // zenz_delay_millisec counts from the last key, like this conversion.
  const config::Config& config = context_->GetConfig();
  const uint32_t live_delay = config.live_conversion_delay_millisec();
  const uint32_t zenz_delay = config.zenz_delay_millisec();
  SetCallback(command, commands::SessionCommand::REQUEST_ZENZ,
              zenz_delay > live_delay ? zenz_delay - live_delay : 0);
  zenz_scheduled_ = config.use_zenz() && zenz_.available();
  return true;
}

bool Session::RequestZenzAfterLiveConversion(commands::Command* command) {
  zenz_scheduled_ = false;
  if (!live_conversion_ || context_->state() != ImeContext::CONVERSION) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  OutputLiveConversion(command);
  RequestZenz(command);
  return true;
}

void Session::WarmZenz() {
  const config::Config& config = context_->GetConfig();
  if (!floatype::SessionFeaturesEnabled() || !config.use_zenz()) {
    return;
  }
  // The settings go first: changing them later would stop a scorer just
  // started, and the restart interval would then keep zenz away.
  zenz_.Configure(config.zenz_stop_when_idle());
  zenz_.Warm();
}

void Session::RequestZenz(commands::Command* command, bool not_this) {
  const config::Config& config = context_->GetConfig();
  if (!config.use_zenz()) {
    return;
  }
  zenz_.set_timeout(absl::Milliseconds(config.zenz_timeout_millisec()));
  zenz_.set_learning(config.zenz_learning());
  zenz_.Configure(config.zenz_stop_when_idle());
  // A correction the user committed before shows at once; zenz still looks,
  // as the context may call for another. Not after "not this", which may have
  // turned it down.
  if (!not_this && ApplyAcceptedCorrection()) {
    waiting_for_zenz_ = false;
    // Output() adds to what is there: the conversion is shown afresh.
    commands::Output* output = command->mutable_output();
    output->clear_preedit();
    output->clear_candidate_window();
    output->clear_all_candidate_words();
    if (live_conversion_) {
      OutputLiveConversion(command);
    } else {
      Output(command);
    }
  }
  const Segments& segments = context_->converter().GetSegments();
  std::string left_context = context_->client_context().preceding_text();
  // Where the app does not tell what precedes the caret, as in terminals, what
  // was committed last stands in (spec section 4).
  if (left_context.empty()) {
    for (const Segment& segment : segments.history_segments()) {
      if (segment.candidates_size() > 0) {
        left_context += segment.candidate(0).value;
      }
    }
  }
  if (!zenz_.Request(segments, left_context, not_this)) {
    return;
  }
  SetCallback(
      command, commands::SessionCommand::POLL_ZENZ,
      absl::ToInt64Milliseconds(floatype::ZenzCorrector::kPollInterval));
}

bool Session::PollZenz(commands::Command* command) {
  if (context_->state() != ImeContext::CONVERSION) {
    zenz_.Cancel();
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  floatype::ZenzCorrector::PollResult result =
      zenz_.Poll(context_->converter().GetSegments(), ZenzResegmenter(),
                 ZenzWords());
  if (result.kind == floatype::ZenzCorrector::PollResult::kApply) {
    floatype::Mapping mapping{std::move(result.candidate_indices),
                              std::move(result.segments)};
    // An answer that replaces zenz's correction goes on Mozc's conversion,
    // so that Space still brings back Mozc's result. It may be Mozc's result
    // itself.
    if (!zenz_indices_.empty()) {
      zenz_indices_.clear();
      RestoreMozcOrder();
      mapping =
          floatype::MapWithResegmentation(context_->converter().GetSegments(),
                                          result.text, ZenzResegmenter(),
                                          ZenzWords())
              .value_or(floatype::Mapping());
    }
    ShowZenzText(result.reading, result.text, std::move(mapping));
  }
  // A direct commit commits once zenz has answered or given up.
  if (commit_after_zenz_ &&
      result.kind != floatype::ZenzCorrector::PollResult::kWait) {
    commit_after_zenz_ = false;
    live_conversion_ = false;
    const bool committed = CommitNotTriggeringZeroQuerySuggest(command);
    AppendCommittedSpace(command);
    return committed;
  }
  if (waiting_for_zenz_) {
    if (result.kind == floatype::ZenzCorrector::PollResult::kWait &&
        Clock::GetAbslTime() < zenz_wait_until_) {
      Output(command);
      *command->mutable_output()->mutable_preedit() = waiting_preedit_;
      AttachLiveSuggestions(command);
      SetCallback(
          command, commands::SessionCommand::POLL_ZENZ,
          absl::ToInt64Milliseconds(floatype::ZenzCorrector::kPollInterval));
      return true;
    }
    waiting_for_zenz_ = false;  // Answered or out of time: shown now.
  }
  if (live_conversion_) {
    OutputLiveConversion(command);
  } else {
    Output(command);
  }
  if (result.kind == floatype::ZenzCorrector::PollResult::kWait) {
    SetCallback(
        command, commands::SessionCommand::POLL_ZENZ,
        absl::ToInt64Milliseconds(floatype::ZenzCorrector::kPollInterval));
  }
  return true;
}

bool Session::ConvertWithoutHistory(commands::Command* command) {
  command->mutable_output()->set_consumed(true);

  ConversionPreferences preferences =
      context_->converter().conversion_preferences();
  preferences.use_history = false;
  if (!context_->mutable_converter()->Convert(
          context_->composer(), context_->client_context(), preferences)) {
    LOG(ERROR) << "Conversion failed for some reasons.";
    OutputComposition(command);
    return true;
  }

  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::CommitIfPassword(commands::Command* command) {
  if (context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
    CommitCompositionDirectly(command);
    return true;
  }
  return false;
}

bool Session::MoveCursorRight(commands::Command* command) {
  // In future, we may want to change the strategy of committing, to support
  // more flexible behavior.
  // - If the composing text has some "pending toggling character(s) at the
  //   end", we'd like to "fix" the toggling state, but not to commit.
  // - Otherwise (i.e. if there is no such character(s)), we'd like to commit
  //   (considering the use cases, probably we'd like to apply it only for
  //   alphabet mode).
  // Before supporting it, we'll need to support auto fixing by waiting
  // a period. Also, it is necessary to support displaying the current toggling
  // state (otherwise, users would be confused).
  // So, to keep users out from such confusion, we only commit if the current
  // composing mode doesn't has toggling state. Clients has the responsibility
  // to check if the keyboard has toggling state or not. Note that the server
  // should know the current table has toggling state or not. However,
  // a client may NOT want to auto committing even if the composition mode
  // doesn't have the toggling state, so the server just relies on the flag
  // passed from the client.
  // TODO(hidehiko): Support it, when it is prioritized.
  if (context_->GetRequest().crossing_edge_behavior() ==
          commands::Request::COMMIT_WITHOUT_CONSUMING &&
      context_->composer().GetLength() == context_->composer().GetCursor()) {
    Commit(command);

    // Do not consume.
    command->mutable_output()->set_consumed(false);
    return true;
  }

  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorRight();
  ClearUndoContext();
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::MoveCursorLeft(commands::Command* command) {
  if (context_->GetRequest().crossing_edge_behavior() ==
          commands::Request::COMMIT_WITHOUT_CONSUMING &&
      context_->composer().GetCursor() == 0) {
    CommitNotTriggeringZeroQuerySuggest(command);

    // Move the cursor to the beginning of the values.
    command->mutable_output()->mutable_result()->set_cursor_offset(
        -static_cast<int32_t>(
            Util::CharsLen(command->output().result().value())));

    // Do not consume.
    command->mutable_output()->set_consumed(false);
    return true;
  }

  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorLeft();
  ClearUndoContext();
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::MoveCursorToEnd(commands::Command* command) {
  return MoveCursorToEndInternal(command, true);
}

bool Session::MoveCursorToEndInternal(commands::Command* command,
                                      bool clear_undo) {
  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorToEnd();
  if (clear_undo) {
    ClearUndoContext();
  }
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::MoveCursorTo(commands::Command* command) {
  // This method moves the cursor *inside* the composition text.
  // Therefore on PRECOMPOSITION state, where there is no composition text,
  // this method shouldn't consume the event but send back it to the client.
  if (context_->state() == ImeContext::PRECOMPOSITION) {
    return EchoBack(command);
  }
  if (context_->state() != ImeContext::COMPOSITION) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorTo(
      command->input().command().cursor_position());
  ClearUndoContext();
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::MoveCursorToBeginning(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorToBeginning();
  ClearUndoContext();
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::Delete(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->Delete();
  ClearUndoContext();
  if (context_->mutable_composer()->Empty()) {
    SetStateToPredompositionAndCancel(context_.get());
    Output(command);
  } else if (Suggest(command->input())) {
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

bool Session::Backspace(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->Backspace();
  ClearUndoContext();
  if (context_->mutable_composer()->Empty()) {
    SetStateToPredompositionAndCancel(context_.get());
    Output(command);
  } else if (Suggest(command->input())) {
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

bool Session::SegmentFocusRight(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentFocusRight();
  Output(command);
  return true;
}

bool Session::SegmentFocusLast(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentFocusLast();
  Output(command);
  return true;
}

bool Session::SegmentFocusLeft(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentFocusLeft();
  Output(command);
  return true;
}

bool Session::SegmentFocusLeftEdge(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentFocusLeftEdge();
  Output(command);
  return true;
}

bool Session::SegmentWidthExpand(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentWidthExpand(context_->composer());
  Output(command);
  return true;
}

bool Session::SegmentWidthShrink(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentWidthShrink(context_->composer());
  Output(command);
  return true;
}

bool Session::ReportBug(commands::Command* command) {
  return DoNothing(command);
}

bool Session::ConvertNext(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CandidateNext(context_->composer());
  Output(command);
  return true;
}

bool Session::ConvertNextPage(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CandidateNextPage();
  Output(command);
  return true;
}

bool Session::ConvertPrev(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CandidatePrev();
  Output(command);
  return true;
}

bool Session::ConvertPrevPage(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CandidatePrevPage();
  Output(command);
  return true;
}

bool Session::ConvertCancel(commands::Command* command) {
  command->mutable_output()->set_consumed(true);

  SetSessionState(ImeContext::COMPOSITION, context_.get());
  context_->mutable_converter()->Cancel();
  if (Suggest(command->input())) {
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

bool Session::PredictAndConvert(commands::Command* command) {
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertNext(command);
  }

  command->mutable_output()->set_consumed(true);
  if (context_->mutable_converter()->Predict(context_->composer(),
                                             context_->client_context())) {
    SetSessionState(ImeContext::CONVERSION, context_.get());
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

void Session::OutputFromState(commands::Command* command) {
  if (context_->state() == ImeContext::DIRECT) {
    OutputMode(command);
    return;
  }
  Output(command);
}

void Session::Output(commands::Command* command) {
  OutputMode(command);
  context_->mutable_converter()->PopOutput(context_->composer(),
                                           command->mutable_output());
}

void Session::OutputMode(commands::Command* command) const {
  const commands::CompositionMode mode =
      ToCompositionMode(context_->composer().GetInputMode());
  const commands::CompositionMode comeback_mode =
      ToCompositionMode(context_->composer().GetComebackInputMode());

  commands::Output* output = command->mutable_output();
  commands::Status* status = output->mutable_status();
  if (context_->state() == ImeContext::DIRECT) {
    output->set_mode(commands::DIRECT);
    status->set_activated(false);
  } else {
    output->set_mode(mode);
    status->set_activated(true);
  }
  status->set_mode(mode);
  status->set_comeback_mode(comeback_mode);
}

void Session::OutputComposition(commands::Command* command) const {
  OutputMode(command);
  context_->converter().FillPreedit(
      context_->composer(), command->mutable_output()->mutable_preedit());
}

void Session::OutputKey(commands::Command* command) const {
  OutputMode(command);
  commands::KeyEvent* key = command->mutable_output()->mutable_key();
  *key = command->input().key();
}

namespace {
// return
// ((key_code == static_cast<uint32_t>('.') ||
//       key_string == "." || key_string == "．" ||
//   key_string == "。" || key_string == "｡") &&
//  (config.auto_conversion_key() &
//   config::Config::AUTO_CONVERSION_KUTEN)) ||
// ((key_code == static_cast<uint32_t>(',') ||
//       key_string == "," || key_string == "，" ||
//   key_string == "、" || key_string == "､") &&
//  (config.auto_conversion_key() &
//   config::Config::AUTO_CONVERSION_TOUTEN)) ||
// ((key_code == static_cast<uint32_t>('?') ||
//   key_string == "?" || key_string == "？") &&
//  (config.auto_conversion_key() &
//   config::Config::AUTO_CONVERSION_QUESTION_MARK)) ||
// ((key_code == static_cast<uint32_t>('!') ||
//   key_string == "!" || key_string == "！") &&
//  (config.auto_conversion_key() &
//   config::Config::AUTO_CONVERSION_EXCLAMATION_MARK));
bool IsValidKey(const config::Config& config, const uint32_t key_code,
                absl::string_view key_string) {
  return (((key_code == static_cast<uint32_t>('.') && key_string.empty()) ||
           key_string == "." || key_string == "．" || key_string == "。" ||
           key_string == "｡") &&
          (config.auto_conversion_key() &
           config::Config::AUTO_CONVERSION_KUTEN)) ||
         (((key_code == static_cast<uint32_t>(',') && key_string.empty()) ||
           key_string == "," || key_string == "，" || key_string == "、" ||
           key_string == "､") &&
          (config.auto_conversion_key() &
           config::Config::AUTO_CONVERSION_TOUTEN)) ||
         (((key_code == static_cast<uint32_t>('?') && key_string.empty()) ||
           key_string == "?" || key_string == "？") &&
          (config.auto_conversion_key() &
           config::Config::AUTO_CONVERSION_QUESTION_MARK)) ||
         (((key_code == static_cast<uint32_t>('!') && key_string.empty()) ||
           key_string == "!" || key_string == "！") &&
          (config.auto_conversion_key() &
           config::Config::AUTO_CONVERSION_EXCLAMATION_MARK));
}
}  // namespace

bool Session::CanStartAutoConversion(
    const commands::KeyEvent& key_event) const {
  if (!context_->GetConfig().use_auto_conversion()) {
    return false;
  }

  // Disable if the input comes from non-standard user keyboards, like numpad.
  // http://b/issue?id=2932067
  if (key_event.input_style() != commands::KeyEvent::FOLLOW_MODE) {
    return false;
  }

  // This is a tentative workaround for the bug http://b/issue?id=2932028
  // When user types <Shift Down>O<Shift Up>racle<Shift Down>!<Shift Up>,
  // The final "!" must be half-width, however, due to the limitation
  // of converter interface, we don't have a good way to change it halfwidth, as
  // the default preference of "!" is fullwidth. Basically, the converter is
  // not composition-mode-aware.
  // We simply disable the auto conversion feature if the mode is ASCII.
  // We conclude that disabling this feature is better in this situation.
  // TODO(taku): fix the behavior. Converter module needs to be fixed.
  if (key_event.mode() == commands::HALF_ASCII ||
      key_event.mode() == commands::FULL_ASCII) {
    return false;
  }

  // We should NOT check key_string. http://b/issue?id=3217992

  // Auto conversion is not triggered if the composition is empty or
  // only one character, or the cursor is not in the end of the
  // composition.
  const size_t length = context_->composer().GetLength();
  if (length <= 1 || length != context_->composer().GetCursor()) {
    return false;
  }

  const uint32_t key_code = key_event.key_code();

  const std::string preedit = context_->composer().GetStringForPreedit();
  const absl::string_view last_char =
      Util::Utf8SubString(preedit, length - 1, 1);
  if (last_char.empty()) {
    return false;
  }

  // Check last character as user may change romaji table,
  // For instance, if user assigns "." as "foo", we don't
  // want to invoke auto_conversion.
  if (!IsValidKey(context_->GetConfig(), key_code, last_char)) {
    return false;
  }

  // Check the previous character of last_character.
  // when |last_prev_char| is number, we don't invoke auto_conversion
  // if the same invoke key is repeated, do not conversion.
  // http://b/issue?id=2932118
  const absl::string_view last_prev_char =
      Util::Utf8SubString(preedit, length - 2, 1);
  if (last_prev_char.empty() || last_prev_char == last_char ||
      Util::NUMBER == Util::GetScriptType(last_prev_char)) {
    return false;
  }

  return true;
}

void Session::UpdateTime() {
  context_->set_last_command_time(Clock::GetAbslTime());
}

void Session::TransformInput(commands::Input* input) {
  if (input->has_key()) {
    context_->key_event_transformer().TransformKeyEvent(input->mutable_key());
  }
}

bool Session::SwitchInputFieldType(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->SetInputFieldType(
      command->input().context().input_field_type());
  Output(command);
  return true;
}

bool Session::HandleIndirectImeOnOff(commands::Command* command) {
  const commands::KeyEvent& key = command->input().key();
  if (!key.has_activated()) {
    return true;
  }
  const ImeContext::State state = context_->state();
  if (state == ImeContext::DIRECT && key.activated()) {
    // Indirect IME On found.
    commands::Command on_command;
    on_command = *command;
    if (!IMEOn(&on_command)) {
      return false;
    }
  } else if (state != ImeContext::DIRECT && !key.activated()) {
    // Indirect IME Off found.
    commands::Command off_command;
    off_command = *command;
    if (!IMEOff(&off_command)) {
      return false;
    }
  }
  return true;
}

bool Session::ImeAction(commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION ||
      context_->composer().GetInputFieldType() != commands::Context::NORMAL) {
    return false;
  }

  // ImeAction is triggered when the mobile-specific IME action buttons such as
  // Search, Go, or Next, are tapped. After a user finishes an IME session, they
  // might still perform manual edits using the Backspace key. To sync these
  // edits with the converter, we call CommitContext. Typical use case is the
  // partial-revert on user history training.
  context_->mutable_converter()->CommitContext(context_->composer(),
                                               command->input().context());

  return true;
}

bool Session::CommitRawText(commands::Command* command) {
  if (context_->composer().GetLength() == 0) {
    return false;
  }
  CommitRawTextDirectly(command);
  return true;
}

// TODO(komatsu): delete this function.
composer::Composer* Session::get_internal_composer_only_for_unittest() {
  return context_->mutable_composer();
}

const ImeContext& Session::context() const { return *context_; }

}  // namespace session
}  // namespace mozc
