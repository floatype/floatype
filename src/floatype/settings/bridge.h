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

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOZC_FLOATYPE_SETTINGS_BRIDGE_H_
