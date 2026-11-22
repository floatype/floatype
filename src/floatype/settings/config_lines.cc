#include "floatype/settings/config_lines.h"

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "base/protobuf/descriptor.h"
#include "base/protobuf/message.h"
#include "floatype/config/text_config.h"
#include "floatype/config/toml_config.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {
namespace {

using ::google::protobuf::Descriptor;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::Message;
using ::google::protobuf::Reflection;

// A TOML basic string.
std::string Quoted(absl::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
          absl::StrAppendFormat(&out, "\\u%04x", static_cast<unsigned char>(c));
        } else {
          out += c;
        }
    }
  }
  return out + "\"";
}

void AppendLines(const Message& message, absl::string_view prefix,
                 std::string& out) {
  const Descriptor& descriptor = *message.GetDescriptor();
  const Reflection& reflection = *message.GetReflection();
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const FieldDescriptor& field = *descriptor.field(i);
    if (field.is_repeated() || (prefix.empty() && field.name() == "general_config")) {
      continue;
    }
    // Unset with no default (a candidate window's color, left to its theme):
    // no line, so that the lines read back leave it unset.
    if (field.cpp_type() != FieldDescriptor::CPPTYPE_MESSAGE &&
        field.has_presence() && !field.has_default_value() &&
        !reflection.HasField(message, &field)) {
      continue;
    }
    const std::string key = absl::StrCat(prefix, field.name());
    std::string value;
    switch (field.cpp_type()) {
      case FieldDescriptor::CPPTYPE_MESSAGE:
        AppendLines(reflection.GetMessage(message, &field),
                    absl::StrCat(key, "."), out);
        continue;
      case FieldDescriptor::CPPTYPE_BOOL:
        value = reflection.GetBool(message, &field) ? "true" : "false";
        break;
      case FieldDescriptor::CPPTYPE_INT32:
        value = absl::StrCat(reflection.GetInt32(message, &field));
        break;
      case FieldDescriptor::CPPTYPE_UINT32:
        value = absl::StrCat(reflection.GetUInt32(message, &field));
        break;
      case FieldDescriptor::CPPTYPE_INT64:
        value = absl::StrCat(reflection.GetInt64(message, &field));
        break;
      case FieldDescriptor::CPPTYPE_UINT64:
        value = absl::StrCat(reflection.GetUInt64(message, &field));
        break;
      case FieldDescriptor::CPPTYPE_FLOAT:
        value = absl::StrCat(reflection.GetFloat(message, &field));
        break;
      case FieldDescriptor::CPPTYPE_DOUBLE:
        value = absl::StrCat(reflection.GetDouble(message, &field));
        break;
      case FieldDescriptor::CPPTYPE_ENUM:
        value = Quoted(reflection.GetEnum(message, &field)->name());
        break;
      case FieldDescriptor::CPPTYPE_STRING:
        value = Quoted(reflection.GetString(message, &field));
        break;
    }
    absl::StrAppend(&out, key, " = ", value, "\n");
  }
}

}  // namespace

std::string ConfigLines(const config::Config& config) {
  std::string out;
  AppendLines(config, "", out);
  return out;
}

absl::StatusOr<config::Config> ApplyConfigLines(const config::Config& loaded,
                                                absl::string_view toml) {
  absl::StatusOr<config::Config> changes = ParseTomlConfig(toml);
  if (!changes.ok()) {
    return changes.status();
  }
  config::Config config = loaded;
  ApplyOverlay(*changes, config);
  return config;
}

}  // namespace mozc::floatype
