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

#ifndef MOZC_SESSION_SESSION_H_
#define MOZC_SESSION_SESSION_H_

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "composer/composer.h"
#include "composer/table.h"
#include "converter/segments.h"
#include "engine/engine_interface.h"
#include "floatype/english/english_words.h"
#include "floatype/zenz/zenz_corrector.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/ime_context.h"
#include "session/keymap.h"
#include "transliteration/transliteration.h"

namespace mozc {
namespace session {

class Session {
 public:
  explicit Session(const EngineInterface& engine);
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  bool SendKey(mozc::commands::Command* command);

  // Check if the input key event will be consumed by the session.
  bool TestSendKey(mozc::commands::Command* command);

  // Perform the SEND_COMMAND command defined commands.proto.
  bool SendCommand(mozc::commands::Command* command);

  // Turn on IME. Do nothing (but the keyevent is consumed) when IME is already
  // turned on.
  bool IMEOn(mozc::commands::Command* command);

  // Turn off IME. Do nothing (but the keyevent is consumed) when IME is already
  // turned off.
  bool IMEOff(mozc::commands::Command* command);

  // Unlike IMEOn/IMEOff, these commands 1) can update composition mode, and
  // 2) are functional even when IME is already turned on/off.
  // TODO(team): Merge these into IMEOn/Off once b/10250883 is fixed.
  bool MakeSureIMEOn(mozc::commands::Command* command);
  bool MakeSureIMEOff(mozc::commands::Command* command);

  bool EchoBack(mozc::commands::Command* command);
  bool EchoBackAndClearUndoContext(mozc::commands::Command* command);
  bool DoNothing(mozc::commands::Command* command);

  // Tries deleting the specified candidate from the user prediction history.
  // The candidate is determined by command.input.command.id, or the current
  // focused candidate if that ....command.id is not specified. If
  // that candidate, as a key value pair, doesn't exist in the user history,
  // nothing happens. Regardless of the result of internal history deletion,
  // invoking this method has the same effect as ConvertCancel() from the
  // viewpoint of session, meaning that the session state gets back to
  // composition.
  bool DeleteCandidateFromHistory(mozc::commands::Command* command);

  // Resets the composer and clear conversion segments.
  // History segments will not be cleared.
  // Therefore if a user commits "風"(かぜ) and Revert method is called,
  // preedit "ひいた"  will be converted into "邪引いた".
  bool Revert(mozc::commands::Command* command);
  // Reset the composer and clear all the segments (including history segments).
  // Therefore preedit "ひいた"  will *not* be converted into "邪引いた"
  // on the situation described above.
  bool ResetContext(mozc::commands::Command* command);

  // Returns the current status such as a composition string, input mode, etc.
  bool GetStatus(mozc::commands::Command* command);

  // Fills Output::Callback with the CONVERT_REVERSE SessionCommand to
  // ask the client to send back the SessionCommand to the server.
  // This function is called when the key event representing the
  // ConvertReverse keybinding is called.
  bool RequestConvertReverse(mozc::commands::Command* command);

  // Begins reverse conversion for the given session.  This function
  // is called when the CONVERT_REVERSE SessionCommand is called.
  bool ConvertReverse(mozc::commands::Command* command);

  // Fills Output::Callback with the Undo SessionCommand to ask the
  // client to send back the SessionCommand to the server.
  // This function is called when the key event representing the
  // Undo keybinding is called.
  bool RequestUndo(mozc::commands::Command* command);

  // Undos the commitment.  This function is called when the
  // UNDO SessionCommand is called.
  bool Undo(mozc::commands::Command* command);

  bool InsertSpace(mozc::commands::Command* command);
  bool InsertSpaceToggled(mozc::commands::Command* command);
  bool InsertSpaceHalfWidth(mozc::commands::Command* command);
  bool InsertSpaceFullWidth(mozc::commands::Command* command);
  bool InsertCharacter(mozc::commands::Command* command);
  bool UpdateComposition(mozc::commands::Command* command);
  bool UpdateCompositionInternal(mozc::commands::Command* command);
  bool Delete(mozc::commands::Command* command);
  bool Backspace(mozc::commands::Command* command);
  bool EditCancel(mozc::commands::Command* command);
  bool EditCancelAndIMEOff(mozc::commands::Command* command);

  bool MoveCursorRight(mozc::commands::Command* command);
  bool MoveCursorLeft(mozc::commands::Command* command);
  bool MoveCursorToEnd(mozc::commands::Command* command);
  bool MoveCursorToBeginning(mozc::commands::Command* command);
  bool MoveCursorTo(mozc::commands::Command* command);
  bool Convert(mozc::commands::Command* command);
  // Starts conversion not using user history.  This is used for debugging.
  bool ConvertWithoutHistory(mozc::commands::Command* command);
  bool ConvertNext(mozc::commands::Command* command);
  bool ConvertPrev(mozc::commands::Command* command);
  // Shows the next page of candidates.
  bool ConvertNextPage(mozc::commands::Command* command);
  // Shows the previous page of candidates.
  bool ConvertPrevPage(mozc::commands::Command* command);
  bool ConvertCancel(mozc::commands::Command* command);
  bool PredictAndConvert(mozc::commands::Command* command);
  // Note: Commit() also triggers zero query suggestion.
  // TODO(team): Rename this method to CommitWithZeroQuerySuggest.
  bool Commit(mozc::commands::Command* command);
  bool CommitNotTriggeringZeroQuerySuggest(commands::Command* command);
  bool CommitFirstSuggestion(mozc::commands::Command* command);
  // Select a candidate located by input.command.id and commit.
  bool CommitCandidate(mozc::commands::Command* command);

  // Commits only the first segment.
  bool CommitSegment(mozc::commands::Command* command);
  // Commits some characters at the head of the preedit.
  bool CommitHead(size_t count, mozc::commands::Command* command);
  // Commits preedit if in password mode.
  bool CommitIfPassword(mozc::commands::Command* command);

  bool SegmentFocusRight(mozc::commands::Command* command);
  bool SegmentFocusLeft(mozc::commands::Command* command);
  bool SegmentFocusLast(mozc::commands::Command* command);
  bool SegmentFocusLeftEdge(mozc::commands::Command* command);
  bool SegmentWidthExpand(mozc::commands::Command* command);
  bool SegmentWidthShrink(mozc::commands::Command* command);

  // Selects the transliteration candidate.  If the current state is
  // composition, candidates will be generated with only translitaration
  // candidates.
  bool ConvertToHiragana(mozc::commands::Command* command);
  bool ConvertToFullKatakana(mozc::commands::Command* command);
  bool ConvertToHalfKatakana(mozc::commands::Command* command);
  bool ConvertToFullASCII(mozc::commands::Command* command);
  bool ConvertToHalfASCII(mozc::commands::Command* command);
  bool ConvertToHalfWidth(mozc::commands::Command* command);
  // Switch the composition to Hiragana, full-width Katakana or
  // half-width Katakana by rotation.
  bool SwitchKanaType(mozc::commands::Command* command);

  // Select the transliteration candidate if the current status is
  // conversion.  This is same with the above ConvertTo functions.  If
  // the current state is composition, the display mode is changed to the
  // transliteration and the composition state still remains.
  bool DisplayAsHiragana(mozc::commands::Command* command);
  bool DisplayAsFullKatakana(mozc::commands::Command* command);
  bool DisplayAsHalfKatakana(mozc::commands::Command* command);
  bool TranslateFullASCII(mozc::commands::Command* command);
  bool TranslateHalfASCII(mozc::commands::Command* command);
  bool TranslateHalfWidth(mozc::commands::Command* command);
  bool ToggleAlphanumericMode(mozc::commands::Command* command);

  // Switch the composition mode.
  bool CompositionModeHiragana(mozc::commands::Command* command);
  bool CompositionModeFullKatakana(mozc::commands::Command* command);
  bool CompositionModeHalfKatakana(mozc::commands::Command* command);
  bool CompositionModeFullASCII(mozc::commands::Command* command);
  bool CompositionModeHalfASCII(mozc::commands::Command* command);
  bool CompositionModeSwitchKanaType(mozc::commands::Command* command);

  // Specify the input field type.
  bool SwitchInputFieldType(mozc::commands::Command* command);

  // Let client launch config dialog
  bool LaunchConfigDialog(mozc::commands::Command* command);

  // Let client launch dictionary tool
  bool LaunchDictionaryTool(mozc::commands::Command* command);

  // Let client launch word register dialog
  bool LaunchWordRegisterDialog(mozc::commands::Command* command);

  // Undo if pre-composition is empty. Rewind KANA cycle otherwise.
  bool UndoOrRewind(mozc::commands::Command* command);

  // Stops key toggling in the composer.
  bool StopKeyToggling(mozc::commands::Command* command);

  // Special IME action key on mobile, such as Search, Send, Go, and Next
  // keys.
  bool ImeAction(mozc::commands::Command* command);

  bool ReportBug(mozc::commands::Command* command);

  void SetConfig(std::shared_ptr<const mozc::config::Config> config);

  void SetKeyMapManager(
      std::shared_ptr<const mozc::keymap::KeyMapManager> key_map_manager);

  void SetRequest(std::shared_ptr<const mozc::commands::Request> request);

  void SetConfig(mozc::config::Config config) {
    SetConfig(std::make_shared<const config::Config>(std::move(config)));
  }

  void SetRequest(mozc::commands::Request request) {
    SetRequest(
        std::make_shared<const mozc::commands::Request>(std::move(request)));
  }

  void SetTable(std::shared_ptr<const mozc::composer::Table> table);

  // Set client capability for this session.  Used by unittest.
  void set_client_capability(mozc::commands::Capability capability);

  // Set application information for this session.
  void set_application_info(mozc::commands::ApplicationInfo application_info);

  // Get application information
  const mozc::commands::ApplicationInfo& application_info() const;

  // Return the time when this instance was created.
  absl::Time create_session_time() const;

  // return 0 (default value) if no command is executed in this session.
  absl::Time last_command_time() const;

  // TODO(komatsu): delete this function.
  // For unittest only
  mozc::composer::Composer* get_internal_composer_only_for_unittest();

  const ImeContext& context() const;

 private:
  friend class SessionTestPeer;

  std::unique_ptr<ImeContext> context_;

  // floatype: corrects each conversion once with zenz.
  floatype::ZenzCorrector zenz_{floatype::ZenzCorrector::DefaultClient()};
  // Sends the new conversion to zenz and asks the client to call back. With
  // `not_this`, the user turned down the conversion on screen (see
  // ZenzCorrector::Request()).
  void RequestZenz(commands::Command* command, bool not_this = false);
  // After a typed character: starts the scorer if it ended after idling, so
  // it has the model loaded when the user pauses.
  void WarmZenz();
  // A live conversion booked zenz for after the delay; the indicator says
  // zenz's from then on, as Mozkey's does.
  bool zenz_scheduled_ = false;
  // Handles the callback: shows zenz's correction or asks to be called again.
  bool PollZenz(commands::Command* command);
  // floatype live conversion: true while the conversion on screen is a live
  // one that the next typed character turns back into the reading.
  bool live_conversion_ = false;
  void MaybeScheduleLiveConversion(commands::Command* command);
  bool LiveConvert(commands::Command* command);
  bool RequestZenzAfterLiveConversion(commands::Command* command);
  // The candidates zenz moved to the top while its correction is on screen;
  // empty otherwise.
  std::vector<int> zenz_indices_;
  // Whether the candidates were opened on that correction (the first Space).
  bool zenz_listed_ = false;
  // The reading and zenz's text of the correction on screen, for Ctrl+Delete.
  std::string zenz_reading_;
  std::string zenz_text_;
  // Mozc's conversion as it was before zenz's correction on screen segmented
  // it again, for Space to bring back; nullopt when the correction kept
  // Mozc's segments. Set with each correction shown.
  std::optional<converter::Segments> mozc_segments_;
  // Converts the conversion again with other segments, for zenz's answers
  // that Mozc's segments cannot write.
  floatype::Resegmenter ZenzResegmenter() const;
  // The dictionary's words, to check zenz's answers against.
  floatype::WordLookup ZenzWords() const;
  // Shows zenz's `text` for `reading` as `mapping` says. Returns whether the
  // conversion on screen changed.
  bool ShowZenzText(absl::string_view reading, absl::string_view text,
                    floatype::Mapping mapping);
  // A live conversion waiting for zenz before it shows (the setting
  // live_conversion_waits_for_zenz_millisec): what stays on screen meanwhile
  // and until when.
  bool waiting_for_zenz_ = false;
  absl::Time zenz_wait_until_;
  commands::Preedit waiting_preedit_;
  // What the composition looks like on screen now.
  commands::Preedit ShownPreedit() const;
  // A key or command during the wait meets the composition the user sees.
  void StopWaitingForZenz();
  // The settings use_direct_commit and direct_commit_characters: a
  // conversion that commits as soon as zenz answers.
  bool commit_after_zenz_ = false;
  // After a typed character: when it is one of the direct commit characters,
  // converts, asks zenz, and commits once it answers. Returns whether it did.
  bool MaybeCommitOnCharacter(commands::Command* command);
  // Converts the composition, asks zenz, and commits once it answers or a
  // key comes first.
  bool ConvertAndCommit(commands::Command* command);
  // Mixed English input: the Space key after an English word commits the
  // composition and types a half-width space, so English is written word by
  // word with no mode switch (time is money). The word may end Japanese
  // (これはpull + Space), which converts first, and may be followed by . , !
  // ? ; or : where the composition holds no kana (thank you.). `live` says a
  // live conversion was on screen. Returns whether the key did so.
  bool TypeSpaceAfterEnglish(bool live, commands::Command* command);
  // The space that follows what ConvertAndCommit() commits for
  // TypeSpaceAfterEnglish(), and adding it to the text just committed.
  bool space_after_commit_ = false;
  void AppendCommittedSpace(commands::Command* command);
  // floatype: the readings of the dictionary's words that start a key
  // (EngineConverter's LookupWords()).
  floatype::ReadingLookup DictionaryReadings() const;
  // floatype: whether a run of kana reads as Japanese by the dictionary's
  // words (floatype::ReadsAsJapanese()), for words typed wholly into kana.
  std::function<bool(absl::string_view)> KanaReadsAsJapanese() const;
  // floatype: whether `key` is a / to type as it is, because a letter or a
  // digit comes right before it: in the composition, in the text the app says
  // is before the caret, or else at the end of what was committed by the
  // key before (`committed`, or the last commit while it can still be undone).
  bool IsSlashAfterAscii(const commands::KeyEvent& key,
                         absl::string_view committed) const;
  // Mixed English input: converts the composition to its letters when they
  // were an English word. Returns whether it did.
  bool MaybeConvertAsEnglish(commands::Command* command);
  // Mixed English input: turns each English word inside the composition back
  // into the letters typed, so it converts, and stays, as typed; words typed
  // wholly into kana inside Japanese too (floatype::KanaSearch). With
  // `undoable` (a live conversion), a word ending the composition in loose
  // letters can be undone by the next key (see english_undo_). Where no kana
  // are left, the symbols between the words are typed again as they were
  // typed (floatype::SymbolsAsTyped(), with its `prose`).
  void KeepEnglishWords(bool undoable, bool prose = false);
  // Whether the composition holds an English word still to turn into letters.
  bool HasEnglishWords() const;
  // Whether what `command`'s key starts follows Japanese text: what the key
  // has just committed, else the text the app reports before the caret, else
  // what this session committed with the key before. Where none is known
  // (the first key in a terminal, or after the caret moved there), it starts
  // afresh.
  bool FollowsJapanese(const commands::Command& command) const;
  // FollowsJapanese() at the key that started the composition: its first
  // kana may go on with that text (KanaSearch's after_japanese).
  bool follows_japanese_ = false;
  // The Space key kept the English words of the composition on its way to
  // converting it, so Convert() does not work them out again.
  bool english_kept_ = false;
  // The same in `composer`, which need not be the session's. Returns whether
  // the word it kept at the end may go on in kana.
  bool KeepEnglishWordsIn(composer::Composer* composer, bool prose) const;
  // Mixed English input: English is being written word by word, since the
  // Space key committed a word and typed a space (TypeSpaceAfterEnglish()).
  // While the next word is typed, its keys are that word whatever kana they
  // type (you, are, a). Any other commit, and any key or command that leaves
  // nothing composed, ends the run; so does a caret the app reports
  // elsewhere than after the word and its space (a click).
  bool english_run_ = false;
  // The composition as typed, before a live conversion kept a word that ends
  // it in loose letters: the user may have paused inside a syllable
  // (きょうは + s reads "has", but may go on to きょうはしる). The next letter
  // key brings it back, and after a word typed wholly into kana the ー key
  // too; any other key or command drops it.
  std::optional<composer::Composer> english_undo_;
  // Mixed English input: after a conversion, shows letters typed in a kana mode
  // half width, as typed, where Mozc offers them so.
  void PreferTypedLetters();
  // Shows the correction the user committed before for this conversion's
  // reading, if any. Returns whether it did.
  bool ApplyAcceptedCorrection();
  // The manual zenz key: converts if needed and asks zenz now. On a
  // conversion already shown, it means "not this": zenz answers something
  // else.
  bool CorrectWithZenzNow(commands::Command* command);
  // Space while zenz's correction is shown: Mozc's result again.
  bool ShowMozcAgain(absl::Span<const int> zenz_indices, bool live,
                     commands::Command* command);
  // Puts Mozc's candidates back on top where zenz's correction moved others,
  // or Mozc's segments back where it segmented the conversion again.
  void RestoreMozcOrder(absl::Span<const int> zenz_indices);
  // Tells the client what the indicator next to the composition shows.
  void SetIndicator(commands::Command* command) const;
  // Tells the renderer the candidate window's font scale and colors, when
  // they are not Mozc's.
  void SetCandidateLook(commands::Command* command) const;
  // Output() with every segment underlined, as a live conversion looks.
  void OutputLiveConversion(commands::Command* command);
  // The reading and the text of the last live conversion. Typing on keeps that
  // text on screen and shows only what was typed after it as the reading.
  std::string live_reading_;
  std::string live_value_;
  // The context as it was before the live conversion, showing the suggestions
  // that stay on screen with it until the next key or command; null if none.
  std::unique_ptr<ImeContext> live_suggestions_;
  // Replaces the output's candidate window with those suggestions.
  void AttachLiveSuggestions(commands::Command* command) const;
  // The text on screen while composing after a live conversion, or "".
  std::string ShownAfterLiveConversion() const;

  // Undo stack. *begin is the oldest, and *back is the newest.
  std::deque<std::unique_ptr<ImeContext>> undo_contexts_;

  std::unique_ptr<ImeContext> CreateContext(
      const EngineInterface& engine) const;

  void PushUndoContext();
  void PopUndoContext();
  // Clear the undo context.
  // This should be called when the composer's preedit or cursor position
  // is updated by non-undo related operations. This achieves intuitive
  // behavior, which clears the undo context on the user's edit operation.
  void ClearUndoContext();
  bool HasUndoContext() const;
  // Set command.output.status.undo_available to true if HasUndoContext()==true.
  // Otherwise no-op.
  // Some code treats empty Status and default Status differently so
  // we don't want to create a new Status instance if not required.
  void MaybeSetUndoStatus(commands::Command* command) const;

  // Return true if full width space is preferred in the given new input
  // state than half width space. When |input| does not have new input mode,
  // the current mode will be considered.
  bool IsFullWidthInsertSpace(const mozc::commands::Input& input) const;

  bool EditCancelOnPasswordField(mozc::commands::Command* command);

  bool ConvertToTransliteration(
      mozc::commands::Command* command,
      mozc::transliteration::TransliterationType type);

  // Select a candidate located by input.command.id.  This command
  // would not be used from SendKey but used from SendCommand because
  // it requires the argument id.
  bool SelectCandidate(mozc::commands::Command* command);

  // Calls EngineConverter::ConmmitHeadToFocusedSegments()
  // and deletes characters from the composer.
  void CommitHeadToFocusedSegmentsInternal(const commands::Context& context);

  // Commits without EngineConverter.
  void CommitCompositionDirectly(commands::Command* command);
  void CommitSourceTextDirectly(commands::Command* command);
  void CommitRawTextDirectly(commands::Command* command);
  void CommitStringDirectly(absl::string_view key, absl::string_view preedit,
                            commands::Command* command);
  bool CommitInternal(commands::Command* command,
                      bool trigger_zero_query_suggest);

  // Calls EngineConverter::Suggest if the condition is applicable to
  // call it.  True is returned when EngineConverter::Suggest is
  // called and results exist.  False is returned when
  // EngineConverter::Suggest is not called or no results exist.
  bool Suggest(const mozc::commands::Input& input);

  // Commands like EditCancel should restore the original string used for
  // the reverse conversion without any modification.
  // Returns true if the |source_text| is committed to cancel reconversion.
  // Returns false if this function has nothing to do.
  bool TryCancelConvertReverse(mozc::commands::Command* command);

  // Set the focus to the candidate located by input.command.id.  This
  // command would not be used from SendKey but used from SendCommand
  // because it requires the argument id.  The difference from
  // SelectCandidate is that HighlightCandidate does not close the
  // candidate window while SelectCandidate closes the candidate
  // window.
  bool HighlightCandidate(mozc::commands::Command* command);

  // The internal implementation of both SelectCandidate and HighlightCandidate.
  bool SelectCandidateInternal(mozc::commands::Command* command);

  // If the command is a shortcut to select a candidate from a list,
  // Process it and return true, otherwise return false.
  bool MaybeSelectCandidate(mozc::commands::Command* command);

  // Fill command's output according to the current state.
  void OutputFromState(mozc::commands::Command* command);
  void Output(mozc::commands::Command* command);
  void OutputMode(mozc::commands::Command* command) const;
  void OutputComposition(mozc::commands::Command* command) const;
  void OutputKey(mozc::commands::Command* command) const;

  bool SendKeyDirectInputState(mozc::commands::Command* command);
  bool SendKeyPrecompositionState(mozc::commands::Command* command);
  bool SendKeyCompositionState(mozc::commands::Command* command);
  bool SendKeyConversionState(mozc::commands::Command* command);
  // floatype: run one command as if its key had been bound to it.
  bool RunDirectInputCommand(keymap::DirectInputState::Commands key_command,
                             mozc::commands::Command* command);
  bool RunPrecompositionCommand(
      keymap::PrecompositionState::Commands key_command,
      mozc::commands::Command* command);
  bool RunCompositionCommand(keymap::CompositionState::Commands key_command,
                             mozc::commands::Command* command);
  bool RunConversionCommand(keymap::ConversionState::Commands key_command,
                            mozc::commands::Command* command);
  // floatype: runs a command named as in the keymap table in the state the
  // session is in now; false when that state has no such command.
  bool RunNamedCommand(const std::string& name,
                       mozc::commands::Command* command);
  // The keymap table's name for the state keys are looked up in now.
  absl::string_view KeymapStateName() const;

  bool MoveCursorToEndInternal(mozc::commands::Command* command,
                               bool clear_undo);

  // update last_command_time;
  void UpdateTime();

  // update preferences only affecting this session.
  void UpdatePreferences(mozc::commands::Command* command);

  // Modify input of SendKey, TestSendKey, and SendCommand.
  void TransformInput(mozc::commands::Input* input);

  // ensure session status is not DIRECT.
  // if session status is DIRECT, set the status to PRECOMPOSITION.
  void EnsureIMEIsOn();

  // return true if |key_event| is a triggering key_event of
  // AutoIMEConversion.
  bool CanStartAutoConversion(const mozc::commands::KeyEvent& key_event) const;

  // Handles KeyEvent::activated to support indirect IME on/off.
  bool HandleIndirectImeOnOff(mozc::commands::Command* command);

  // Commits the raw text of the composition.
  bool CommitRawText(commands::Command* command);
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_SESSION_H_
