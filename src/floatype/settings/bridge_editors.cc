// floatype: the bridge's part for the settings screen's editor windows
// (issue #9): what they show and change, read and written here, so that the
// Swift side only draws.

#include <cstdlib>
#include <cstring>
#include <istream>
#include <memory>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/config_file_stream.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "floatype/config/settings_file.h"
#include "floatype/english/user_words.h"
#include "floatype/settings/config_lines.h"
#include "protocol/config.pb.h"
#include "config/config_handler.h"
#include "floatype/settings/bridge.h"
#include "floatype/zenz/zenz_memory.h"

namespace {

using ::mozc::floatype::ZenzMemory;

char* Copy(absl::string_view text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  std::memcpy(out, text.data(), text.size());
  out[text.size()] = '\0';
  return out;
}

// In the order they are listed, as FloatypeSettingsZenzMemory numbers them.
// The last, the words the user pinned (#28), is the user's own choice:
// forgetting everything keeps it, as clearing the history does.
ZenzMemory& (*const kMemories[])() = {
    &ZenzMemory::TurnedDown, &ZenzMemory::RefusedOnce, &ZenzMemory::Accepted,
    &ZenzMemory::Stopped, &ZenzMemory::Pinned, &ZenzMemory::Hidden};

// english_words.txt as read, so that an edit never writes over a file changed
// or unreadable since.
struct EnglishFile {
  std::string path;
  std::string text;
  mozc::floatype::FileStamp stamp;
  // There but not read: never written over, which would lose its lines.
  bool unreadable = false;
};

EnglishFile& English() {
  static EnglishFile* file = new EnglishFile();
  return *file;
}

// The first lines of a file the window makes.
constexpr absl::string_view kEnglishHeader =
    "# floatype: かなの入力中でも英字のまま残す英単語。1 行に 1 語。\n"
    "# -word と書いた語は、英字にしない。\n";

void ReadEnglish() {
  EnglishFile& file = English();
  file = EnglishFile{.path = mozc::floatype::UserFilePath(
                         mozc::floatype::kEnglishWordsFile)};
  file.stamp = mozc::floatype::StampOf(file.path);
  if (absl::StatusOr<std::string> text = mozc::FileUtil::GetContents(file.path);
      text.ok()) {
    file.text = *std::move(text);
  } else {
    file.unreadable = file.stamp.size >= 0;
  }
}

// A table's lines as the Qt editors take them in: blank lines and lines of
// fewer than two fields left out, at most 10,000 rows, each as
// "<input>\t<output>\t<next>\n" (next may be empty).
std::string RomanRows(absl::string_view text) {
  std::string out;
  int rows = 0;
  for (absl::string_view line : absl::StrSplit(text, '\n')) {
    line = absl::StripSuffix(line, "\r");
    const std::vector<absl::string_view> fields =
        absl::StrSplit(line, '\t', absl::AllowEmpty());
    if (line.empty() || fields.size() < 2) {
      continue;
    }
    absl::StrAppend(&out, fields[0], "\t", fields[1], "\t",
                    fields.size() >= 3 ? fields[2] : "", "\n");
    if (++rows >= 10000) {
      break;
    }
  }
  return out;
}

const std::string& DefaultRomanRows() {
  static const std::string* rows = [] {
    std::unique_ptr<std::istream> in = mozc::ConfigFileStream::LegacyOpen(
        "system://romanji-hiragana.tsv");
    std::string text((std::istreambuf_iterator<char>(*in)),
                     std::istreambuf_iterator<char>());
    return new std::string(RomanRows(text));
  }();
  return *rows;
}

// The table Mozc takes from rows: rows with no input, or with neither output
// nor next, left out; the next input only when there is one.
std::string RomanTableText(absl::string_view rows) {
  std::string out;
  for (const absl::string_view row :
       absl::StrSplit(rows, '\n', absl::SkipEmpty())) {
    const std::vector<absl::string_view> fields =
        absl::StrSplit(row, absl::MaxSplits('\t', 2));
    const absl::string_view input = fields[0];
    const absl::string_view output = fields.size() > 1 ? fields[1] : "";
    const absl::string_view next = fields.size() > 2 ? fields[2] : "";
    if (input.empty() || (output.empty() && next.empty())) {
      continue;
    }
    absl::StrAppend(&out, input, "\t", output, next.empty() ? "" : "\t",
                    next, "\n");
  }
  return out;
}

}  // namespace

extern "C" {

char* FloatypeSettingsRomanTable(const char* lines) {
  const absl::StatusOr<mozc::config::Config> config =
      mozc::floatype::ApplyConfigLines(mozc::config::Config(), lines);
  return Copy(config.ok() && !config->custom_roman_table().empty()
                  ? RomanRows(config->custom_roman_table())
                  : DefaultRomanRows());
}

char* FloatypeSettingsRomanRows(const char* text) {
  return Copy(text == nullptr ? DefaultRomanRows() : RomanRows(text));
}

char* FloatypeSettingsRomanTableText(const char* rows) {
  return Copy(RomanTableText(rows));
}

char* FloatypeSettingsRomanTableValue(const char* rows) {
  const std::string table = RomanTableText(rows);
  if (table.empty()) {
    return nullptr;
  }
  // The default table is kept as none, as the Qt editor does, so that a
  // later default applies.
  mozc::config::Config config;
  config.set_custom_roman_table(
      table == RomanTableText(DefaultRomanRows()) ? "" : table);
  constexpr absl::string_view kKey = "custom_roman_table = ";
  for (const absl::string_view line : absl::StrSplit(
           mozc::floatype::ConfigLines(config), '\n', absl::SkipEmpty())) {
    if (absl::StartsWith(line, kKey)) {
      return Copy(line.substr(kKey.size()));
    }
  }
  return nullptr;
}

void FloatypeSettingsStart(void) {
  // This app starts on its own, not through mozc_tool: the files it edits sit
  // next to config.toml, as the converter has them.
  mozc::floatype::SetUserFilesDir(
      mozc::FileUtil::Dirname(mozc::floatype::DefaultSettingsPath()),
      /*report_errors=*/true);
}

char* FloatypeSettingsEnglishWords(void) {
  ReadEnglish();
  const std::vector<std::string> presets = absl::StrSplit(
      mozc::floatype::PresetEnglishWords(), '\n', absl::SkipEmpty());
  const std::set<std::string> preset(presets.begin(), presets.end());
  std::set<std::string> shown;
  std::string out;
  for (const absl::string_view line : absl::StrSplit(English().text, '\n')) {
    const mozc::floatype::UserEnglishWords one =
        mozc::floatype::ParseUserEnglishWords(line);
    const bool keeps = !one.words.empty();
    const absl::string_view word =
        absl::StripTrailingAsciiWhitespace(keeps ? one.words : one.removed);
    if (!word.empty() && shown.insert(absl::AsciiStrToLower(word)).second) {
      absl::StrAppend(&out, word, "\t", keeps ? 1 : 0, "\t",
                      preset.contains(absl::AsciiStrToLower(word)) ? 1 : 2,
                      "\n");
    }
  }
  for (const std::string& word : presets) {
    if (shown.insert(word).second) {
      absl::StrAppend(&out, word, "\t1\t0\n");
    }
  }
  return Copy(out);
}

char* FloatypeSettingsEditEnglishWords(const char* words, int how) {
  if (how < 0 || how > 2) {
    return Copy("扱いの指定が違います。");
  }
  const std::vector<std::string> list =
      absl::StrSplit(words, '\n', absl::SkipEmpty());
  for (const std::string& word : list) {
    // The file's own rule, so that the window never writes a line left out.
    if (mozc::floatype::ParseUserEnglishWords(word).words !=
        absl::StrCat(word, "\n")) {
      return Copy(absl::StrFormat(
          "「%s」は使えません。英字だけの 3〜32 文字の語が使えます。", word));
    }
  }
  EnglishFile& file = English();
  if (file.path.empty()) {
    ReadEnglish();
  }
  if (!(mozc::floatype::StampOf(file.path) == file.stamp)) {
    ReadEnglish();
    return Copy(
        "開いたあとに english_words.txt が書き換えられたので、変えずに読み直し"
        "ました。もう一度操作してください。");
  }
  if (file.unreadable) {
    return Copy("english_words.txt を読めないので、変えませんでした。");
  }
  std::string text =
      file.stamp.size < 0 ? std::string(kEnglishHeader) : file.text;
  for (const std::string& word : list) {
    text = mozc::floatype::EditUserEnglishWords(
        text, word, static_cast<mozc::floatype::EnglishWordEdit>(how));
  }
  if (const absl::Status status =
          mozc::floatype::WriteAtomically(file.path, text);
      !status.ok()) {
    return Copy(absl::StrFormat("保存できませんでした（%s）", status.message()));
  }
  ReadEnglish();
  return nullptr;
}

char* FloatypeSettingsCharacterForms(const char* lines) {
  using ::mozc::config::Config;
  const absl::StatusOr<Config> read =
      mozc::floatype::ApplyConfigLines(Config(), lines);
  // As the Qt table: an empty list shows the default one.
  const Config& config = read.ok() && read->character_form_rules_size() > 0
                             ? *read
                             : mozc::config::ConfigHandler::DefaultConfig();
  std::string out;
  for (const Config::CharacterFormRule& rule : config.character_form_rules()) {
    absl::StrAppend(
        &out, rule.group(), "\t",
        Config::CharacterForm_Name(rule.preedit_character_form()), "\t",
        Config::CharacterForm_Name(rule.conversion_character_form()), "\n");
  }
  return Copy(out);
}

char* FloatypeSettingsWithCharacterForms(const char* rows) {
  using ::mozc::config::Config;
  Config config;
  for (const absl::string_view row :
       absl::StrSplit(rows, '\n', absl::SkipEmpty())) {
    const std::vector<std::string> parts = absl::StrSplit(row, '\t');
    Config::CharacterForm preedit, conversion;
    if (parts.size() != 3 || parts[0].empty() ||
        !Config::CharacterForm_Parse(parts[1], &preedit) ||
        !Config::CharacterForm_Parse(parts[2], &conversion)) {
      return nullptr;
    }
    Config::CharacterFormRule* rule = config.add_character_form_rules();
    rule->set_group(parts[0]);
    rule->set_preedit_character_form(preedit);
    rule->set_conversion_character_form(conversion);
  }
  if (config.character_form_rules_size() == 0) {
    return nullptr;
  }
  // The rules' line as the screen's other lines are written.
  constexpr absl::string_view kKey = "character_form_rules = ";
  for (const absl::string_view line : absl::StrSplit(
           mozc::floatype::ConfigLines(config), '\n', absl::SkipEmpty())) {
    if (absl::StartsWith(line, kKey)) {
      return Copy(line.substr(kKey.size()));
    }
  }
  return nullptr;
}

char* FloatypeSettingsUserFilesDirectory(void) {
  if (English().path.empty()) {
    ReadEnglish();
  }
  return Copy(mozc::FileUtil::Dirname(English().path));
}

char* FloatypeSettingsZenzMemory(void) {
  std::string out;
  for (int kind = 0; kind < std::ssize(kMemories); ++kind) {
    const auto entries = kMemories[kind]().Entries();
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
      absl::StrAppend(&out, kind, "\t", entry->first, "\t", entry->second,
                      "\n");
    }
  }
  return Copy(out);
}

void FloatypeSettingsZenzForget(int kind, const char* reading,
                                const char* text) {
  if (kind >= 0 && kind < std::ssize(kMemories)) {
    kMemories[kind]().Remove(reading, text);
  }
}

void FloatypeSettingsZenzForgetAll(void) {
  for (ZenzMemory& (*memory)() : kMemories) {
    // The user's own choices stay, as the user dictionary does.
    if (memory != &ZenzMemory::Pinned && memory != &ZenzMemory::Hidden) {
      memory().Clear();
    }
  }
}

char* FloatypeSettingsProfileDirectory(void) {
  return Copy(mozc::SystemUtil::GetUserProfileDirectory());
}

}  // extern "C"
