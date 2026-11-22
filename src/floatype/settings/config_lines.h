// floatype: the settings screen's view of config.proto (issue #9). The
// SwiftUI screen reads the settings as lines of TOML dotted keys and gives
// back, in the same form, only what the user changed, which the converter's
// settings take over.

#ifndef MOZC_FLOATYPE_SETTINGS_CONFIG_LINES_H_
#define MOZC_FLOATYPE_SETTINGS_CONFIG_LINES_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {

// Every singular field with the value in effect, the default where unset, a
// line each: `live_conversion = true`, `indicator_style.position = "BELOW"`
// (enums by name, strings quoted as in TOML). A repeated field is one line,
// an array (of inline tables for messages:
// `character_form_rules = [{group = "0", ...}, ...]`), as a change to it
// replaces it whole. general_config (the settings file's bookkeeping) and
// unset fields with no default (a candidate window's colors, which its theme
// gives then) are left out.
std::string ConfigLines(const config::Config& config);

// `loaded` with the fields `toml` sets (as ConfigLines writes them, or any
// TOML config.toml takes) set; the others stay as they were, set or unset, and
// a repeated field it names is replaced. A line config.toml would refuse is an
// error naming it.
absl::StatusOr<config::Config> ApplyConfigLines(const config::Config& loaded,
                                                absl::string_view toml);

// 一般's 数字の入力, from the digits' rule (group "0") in
// character_form_rules: 0 半角, 1 全角, 2 全角 while typing and the last form
// once converted (Mozc's default); 3 another pair, or no such rule.
int NumberForm(const config::Config& config);

// Sets the digits' rule to `form` (0 to 2, as NumberForm gives it), the other
// rules as they are; false, and `config` as it was, without that rule.
bool SetNumberForm(int form, config::Config& config);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_SETTINGS_CONFIG_LINES_H_
