// floatype's text settings file: protobuf text format of config.proto, kept
// by the user (for example in dotfiles). It overrides the fields it sets; the
// settings screen's file (config1.db) governs the rest. floatype never writes
// it, so comments and order stay as the user wrote them.

#ifndef MOZC_FLOATYPE_CONFIG_TEXT_CONFIG_H_
#define MOZC_FLOATYPE_CONFIG_TEXT_CONFIG_H_

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "base/protobuf/descriptor.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {

// ~/.config/floatype/config.txtpb
std::string DefaultTextConfigPath();

// Parses the file's text and checks the ranges of floatype's numbers. An error
// names the line, as in "3 行目: ...".
absl::StatusOr<config::Config> ParseTextConfig(absl::string_view text);

// Checks a number of floatype's whose type alone does not keep it sensible
// (other fields always pass). The error names `line`, counted from 1.
absl::Status CheckConfigRange(const google::protobuf::FieldDescriptor& field,
                              int64_t value, int line);

// Copies the fields set in `overlay` onto `config`. Repeated fields are
// replaced rather than appended.
void ApplyOverlay(const config::Config& overlay, config::Config& config);

// Gives the fields set in `overlay` the values (or absence) they have in
// `stored`, so that values pinned by the file never reach config1.db.
void RestoreOverlaidFields(const config::Config& overlay,
                           const config::Config& stored,
                           config::Config& config);

// The fields of `config` whose values differ from `defaults`, as text to
// paste into the file. Leaves out general_config (the settings file's own
// bookkeeping) and nested fields that equal their defaults.
std::string ExportTextConfig(const config::Config& config,
                             const config::Config& defaults);

// The process-wide file. Its path is empty, and the file is never read, until
// one of floatype's main()s sets it, so tests never read the user's file.
// With `report_errors`, errors are also shown as a macOS notification.
void SetTextConfigPath(std::string path, bool report_errors);
// Reads the file again. A broken file keeps the last good settings.
void ReloadTextConfig();
// Whether the file changed since it was last read. Looks at most once a
// second, so it can be called for every command.
bool TextConfigChanged();
// What the file sets, as last read successfully; empty without a file.
config::Config TextConfigOverlay();

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_CONFIG_TEXT_CONFIG_H_
