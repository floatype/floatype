// floatype: what the SwiftUI settings screen calls (issue #9), in C so that
// Swift imports it as is. The converter keeps the settings and writes
// config.toml; the screen only reads them and hands back its changes, as the
// Qt screen did through the same client.

#ifndef MOZC_FLOATYPE_SETTINGS_BRIDGE_H_
#define MOZC_FLOATYPE_SETTINGS_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

// The converter's settings as ConfigLines (config_lines.h) writes them, or
// NULL if the converter cannot be reached. Free it with FloatypeSettingsFree.
char* FloatypeSettingsLoad(void);

// Applies `toml`, the changed fields, to the converter's settings as they are
// now and saves them. NULL when saved, otherwise why not; free it with
// FloatypeSettingsFree.
char* FloatypeSettingsSave(const char* toml);

void FloatypeSettingsFree(char* text);

// Saves the default settings in place of the converter's, as 初期値に戻す
// does; what it learned stays. NULL when saved, otherwise why not.
char* FloatypeSettingsReset(void);

// Clears what the converter learned, the history and the predictions, as
// 学習履歴のクリア does. NULL when cleared, otherwise why not.
char* FloatypeSettingsClearHistory(void);

// Opens the converter's tool `mode` ("dictionary_tool"). NULL when opened,
// otherwise why not.
char* FloatypeSettingsLaunchTool(const char* mode);

// Whether the converter starts at login, and setting it (macOS's login item
// for the prelauncher, as the Qt screen's スタートアップ).
int FloatypeSettingsStartsAtLogin(void);
void FloatypeSettingsSetStartsAtLogin(int on);

// This Floatype's version ("0.1.1+mozc.3.35.6354"), to free.
char* FloatypeSettingsVersion(void);

// Where to ask for the latest release (update_check.h's kLatestReleaseUrl);
// not to free.
const char* FloatypeSettingsLatestReleaseUrl(void);

// What GitHub's latest release says (update_check.h): `status` is the HTTP
// status, `tag` the release's tag, `assets` a line "name\turl" an asset.
// Returns 0 for a newer version, 1 for up to date, 2 for no release yet, 3
// when the answer cannot be used; with 0, `version`, `package_url` and
// `checksum_url` are set to text to free.
int FloatypeSettingsCheckRelease(int status, const char* tag,
                                 const char* assets, char** version,
                                 char** package_url, char** checksum_url);

// Whether the .sha256 file's text names `hex_digest`.
int FloatypeSettingsChecksumMatches(const char* checksum_file,
                                    const char* hex_digest);

// A candidate window's look as the renderer draws it (window_look.h), for
// 外観's preview. Colors are 0xRRGGBB; measures and font sizes in points.
typedef struct {
  int dark;
  unsigned background, text, secondary_text;
  unsigned focused_background, focused_text, focused_secondary_text;
  unsigned focused_border;
  double focused_border_alpha;
  unsigned border;
  double border_alpha;
  double size_percent;  // Of fonts and measures.
  double window_padding, row_height, row_padding, row_corner_radius;
  double number_width, footer_top_padding, footer_bottom_padding;
  double corner_radius, opacity;
  double shadow_radius, shadow_offset_y, shadow_opacity;
  unsigned shadow_color;
  double candidate_font_size, number_font_size, description_font_size;
  double footer_font_size;
  // The footer's dot for conversion candidates (candidate_kind_mark).
  unsigned kind_mark_color;
  // A candidate column is at least this wide (column_minimum_width_string).
  char column_minimum_width[64];
} FloatypeWindowLook;

// The look of the candidate window or, with `suggestion`, the prediction list,
// with the settings `lines` (as FloatypeSettingsLoad gives them, changes
// applied); `dark` is the system appearance, for the automatic theme. 1 when
// `out` is set, 0 if `lines` cannot be read.
int FloatypeSettingsWindowLook(const char* lines, int suggestion, int dark,
                               FloatypeWindowLook* out);

// What the indicator shows for `state` (1 入力中, 2 Mozc, 3 zenz pending, 4
// Zenz, as commands.proto numbers them) with indicator_style.label `label`
// ("FULL_LABEL", "SYMBOL", "COLOR_ONLY"); to free.
char* FloatypeSettingsIndicatorLabel(int state, const char* label);

// 一般's 数字の入力 with the settings `lines` (character_form_rules's line
// is enough): 0 半角, 1 全角, 2 全角 while typing and the last form once
// converted, 3 another pair or no digits' rule.
int FloatypeSettingsNumberForm(const char* lines);

// The value of character_form_rules, `[{...}, ...]`, with the digits' rule set
// to `form` (0 to 2) and the other rules as they are; NULL without a digits'
// rule. To free.
char* FloatypeSettingsWithNumberForm(const char* lines, int form);

// zenz's learning data (学習データを見る…): one "<kind>\t<reading>\t<text>"
// line each, kind 0 退けた語, 1 1 回退けた語, 2 すぐ出す補正, 3 止めた補正,
// the newest of each first. To free.
char* FloatypeSettingsZenzMemory(void);
// Forgets one line of it, or all of it.
void FloatypeSettingsZenzForget(int kind, const char* reading,
                                const char* text);
void FloatypeSettingsZenzForgetAll(void);
// The folder the learning data is in. To free.
char* FloatypeSettingsProfileDirectory(void);

// Tells where the user's files are (next to config.toml); the app calls it
// once as it starts, before the editor windows read them.
void FloatypeSettingsStart(void);

// The English words kept in letters (英単語を編集…), read from
// english_words.txt again: one "<word>\t<kept>\t<from>" line each, kept 1
// (英字にする) or 0 (英字にしない), from 0 the preset, 1 the preset changed in
// the file, 2 the file alone. To free.
char* FloatypeSettingsEnglishWords(void);
// Makes the words (one a line) kept (`how` 0), taken out (1) or as the
// built-in lists say (2, the line removed) in english_words.txt; NULL, or why
// nothing was written: a word the file would leave out, or a file changed or
// unreadable since FloatypeSettingsEnglishWords read it. To free.
char* FloatypeSettingsEditEnglishWords(const char* words, int how);
// The folder english_words.txt is in (next to config.toml). To free.
char* FloatypeSettingsUserFilesDirectory(void);

// The 全角・半角 table with the settings `lines` (the default table when they
// have none): one "<group>\t<typed>\t<converted>" line a rule, the forms
// by name (FULL_WIDTH, HALF_WIDTH, LAST_FORM). To free.
char* FloatypeSettingsCharacterForms(const char* lines);
// The value of character_form_rules, `[{...}, ...]`, for rows as
// FloatypeSettingsCharacterForms gives them; NULL for none or a row it cannot
// read. To free.
char* FloatypeSettingsWithCharacterForms(const char* rows);

// The romaji table (ローマ字テーブル), one "<input>\t<output>\t<next>" row
// a line (next may be empty): from the settings `lines`, Mozc's default
// table when they have none. To free.
char* FloatypeSettingsRomanTable(const char* lines);
// Rows from a table file's `text` (as imported), or Mozc's default table for
// NULL; blank lines and lines of fewer than two fields are left out. To free.
char* FloatypeSettingsRomanRows(const char* text);
// The table for rows, as a table file holds it (exported). To free.
char* FloatypeSettingsRomanTableText(const char* rows);
// The value of custom_roman_table for rows: `""` when they are the default
// table; NULL when no row has an input and an output or next. To free.
char* FloatypeSettingsRomanTableValue(const char* rows);

// The keymap editor's table, one "<mode>\t<key>\t<command>\t<shown>" row
// a line: the rows it shows (shown 1) first, then those the Qt editor never
// showed (0), which are to be saved back as they are. From the settings
// `lines`: the custom table, or the preset's file for a preset. To free.
char* FloatypeSettingsKeymap(const char* lines);
// The rows of a preset ("ATOK", "MSIME", "KOTOERI"), or of a keymap file's
// `text` (as imported). To free.
char* FloatypeSettingsKeymapPreset(const char* keymap);
char* FloatypeSettingsKeymapRows(const char* text);
// The table for rows, as a keymap file holds it (exported). To free.
char* FloatypeSettingsKeymapText(const char* rows);
// The value of custom_keymap_table for rows; NULL without a row shown. To
// free.
char* FloatypeSettingsKeymapValue(const char* rows);
// The modes and the commands that can be bound, with their Japanese names:
// "mode\t<name>\t<shown>" and "command\t<name>\t<shown>" lines. To free.
char* FloatypeSettingsKeymapNames(void);
// 1 if `key` ("Ctrl Shift a", "Henkan") can be bound in the editor.
int FloatypeSettingsKeymapKeyIsValid(const char* key);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOZC_FLOATYPE_SETTINGS_BRIDGE_H_
