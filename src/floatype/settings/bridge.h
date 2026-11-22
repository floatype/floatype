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

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOZC_FLOATYPE_SETTINGS_BRIDGE_H_
