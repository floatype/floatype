// floatype: the bridge's part for the keymap editor (issue #9): the table as
// rows, the rows the Qt editor never showed kept to be written back, the
// commands and modes with the Qt editor's Japanese names, and the checks it
// made on a key.

#include <cstdlib>
#include <cstring>
#include <istream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/config_file_stream.h"
#include "composer/key_parser.h"
#include "floatype/settings/bridge.h"
#include "floatype/settings/config_lines.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/keymap.h"

namespace {

using ::mozc::commands::KeyEvent;
using ::mozc::config::Config;

char* Copy(absl::string_view text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  std::memcpy(out, text.data(), text.size());
  out[text.size()] = '\0';
  return out;
}

// The modes, in the Qt editor's order.
constexpr absl::string_view kModes[] = {
    "DirectInput", "Precomposition", "Composition",
    "Conversion",  "Suggestion",     "Prediction",
};

// keymap_ja.qtts: the Qt editor's Japanese names of modes and commands.
const absl::flat_hash_map<absl::string_view, absl::string_view>& Names() {
  static const auto* names =
      new absl::flat_hash_map<absl::string_view, absl::string_view>{
          // floatype's own (behavior.md, Ctrl+z).
          {"ZenzCorrect", "zenz に補正を聞く"},
    {"Conversion", "変換中"},
    {"Composition", "変換前入力中"},
    {"DirectInput", "直接入力"},
    {"Precomposition", "入力文字なし"},
    {"Prediction", "サジェスト選択中"},
    {"Suggestion", "サジェスト表示中"},
    {"Backspace", "左の文字を削除"},
    {"Cancel", "キャンセル"},
    {"CancelAndIMEOff", "キャンセル後 IME を無効化"},
    {"Revert", "最後の学習をキャンセル"},
    {"Commit", "確定"},
    {"CommitFirstSuggestion", "最初の予測候補を確定"},
    {"CommitOnlyFirstSegment", "最初の文節のみを確定"},
    {"Convert", "変換"},
    {"ConvertNext", "次候補を選択"},
    {"ConvertNextPage", "次候補ページを選択"},
    {"ConvertPrev", "前候補を選択"},
    {"ConvertPrevPage", "前候補ページを選択"},
    {"ConvertToFullAlphanumeric", "全角英数に変換"},
    {"ConvertToFullKatakana", "全角カタカナに変換"},
    {"ConvertToHalfKatakana", "半角カタカナに変換"},
    {"ConvertToHalfAlphanumeric", "半角英数に変換"},
    {"ConvertToHalfWidth", "半角に変換"},
    {"ConvertToHiragana", "ひらがなに変換"},
    {"ConvertWithoutHistory", "学習を使わずに変換"},
    {"Delete", "右の文字を削除"},
    {"DisplayAsFullAlphanumeric", "全角英数に表示切替"},
    {"DisplayAsFullKatakana", "全角カタカナに表示切替"},
    {"DisplayAsHalfKatakana", "半角カタカナに表示切替"},
    {"DisplayAsHalfAlphanumeric", "半角英数に表示切替"},
    {"DisplayAsHalfWidth", "半角に表示切替"},
    {"DisplayAsHiragana", "ひらがなに表示切替"},
    {"IMEOff", "IME を無効化"},
    {"IMEOn", "IME を有効化"},
    {"CompositionModeSwitchKanaType", "次のかな文字種に入力切替"},
    {"CompositionModeFullAlphanumeric", "全角英数に入力切替"},
    {"CompositionModeFullKatakana", "全角カタカナに入力切替"},
    {"CompositionModeHalfAlphanumeric", "半角英数に入力切替"},
    {"CompositionModeHalfKatakana", "半角カタカナに入力切替"},
    {"CompositionModeHiragana", "ひらがなに入力切替"},
    {"InsertAlternateSpace", "代替空白文字を入力"},
    {"InsertFullSpace", "全角空白を入力"},
    {"InsertHalfSpace", "半角空白を入力"},
    {"InsertSpace", "空白を入力"},
    {"MoveCursorLeft", "カーソルを左に移動"},
    {"MoveCursorRight", "カーソルを右に移動"},
    {"MoveCursorToBeginning", "カーソルを左端に移動"},
    {"MoveCursorToEnd", "カーソルを右端に移動"},
    {"PredictAndConvert", "予測変換"},
    {"SegmentFocusFirst", "文節を左端に移動"},
    {"SegmentFocusLast", "文節を右端に移動"},
    {"SegmentFocusLeft", "文節を左に移動"},
    {"SegmentFocusRight", "文節を右に移動"},
    {"SegmentWidthExpand", "文節を伸ばす"},
    {"SegmentWidthShrink", "文節を縮める"},
    {"SwitchKanaType", "ひらがな・カタカナを切替"},
    {"ToggleAlphanumericMode", "英数入力切り替え"},
    {"LaunchConfigDialog", "プロパティを起動"},
    {"LaunchDictionaryTool", "辞書ツールを起動"},
    {"LaunchWordRegisterDialog", "単語登録を起動"},
    {"Undo", "確定取り消し"},
    {"Reconvert", "再変換"},
    {"DeleteSelectedCandidate", "選択候補を履歴から削除"},
      };
  return *names;
}

// What the Qt editor never showed, and so never let be changed (keymap_editor
// .cc's KeyMapValidator): such rows are kept as they are.
bool VisibleKey(absl::string_view key) {
  KeyEvent event;
  if (!mozc::KeyParser::ParseKey(key, &event)) {
    return false;
  }
  for (const uint32_t modifier : event.modifier_keys()) {
    if (modifier == KeyEvent::KEY_DOWN || modifier == KeyEvent::KEY_UP) {
      return false;
    }
  }
  if (event.has_special_key()) {
    switch (event.special_key()) {
      case KeyEvent::KANJI:
      case KeyEvent::ON:
      case KeyEvent::OFF:
      case KeyEvent::TEXT_INPUT:
        return false;
      default:
        break;
    }
  }
  return true;
}

bool VisibleCommand(absl::string_view command) {
  // A sequence ("Commit|IMEOff") has no row in the editor.
  return !absl::StrContains(command, '|') && command != "InsertCharacter" &&
         command != "ReportBug" && command != "EditInsert" &&
         // On a Mac, IME on and off cannot be bound.
         command != "IMEOn" && command != "IMEOff";
}

// "<mode>\t<key>\t<command>\t<shown>\n" rows from a keymap file's text, the
// shown ones first, as the Qt editor kept them.
std::string KeymapRows(absl::string_view text) {
  std::string shown, kept;
  bool first = true;
  for (absl::string_view line : absl::StrSplit(text, '\n')) {
    line = absl::StripSuffix(line, "\r");
    if (first) {  // The header, "status\tkey\tcommand".
      first = false;
      continue;
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const std::vector<absl::string_view> fields =
        absl::StrSplit(line, '\t', absl::SkipEmpty());
    if (fields.size() < 3) {
      continue;
    }
    const bool visible = VisibleKey(fields[1]) && VisibleCommand(fields[2]);
    absl::StrAppend(visible ? &shown : &kept, fields[0], "\t", fields[1], "\t",
                    fields[2], visible ? "\t1\n" : "\t0\n");
  }
  return shown + kept;
}

std::string PresetText(Config::SessionKeymap keymap) {
  std::unique_ptr<std::istream> in = mozc::ConfigFileStream::LegacyOpen(
      mozc::keymap::KeyMapManager::GetKeyMapFileName(keymap));
  if (in == nullptr) {
    return "";
  }
  return std::string((std::istreambuf_iterator<char>(*in)),
                     std::istreambuf_iterator<char>());
}

// The table Mozc takes from rows, with its header.
std::string KeymapText(absl::string_view rows) {
  std::string out = "status\tkey\tcommand\n";
  for (const absl::string_view row :
       absl::StrSplit(rows, '\n', absl::SkipEmpty())) {
    const std::vector<absl::string_view> fields = absl::StrSplit(row, '\t');
    if (fields.size() >= 3 && !fields[0].empty() && !fields[1].empty() &&
        !fields[2].empty()) {
      absl::StrAppend(&out, fields[0], "\t", fields[1], "\t", fields[2], "\n");
    }
  }
  return out;
}

}  // namespace

extern "C" {

char* FloatypeSettingsKeymap(const char* lines) {
  const absl::StatusOr<Config> config =
      mozc::floatype::ApplyConfigLines(Config(), lines);
  if (!config.ok()) {
    return Copy("");
  }
  // A preset shows its own file, as the Qt editor opened it.
  return Copy(KeymapRows(config->session_keymap() == Config::CUSTOM
                             ? config->custom_keymap_table()
                             : PresetText(config->session_keymap())));
}

char* FloatypeSettingsKeymapPreset(const char* keymap) {
  Config::SessionKeymap value;
  if (!Config::SessionKeymap_Parse(keymap, &value) ||
      value == Config::CUSTOM) {
    return Copy("");
  }
  return Copy(KeymapRows(PresetText(value)));
}

char* FloatypeSettingsKeymapRows(const char* text) {
  return Copy(KeymapRows(text));
}

char* FloatypeSettingsKeymapText(const char* rows) {
  return Copy(KeymapText(rows));
}

char* FloatypeSettingsKeymapValue(const char* rows) {
  bool shown = false;
  for (const absl::string_view row :
       absl::StrSplit(rows, '\n', absl::SkipEmpty())) {
    shown = shown || absl::EndsWith(row, "\t1");
  }
  if (!shown) {
    return nullptr;
  }
  Config config;
  config.set_custom_keymap_table(KeymapText(rows));
  constexpr absl::string_view kKey = "custom_keymap_table = ";
  for (const absl::string_view line : absl::StrSplit(
           mozc::floatype::ConfigLines(config), '\n', absl::SkipEmpty())) {
    if (absl::StartsWith(line, kKey)) {
      return Copy(line.substr(kKey.size()));
    }
  }
  return nullptr;
}

char* FloatypeSettingsKeymapNames(void) {
  std::string out;
  for (const absl::string_view mode : kModes) {
    const auto name = Names().find(mode);
    absl::StrAppend(&out, "mode\t", mode, "\t",
                    name == Names().end() ? mode : name->second, "\n");
  }
  absl::flat_hash_set<std::string> all;
  mozc::keymap::KeyMapManager manager;
  manager.AppendAvailableCommandNameDirect(all);
  manager.AppendAvailableCommandNamePrecomposition(all);
  manager.AppendAvailableCommandNameComposition(all);
  manager.AppendAvailableCommandNameConversion(all);
  manager.AppendAvailableCommandNameZeroQuerySuggestion(all);
  manager.AppendAvailableCommandNameSuggestion(all);
  manager.AppendAvailableCommandNamePrediction(all);
  const absl::btree_set<std::string> commands(all.begin(), all.end());
  for (const std::string& command : commands) {
    if (VisibleCommand(command)) {
      const auto name = Names().find(command);
      absl::StrAppend(&out, "command\t", command, "\t",
                      name == Names().end() ? command : name->second, "\n");
    }
  }
  return Copy(out);
}

int FloatypeSettingsKeymapKeyIsValid(const char* key) {
  return VisibleKey(key) ? 1 : 0;
}

}  // extern "C"
