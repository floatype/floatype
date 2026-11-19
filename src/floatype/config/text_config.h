// floatype's old settings file, ~/.config/floatype/config.txtpb: protobuf
// text format of config.proto. Read only to move its settings into
// config.toml (settings_file.h), which never touches it.

#ifndef MOZC_FLOATYPE_CONFIG_TEXT_CONFIG_H_
#define MOZC_FLOATYPE_CONFIG_TEXT_CONFIG_H_

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "base/protobuf/descriptor.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {

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

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_CONFIG_TEXT_CONFIG_H_
