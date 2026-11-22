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

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOZC_FLOATYPE_SETTINGS_BRIDGE_H_
