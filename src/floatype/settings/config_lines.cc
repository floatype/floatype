#include "floatype/settings/config_lines.h"

#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

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

// The field's value in TOML; `index` picks a repeated field's element.
std::string Value(const Message& message, const FieldDescriptor& field,
                  int index) {
  const Reflection& reflection = *message.GetReflection();
  const bool repeated = field.is_repeated();
  switch (field.cpp_type()) {
    case FieldDescriptor::CPPTYPE_MESSAGE:
      break;  // Written as a table by the callers.
    case FieldDescriptor::CPPTYPE_BOOL:
      return (repeated ? reflection.GetRepeatedBool(message, &field, index)
                       : reflection.GetBool(message, &field))
                 ? "true"
                 : "false";
    case FieldDescriptor::CPPTYPE_INT32:
      return absl::StrCat(
          repeated ? reflection.GetRepeatedInt32(message, &field, index)
                   : reflection.GetInt32(message, &field));
    case FieldDescriptor::CPPTYPE_UINT32:
      return absl::StrCat(
          repeated ? reflection.GetRepeatedUInt32(message, &field, index)
                   : reflection.GetUInt32(message, &field));
    case FieldDescriptor::CPPTYPE_INT64:
      return absl::StrCat(
          repeated ? reflection.GetRepeatedInt64(message, &field, index)
                   : reflection.GetInt64(message, &field));
    case FieldDescriptor::CPPTYPE_UINT64:
      return absl::StrCat(
          repeated ? reflection.GetRepeatedUInt64(message, &field, index)
                   : reflection.GetUInt64(message, &field));
    case FieldDescriptor::CPPTYPE_FLOAT:
      return absl::StrCat(
          repeated ? reflection.GetRepeatedFloat(message, &field, index)
                   : reflection.GetFloat(message, &field));
    case FieldDescriptor::CPPTYPE_DOUBLE:
      return absl::StrCat(
          repeated ? reflection.GetRepeatedDouble(message, &field, index)
                   : reflection.GetDouble(message, &field));
    case FieldDescriptor::CPPTYPE_ENUM:
      return Quoted(
          (repeated ? reflection.GetRepeatedEnum(message, &field, index)
                    : reflection.GetEnum(message, &field))
              ->name());
    case FieldDescriptor::CPPTYPE_STRING:
      return Quoted(repeated
                        ? reflection.GetRepeatedString(message, &field, index)
                        : reflection.GetString(message, &field));
  }
  return "";
}

// Unset with no default (a candidate window's color, left to its theme): no
// line, so that the lines read back leave it unset.
bool LeftOut(const Message& message, const FieldDescriptor& field) {
  return field.cpp_type() != FieldDescriptor::CPPTYPE_MESSAGE &&
         field.has_presence() && !field.has_default_value() &&
         !message.GetReflection()->HasField(message, &field);
}

// A message of plain fields as an inline table: {group = "0", ...}.
std::string InlineTable(const Message& message) {
  const Descriptor& descriptor = *message.GetDescriptor();
  std::string out = "{";
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const FieldDescriptor& field = *descriptor.field(i);
    if (field.is_repeated() ||
        field.cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE ||
        LeftOut(message, field)) {
      continue;
    }
    absl::StrAppend(&out, out.size() > 1 ? ", " : "", field.name(), " = ",
                    Value(message, field, -1));
  }
  return out + "}";
}

void AppendLines(const Message& message, absl::string_view prefix,
                 std::string& out) {
  const Descriptor& descriptor = *message.GetDescriptor();
  const Reflection& reflection = *message.GetReflection();
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const FieldDescriptor& field = *descriptor.field(i);
    if ((prefix.empty() && field.name() == "general_config") ||
        LeftOut(message, field)) {
      continue;
    }
    const std::string key = absl::StrCat(prefix, field.name());
    if (field.is_repeated()) {
      // One line for the whole list, which a change replaces whole.
      std::string value = "[";
      for (int j = 0; j < reflection.FieldSize(message, &field); ++j) {
        absl::StrAppend(
            &value, j > 0 ? ", " : "",
            field.cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE
                ? InlineTable(reflection.GetRepeatedMessage(message, &field, j))
                : Value(message, field, j));
      }
      absl::StrAppend(&out, key, " = ", value, "]\n");
    } else if (field.cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
      AppendLines(reflection.GetMessage(message, &field),
                  absl::StrCat(key, "."), out);
    } else {
      absl::StrAppend(&out, key, " = ", Value(message, field, -1), "\n");
    }
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

namespace {

using Form = config::Config::CharacterForm;
constexpr std::pair<Form, Form> kNumberForms[] = {
    {config::Config::HALF_WIDTH, config::Config::HALF_WIDTH},
    {config::Config::FULL_WIDTH, config::Config::FULL_WIDTH},
    {config::Config::FULL_WIDTH, config::Config::LAST_FORM}};
constexpr absl::string_view kDigits = "0";

}  // namespace

int NumberForm(const config::Config& config) {
  for (const config::Config::CharacterFormRule& rule :
       config.character_form_rules()) {
    if (rule.group() != kDigits) {
      continue;
    }
    for (int form = 0; form < std::ssize(kNumberForms); ++form) {
      if (rule.preedit_character_form() == kNumberForms[form].first &&
          rule.conversion_character_form() == kNumberForms[form].second) {
        return form;
      }
    }
    break;
  }
  return std::ssize(kNumberForms);
}

bool SetNumberForm(int form, config::Config& config) {
  if (form < 0 || form >= std::ssize(kNumberForms)) {
    return false;
  }
  for (config::Config::CharacterFormRule& rule :
       *config.mutable_character_form_rules()) {
    if (rule.group() == kDigits) {
      rule.set_preedit_character_form(kNumberForms[form].first);
      rule.set_conversion_character_form(kNumberForms[form].second);
      return true;
    }
  }
  return false;
}

}  // namespace mozc::floatype
