// floatype's settings file, ~/.config/floatype/config.toml: config.proto in
// TOML. Keys are the proto's field names, nested messages are tables
// ([indicator_style]), repeated messages arrays of tables
// ([[character_form_rules]]), enums their value names as strings, and fields
// named *_color are written in hex (0x112233). general_config, the settings
// file's own bookkeeping, is never written and is ignored when read.

#ifndef MOZC_FLOATYPE_CONFIG_TOML_CONFIG_H_
#define MOZC_FLOATYPE_CONFIG_TOML_CONFIG_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {

// The result holds only the fields the file sets. Unknown keys, wrong types,
// unknown enum names and out-of-range numbers are errors naming the line, as
// in "3 行目: ...".
absl::StatusOr<config::Config> ParseTomlConfig(absl::string_view text);

// Returns `text` changed so that, read over `defaults`, it gives `config`.
// Only the fields whose value changes are touched, so comments, blank lines
// and order stay (but see the tidying below), and so does a value written in
// the file that equals its default. A changed value is replaced in place (a
// trailing comment stays); a value now equal to its default loses its
// line(s); a new one is added after the last key of its table, and a new
// table is added at the end of the file.
// Two kinds of fields are rewritten whole at the end of the file when they
// change, losing the comments inside them: repeated messages (all their
// [[name]] blocks), and nested messages written as inline tables or dotted
// keys rather than under a [name] header. When anything changes, the file is
// tidied too: a [name] table with no keys and no comment beside it goes, and
// blank lines run to one, with none at the start or the end (lines inside a
// multi-line value stay). Errors if `text` does not parse, so that the caller
// leaves the file alone.
absl::StatusOr<std::string> UpdateTomlConfig(absl::string_view text,
                                             const config::Config& config,
                                             const config::Config& defaults);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_CONFIG_TOML_CONFIG_H_
