// floatype's settings file, ~/.config/floatype/config.toml: the one place the
// settings live, Mozc's and floatype's. Hand edits take effect on the next key
// a second later; the settings screen writes its changes into the file,
// keeping comments and order. config1.db keeps a copy of the last good
// settings, used while the file is broken.

#ifndef MOZC_FLOATYPE_CONFIG_SETTINGS_FILE_H_
#define MOZC_FLOATYPE_CONFIG_SETTINGS_FILE_H_

#include <string>

#include "absl/status/statusor.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {

// ~/.config/floatype/config.toml
std::string DefaultSettingsPath();

// The process-wide file. Its path is empty, and ConfigHandler uses only
// config1.db as in Mozc, until one of floatype's main()s sets it, so tests
// never touch the user's file. With `report_errors`, errors are also shown as
// a macOS notification. Only a `writable` process writes the file or
// config1.db.
void SetSettingsPath(std::string path, bool report_errors, bool writable);
bool SettingsFileEnabled();
bool SettingsFileWritable();

// Whether the file changed since it was last read or written. Looks at most
// once a second, so it can be called for every command.
bool SettingsFileChanged();

// Whether good settings were read or written since the path was set, so that
// a broken file can keep them rather than fall back to config1.db.
bool SettingsFileLoaded();

// The settings the file gives: `defaults` with the fields it sets. Without
// the file, `stored` (config1.db) with the old config.txtpb next to it
// applied; a writable process writes them to the file, and `migrated` is set.
// A broken file is an error, logged and reported.
absl::StatusOr<config::Config> ReadSettingsFile(const config::Config& defaults,
                                                const config::Config& stored,
                                                bool& migrated);

// Writes `config` into the file, changing only the lines needed, and returns
// the settings the file now gives. An error, logged and reported, leaves the
// file as it was: a broken file, a failed write, or a read-only process.
absl::StatusOr<config::Config> WriteSettingsFile(
    const config::Config& config, const config::Config& defaults);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_CONFIG_SETTINGS_FILE_H_
