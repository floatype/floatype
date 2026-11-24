#include "floatype/config/toml_config.h"

// Errors come back in toml::parse_result rather than as exceptions.
#define TOML_EXCEPTIONS 0
#include <toml.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/protobuf/descriptor.h"
#include "base/protobuf/message.h"
#include "base/protobuf/text_format.h"
#include "floatype/config/text_config.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {
namespace {

using ::google::protobuf::Descriptor;
using ::google::protobuf::EnumValueDescriptor;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::Message;
using ::google::protobuf::Reflection;
using ::google::protobuf::TextFormat;

absl::Status LineError(int line, absl::string_view message) {
  return absl::InvalidArgumentError(absl::StrCat(line, " 行目: ", message));
}

int BeginLine(const toml::node& node) {
  return static_cast<int>(node.source().begin.line);
}

int EndLine(const toml::node& node) {
  return static_cast<int>(node.source().end.line);
}

// The settings file's own bookkeeping: never written, ignored when read.
bool Skipped(const FieldDescriptor& field) {
  return field.message_type() == config::GeneralConfig::descriptor();
}

bool IsMessage(const FieldDescriptor& field) {
  return field.cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE;
}

// Sets `field` of `message` to `node`, or adds `node` to it if repeated.
absl::Status ReadValue(const toml::node& node, const FieldDescriptor& field,
                       Message& message) {
  const Reflection& reflection = *message.GetReflection();
  const bool add = field.is_repeated();
  const int line = BeginLine(node);
  switch (field.cpp_type()) {
    case FieldDescriptor::CPPTYPE_BOOL: {
      const toml::value<bool>* value = node.as_boolean();
      if (value == nullptr) {
        return LineError(line, absl::StrCat(field.name(),
                                            " には true か false を書いてください"));
      }
      add ? reflection.AddBool(&message, &field, value->get())
          : reflection.SetBool(&message, &field, value->get());
      return absl::OkStatus();
    }
    case FieldDescriptor::CPPTYPE_INT32:
    case FieldDescriptor::CPPTYPE_UINT32:
    case FieldDescriptor::CPPTYPE_INT64:
    case FieldDescriptor::CPPTYPE_UINT64: {
      const toml::value<int64_t>* value = node.as_integer();
      if (value == nullptr) {
        return LineError(line,
                         absl::StrCat(field.name(), " には整数を書いてください"));
      }
      const int64_t number = value->get();
      const auto [min, max] = [&]() -> std::pair<int64_t, int64_t> {
        switch (field.cpp_type()) {
          case FieldDescriptor::CPPTYPE_INT32:
            return {std::numeric_limits<int32_t>::min(),
                    std::numeric_limits<int32_t>::max()};
          case FieldDescriptor::CPPTYPE_UINT32:
            return {0, std::numeric_limits<uint32_t>::max()};
          case FieldDescriptor::CPPTYPE_INT64:
            return {std::numeric_limits<int64_t>::min(),
                    std::numeric_limits<int64_t>::max()};
          default:
            return {0, std::numeric_limits<int64_t>::max()};
        }
      }();
      if (number < min || number > max) {
        return LineError(
            line, absl::StrCat(field.name(), " は ", min, "〜", max,
                               " の範囲で書いてください（", number,
                               " と書かれています）"));
      }
      if (absl::Status status = CheckConfigRange(field, number, line);
          !status.ok()) {
        return status;
      }
      switch (field.cpp_type()) {
        case FieldDescriptor::CPPTYPE_INT32:
          add ? reflection.AddInt32(&message, &field, number)
              : reflection.SetInt32(&message, &field, number);
          break;
        case FieldDescriptor::CPPTYPE_UINT32:
          add ? reflection.AddUInt32(&message, &field, number)
              : reflection.SetUInt32(&message, &field, number);
          break;
        case FieldDescriptor::CPPTYPE_INT64:
          add ? reflection.AddInt64(&message, &field, number)
              : reflection.SetInt64(&message, &field, number);
          break;
        default:
          add ? reflection.AddUInt64(&message, &field, number)
              : reflection.SetUInt64(&message, &field, number);
          break;
      }
      return absl::OkStatus();
    }
    case FieldDescriptor::CPPTYPE_ENUM: {
      const toml::value<std::string>* value = node.as_string();
      const EnumValueDescriptor* enum_value =
          value == nullptr ? nullptr
                           : field.enum_type()->FindValueByName(value->get());
      if (enum_value == nullptr) {
        std::vector<std::string> names;
        for (int i = 0; i < field.enum_type()->value_count(); ++i) {
          names.push_back(
              absl::StrCat("\"", field.enum_type()->value(i)->name(), "\""));
        }
        return LineError(line, absl::StrCat(field.name(),
                                            " には次のどれかを書いてください: ",
                                            absl::StrJoin(names, ", ")));
      }
      add ? reflection.AddEnum(&message, &field, enum_value)
          : reflection.SetEnum(&message, &field, enum_value);
      return absl::OkStatus();
    }
    case FieldDescriptor::CPPTYPE_STRING: {
      const toml::value<std::string>* value = node.as_string();
      if (value == nullptr) {
        return LineError(
            line, absl::StrCat(field.name(), " には文字列を書いてください"));
      }
      add ? reflection.AddString(&message, &field, value->get())
          : reflection.SetString(&message, &field, value->get());
      return absl::OkStatus();
    }
    default:
      // shortcut: config.proto has no float or double, add them when it does.
      return LineError(line,
                       absl::StrCat(field.name(), " はこのファイルでは書けません"));
  }
}

absl::Status ReadTable(const toml::table& table, Message& message) {
  const Descriptor& descriptor = *message.GetDescriptor();
  const Reflection& reflection = *message.GetReflection();
  for (auto&& [key, node] : table) {
    const int line = static_cast<int>(key.source().begin.line);
    const FieldDescriptor* field = descriptor.FindFieldByName(key.str());
    if (field == nullptr) {
      // A setting removed since (now a reserved name) is passed over, so a
      // file written before still reads.
      if (descriptor.IsReservedName(key.str())) {
        continue;
      }
      return LineError(line,
                       absl::StrCat(key.str(), " という設定はありません"));
    }
    if (Skipped(*field)) {
      continue;
    }
    if (field->is_repeated()) {
      const std::string shape =
          IsMessage(*field) ? absl::StrCat("[[", field->name(), "]] の表")
                            : "配列 [...]";
      const toml::array* array = node.as_array();
      if (array == nullptr) {
        return LineError(line,
                         absl::StrCat(field->name(), " は", shape,
                                      "で書いてください"));
      }
      for (const toml::node& element : *array) {
        if (!IsMessage(*field)) {
          if (absl::Status status = ReadValue(element, *field, message);
              !status.ok()) {
            return status;
          }
          continue;
        }
        const toml::table* element_table = element.as_table();
        if (element_table == nullptr) {
          return LineError(BeginLine(element),
                           absl::StrCat(field->name(), " は", shape,
                                        "で書いてください"));
        }
        if (absl::Status status = ReadTable(
                *element_table, *reflection.AddMessage(&message, field));
            !status.ok()) {
          return status;
        }
      }
    } else if (IsMessage(*field)) {
      const toml::table* nested = node.as_table();
      if (nested == nullptr) {
        return LineError(line, absl::StrCat(field->name(), " は [",
                                            field->name(),
                                            "] の表で書いてください"));
      }
      if (absl::Status status =
              ReadTable(*nested, *reflection.MutableMessage(&message, field));
          !status.ok()) {
        return status;
      }
    } else if (absl::Status status = ReadValue(node, *field, message);
               !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

struct Document {
  toml::table table;
  config::Config config;
};

absl::StatusOr<Document> Parse(absl::string_view text) {
  toml::parse_result result = toml::parse(text);
  if (!result) {
    return LineError(static_cast<int>(result.error().source().begin.line),
                     absl::StrCat("TOML として読めません（",
                                  result.error().description(), "）"));
  }
  Document document{std::move(result).table(), config::Config()};
  if (absl::Status status = ReadTable(document.table, document.config);
      !status.ok()) {
    return status;
  }
  return document;
}

std::string Print(const Message& message, const FieldDescriptor& field,
                  int index) {
  std::string text;
  TextFormat::PrintFieldValueToString(message, &field, index, &text);
  return text;
}

bool SameMessage(const Message& a, const Message& b);

// Whether `a` and `b` hold the same value of `field`, an unset field holding
// its default.
bool SameField(const Message& a, const Message& b,
               const FieldDescriptor& field) {
  if (field.is_repeated()) {
    const int size = a.GetReflection()->FieldSize(a, &field);
    if (size != b.GetReflection()->FieldSize(b, &field)) {
      return false;
    }
    for (int i = 0; i < size; ++i) {
      if (IsMessage(field)
              ? !SameMessage(
                    a.GetReflection()->GetRepeatedMessage(a, &field, i),
                    b.GetReflection()->GetRepeatedMessage(b, &field, i))
              : Print(a, field, i) != Print(b, field, i)) {
        return false;
      }
    }
    return true;
  }
  if (IsMessage(field)) {
    return SameMessage(a.GetReflection()->GetMessage(a, &field),
                       b.GetReflection()->GetMessage(b, &field));
  }
  // A color without a default stays unset until chosen, so black, its zero,
  // is a choice and differs from unset.
  if (!field.has_default_value() && absl::EndsWith(field.name(), "_color") &&
      a.GetReflection()->HasField(a, &field) !=
          b.GetReflection()->HasField(b, &field)) {
    return false;
  }
  return Print(a, field, -1) == Print(b, field, -1);
}

bool SameMessage(const Message& a, const Message& b) {
  const Descriptor& descriptor = *a.GetDescriptor();
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const FieldDescriptor& field = *descriptor.field(i);
    if (!Skipped(field) && !SameField(a, b, field)) {
      return false;
    }
  }
  return true;
}

bool InFile(const Message& file, const FieldDescriptor& field) {
  const Reflection& reflection = *file.GetReflection();
  return field.is_repeated() ? reflection.FieldSize(file, &field) > 0
                             : reflection.HasField(file, &field);
}

// Whether `file` (the fields a file sets) read over `defaults` gives `config`.
bool Gives(const Message& file, const Message& defaults,
           const Message& config) {
  const Descriptor& descriptor = *config.GetDescriptor();
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const FieldDescriptor& field = *descriptor.field(i);
    if (Skipped(field)) {
      continue;
    }
    if (IsMessage(field) && !field.is_repeated()) {
      if (!Gives(file.GetReflection()->GetMessage(file, &field),
                 defaults.GetReflection()->GetMessage(defaults, &field),
                 config.GetReflection()->GetMessage(config, &field))) {
        return false;
      }
    } else if (!SameField(InFile(file, field) ? file : defaults, config,
                          field)) {
      return false;
    }
  }
  return true;
}

// A TOML basic string, multi-line ones keeping their newlines and tabs.
std::string Quote(absl::string_view text, bool multiline) {
  std::string quoted;
  for (const char c : text) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      absl::StrAppend(&quoted, "\\", absl::string_view(&c, 1));
    } else if (multiline && (c == '\n' || c == '\t')) {
      quoted += c;
    } else if (byte < 0x20 || byte == 0x7f) {
      absl::StrAppendFormat(&quoted, "\\u%04X", byte);
    } else {
      quoted += c;
    }
  }
  return multiline ? absl::StrCat("\"\"\"\n", quoted, "\"\"\"")
                   : absl::StrCat("\"", quoted, "\"");
}

// Multi-line text (custom_roman_table and the like) as it reads best: a
// literal string unless something in it needs an escape.
std::string MultilineString(absl::string_view text) {
  const bool literal =
      !absl::StrContains(text, "'''") && !absl::EndsWith(text, "'") &&
      std::all_of(text.begin(), text.end(), [](const char c) {
        const unsigned char byte = static_cast<unsigned char>(c);
        return c == '\n' || c == '\t' || (byte >= 0x20 && byte != 0x7f);
      });
  return literal ? absl::StrCat("'''\n", text, "'''") : Quote(text, true);
}

std::string Value(const Message& message, const FieldDescriptor& field,
                  int index) {
  switch (field.cpp_type()) {
    case FieldDescriptor::CPPTYPE_ENUM:
      return absl::StrCat("\"", Print(message, field, index), "\"");
    case FieldDescriptor::CPPTYPE_STRING: {
      const Reflection& reflection = *message.GetReflection();
      const std::string text =
          field.is_repeated()
              ? reflection.GetRepeatedString(message, &field, index)
              : reflection.GetString(message, &field);
      return field.type() == FieldDescriptor::TYPE_BYTES
                 ? MultilineString(text)
                 : Quote(text, false);
    }
    default: {  // Booleans and integers print as TOML writes them.
      const std::string text = Print(message, field, index);
      uint64_t color;
      if (absl::EndsWith(field.name(), "_color") &&
          absl::SimpleAtoi(text, &color)) {
        return absl::StrFormat("0x%06x", color);
      }
      return text;
    }
  }
}

std::string FieldValue(const Message& message, const FieldDescriptor& field) {
  if (!field.is_repeated()) {
    return Value(message, field, -1);
  }
  std::vector<std::string> values;
  for (int i = 0; i < message.GetReflection()->FieldSize(message, &field);
       ++i) {
    values.push_back(Value(message, field, i));
  }
  return absl::StrCat("[", absl::StrJoin(values, ", "), "]");
}

std::string JoinPath(absl::string_view path, absl::string_view name) {
  return path.empty() ? std::string(name) : absl::StrCat(path, ".", name);
}

// The body of the table at `path` holding `message`: its keys, then its
// tables. With `defaults`, only the fields that differ from them; without,
// the fields set.
std::string RenderTable(const Message& message, const Message* defaults,
                        absl::string_view path) {
  std::string keys;
  std::string tables;
  const Descriptor& descriptor = *message.GetDescriptor();
  const Reflection& reflection = *message.GetReflection();
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const FieldDescriptor& field = *descriptor.field(i);
    if (Skipped(field) || (defaults != nullptr
                               ? SameField(message, *defaults, field)
                               : !InFile(message, field))) {
      continue;
    }
    const std::string name = JoinPath(path, field.name());
    if (!IsMessage(field)) {
      absl::StrAppend(&keys, field.name(), " = ", FieldValue(message, field),
                      "\n");
    } else if (!field.is_repeated()) {
      absl::StrAppend(
          &tables, "\n[", name, "]\n",
          RenderTable(reflection.GetMessage(message, &field),
                      defaults == nullptr
                          ? nullptr
                          : &defaults->GetReflection()->GetMessage(*defaults,
                                                                   &field),
                      name));
    } else {
      for (int j = 0; j < reflection.FieldSize(message, &field); ++j) {
        absl::StrAppend(
            &tables, "\n[[", name, "]]\n",
            RenderTable(reflection.GetRepeatedMessage(message, &field, j),
                        nullptr, name));
      }
    }
  }
  return keys + tables;
}

enum class Kind {
  kValue,       // key = value, inline tables and arrays included.
  kHeader,      // [table]
  kDotted,      // A table made by dotted keys: table.key = value.
  kTableArray,  // [[table]]
};

// Edits to the file's text, made at once by Apply().
class Editor {
 public:
  explicit Editor(absl::string_view text) : text_(text) {
    line_starts_.push_back(0);
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\n') {
        line_starts_.push_back(i + 1);
      }
    }
  }

  // Where `line` (from 1) begins; the end of the text past the last line.
  size_t LineStart(int line) const {
    return line >= 1 && line <= static_cast<int>(line_starts_.size())
               ? line_starts_[line - 1]
               : text_.size();
  }

  Kind KindOf(const toml::node& node) const {
    if (const toml::table* table = node.as_table()) {
      if (table->is_inline()) {
        return Kind::kValue;
      }
      const absl::string_view line = text_.substr(LineStart(BeginLine(node)));
      const size_t first = line.find_first_not_of(" \t");
      return first != absl::string_view::npos && line[first] == '['
                 ? Kind::kHeader
                 : Kind::kDotted;
    }
    const toml::array* array = node.as_array();
    if (array != nullptr && !array->empty() && array->front().is_table() &&
        !array->front().as_table()->is_inline()) {
      return Kind::kTableArray;
    }
    return Kind::kValue;
  }

  // The last line of the keys directly in `table` (not in its [sub]tables),
  // or 0 without any.
  int LastKeyLine(const toml::table& table) const {
    int last = 0;
    for (auto&& [key, node] : table) {
      switch (KindOf(node)) {
        case Kind::kValue:
          last = std::max(last, EndLine(node));
          break;
        case Kind::kDotted:
          last = std::max(last, LastKeyLine(*node.as_table()));
          break;
        default:
          break;
      }
    }
    return last;
  }

  // The first line of the first table header, or 0 without any.
  int FirstHeaderLine(const toml::table& root) const {
    int first = 0;
    for (auto&& [key, node] : root) {
      const Kind kind = KindOf(node);
      if ((kind == Kind::kHeader || kind == Kind::kTableArray) &&
          (first == 0 || BeginLine(node) < first)) {
        first = BeginLine(node);
      }
    }
    return first;
  }

  bool IsCommentLine(int line) const {
    const absl::string_view text = LineText(line);
    const size_t first = text.find_first_not_of(" \t");
    return first != absl::string_view::npos && text[first] == '#';
  }

  // Removes the lines that write `node`.
  void Remove(const toml::node& node) {
    switch (KindOf(node)) {
      case Kind::kValue:
        RemoveLines(BeginLine(node), EndLine(node));
        break;
      case Kind::kDotted:
        for (auto&& [key, child] : *node.as_table()) {
          Remove(child);
        }
        break;
      case Kind::kHeader:
        RemoveBlock(*node.as_table());
        break;
      case Kind::kTableArray:
        for (const toml::node& element : *node.as_array()) {
          RemoveBlock(*element.as_table());
        }
        break;
    }
  }

  void Replace(const toml::node& node, std::string text) {
    edits_.push_back({Offset(node.source().begin), Offset(node.source().end),
                      std::move(text), Edit::kChange});
  }

  // Inserts whole lines at `offset`, the start of a line or the end of the
  // text.
  void Insert(size_t offset, std::string text) {
    edits_.push_back({offset, offset, std::move(text), Edit::kInsert});
  }

  size_t End() const { return text_.size(); }

  // Adds a table at the end, set apart by a blank line.
  void Append(std::string text) {
    edits_.push_back({End(), End(), std::move(text), Edit::kAppend});
  }

  absl::StatusOr<std::string> Apply() && {
    std::stable_sort(edits_.begin(), edits_.end(),
                     [](const Edit& a, const Edit& b) {
                       return a.begin != b.begin ? a.begin < b.begin
                                                 : a.kind < b.kind;
                     });
    std::string updated;
    size_t done = 0;
    for (const Edit& edit : edits_) {
      if (edit.begin < done) {
        return absl::InternalError("config.toml: edits overlap");
      }
      absl::StrAppend(&updated, text_.substr(done, edit.begin - done));
      if (edit.kind != Edit::kChange && !updated.empty() &&
          updated.back() != '\n') {
        updated += '\n';  // The text ended without a newline.
      }
      if (edit.kind == Edit::kAppend && !updated.empty() &&
          !absl::EndsWith(updated, "\n\n")) {
        updated += '\n';
      }
      absl::StrAppend(&updated, edit.text);
      done = edit.end;
    }
    absl::StrAppend(&updated, text_.substr(done));
    return updated;
  }

 private:
  struct Edit {
    // The order of edits at one place: keys go into the table that ends
    // there before tables are added after it, and both before a removal
    // that starts there.
    enum Kind { kInsert, kAppend, kChange };
    size_t begin;
    size_t end;
    std::string text;
    Kind kind;
  };

  // Columns count characters, not bytes.
  size_t Offset(const toml::source_position& position) const {
    size_t offset = LineStart(static_cast<int>(position.line));
    for (uint32_t column = 1;
         column < position.column && offset < text_.size(); ++column) {
      ++offset;
      while (offset < text_.size() &&
             (static_cast<unsigned char>(text_[offset]) & 0xc0) == 0x80) {
        ++offset;
      }
    }
    return offset;
  }

  absl::string_view LineText(int line) const {
    return text_.substr(LineStart(line), LineStart(line + 1) - LineStart(line));
  }

  void RemoveLines(int first, int last) {
    edits_.push_back({LineStart(first), LineStart(last + 1), "", Edit::kChange});
  }

  // Removes a table under a [header] or [[header]], with its subtables.
  void RemoveBlock(const toml::table& table) {
    const int header = BeginLine(table);
    // With the blank line that set it apart.
    const bool blank_above =
        header > 1 &&
        LineText(header - 1).find_first_not_of(" \t\r\n") ==
            absl::string_view::npos;
    RemoveLines(blank_above ? header - 1 : header,
                std::max(header, LastKeyLine(table)));
    for (auto&& [key, child] : table) {
      const Kind kind = KindOf(child);
      if (kind == Kind::kHeader || kind == Kind::kTableArray) {
        Remove(child);
      }
    }
  }

  absl::string_view text_;
  std::vector<size_t> line_starts_;
  std::vector<Edit> edits_;
};

// floatype: tidies what the edits leave behind in `text`, which reads as
// `root`. A [header] table with no keys goes, unless a comment sits on its
// line, right above it, or below it; blank lines run to one, with none at the
// start or the end. Lines inside a multi-line value ('''...''') stay.
std::string Tidy(absl::string_view text, const toml::table& root) {
  std::vector<absl::string_view> lines = absl::StrSplit(text, '\n');
  const bool newline_at_end = absl::EndsWith(text, "\n");
  if (newline_at_end) {
    lines.pop_back();
  }
  const int count = static_cast<int>(lines.size());
  const Editor editor(text);
  auto blank = [&](int line) {
    return line >= 1 && line <= count &&
           lines[line - 1].find_first_not_of(" \t\r") ==
               absl::string_view::npos;
  };
  auto comment = [&](int line) {
    return line >= 1 && line <= count && editor.IsCommentLine(line);
  };
  std::vector<bool> inside(count + 1, false);
  std::vector<bool> dropped(count + 1, false);
  std::function<void(const toml::node&)> walk = [&](const toml::node& node) {
    const Kind kind = editor.KindOf(node);
    switch (kind) {
      case Kind::kHeader:
      case Kind::kDotted: {
        const toml::table& table = *node.as_table();
        for (auto&& [key, child] : table) {
          walk(child);
        }
        const int header = BeginLine(node);
        if (kind == Kind::kHeader && table.empty() && header <= count) {
          int below = header + 1;
          while (blank(below)) {
            ++below;
          }
          dropped[header] =
              lines[header - 1].find('#') == absl::string_view::npos &&
              !comment(header - 1) && !comment(below);
        }
        break;
      }
      case Kind::kTableArray:
        for (const toml::node& element : *node.as_array()) {
          for (auto&& [key, child] : *element.as_table()) {
            walk(child);
          }
        }
        break;
      case Kind::kValue:
        for (int line = BeginLine(node) + 1;
             line <= std::min(EndLine(node), count); ++line) {
          inside[line] = true;
        }
        break;
    }
  };
  for (auto&& [key, node] : root) {
    walk(node);
  }

  std::vector<absl::string_view> kept;
  bool last_blank = true;  // Drops blank lines at the start.
  for (int line = 1; line <= count; ++line) {
    const bool is_blank = !inside[line] && blank(line);
    if (dropped[line] || (is_blank && last_blank)) {
      continue;
    }
    kept.push_back(lines[line - 1]);
    last_blank = is_blank;
  }
  if (last_blank && !kept.empty()) {
    kept.pop_back();
  }
  std::string tidied = absl::StrJoin(kept, "\n");
  if (newline_at_end && !tidied.empty()) {
    tidied += '\n';
  }
  return tidied;
}

// Edits `table`, the root or a table under its own [path] header, which sets
// the fields in `file`, so that read over `defaults` it gives `config`.
// shortcut: a table that only [path.sub] headers make is taken for its own
// [path]; no message in config.proto holds another, so such a file does not
// parse. Tell them apart if one ever does.
void UpdateTable(const toml::table& table, absl::string_view path,
                 const Message& file, const Message& defaults,
                 const Message& config, Editor& editor) {
  const Descriptor& descriptor = *config.GetDescriptor();
  const Reflection& reflection = *config.GetReflection();
  std::string inserts;
  for (int i = 0; i < descriptor.field_count(); ++i) {
    const FieldDescriptor& field = *descriptor.field(i);
    if (IsMessage(field) ||
        SameField(InFile(file, field) ? file : defaults, config, field)) {
      continue;
    }
    const toml::node* node = table.get(field.name());
    if (SameField(defaults, config, field)) {
      if (node != nullptr) {
        editor.Remove(*node);
      }
    } else if (node != nullptr) {
      editor.Replace(*node, FieldValue(config, field));
    } else {
      absl::StrAppend(&inserts, field.name(), " = ",
                      FieldValue(config, field), "\n");
    }
  }
  if (!inserts.empty()) {
    if (const int last = editor.LastKeyLine(table); last > 0) {
      editor.Insert(editor.LineStart(last + 1), std::move(inserts));
    } else if (!path.empty()) {
      editor.Insert(editor.LineStart(BeginLine(table) + 1),
                    std::move(inserts));
    } else if (int header = editor.FirstHeaderLine(table); header > 0) {
      // Above the comments that go with the first table.
      while (header > 1 && editor.IsCommentLine(header - 1)) {
        --header;
      }
      editor.Insert(editor.LineStart(header), absl::StrCat(inserts, "\n"));
    } else {
      editor.Insert(editor.End(), std::move(inserts));
    }
  }

  for (int i = 0; i < descriptor.field_count(); ++i) {
    const FieldDescriptor& field = *descriptor.field(i);
    if (!IsMessage(field) || Skipped(field)) {
      continue;
    }
    const std::string name = JoinPath(path, field.name());
    const toml::node* node = table.get(field.name());
    if (field.is_repeated()) {
      if (SameField(InFile(file, field) ? file : defaults, config, field)) {
        continue;
      }
      if (node != nullptr) {
        editor.Remove(*node);
      }
      if (!SameField(defaults, config, field)) {
        for (int j = 0; j < reflection.FieldSize(config, &field); ++j) {
          editor.Append(absl::StrCat(
              "[[", name, "]]\n",
              RenderTable(reflection.GetRepeatedMessage(config, &field, j),
                          nullptr, name)));
        }
      }
      continue;
    }
    const Message& nested_file =
        file.GetReflection()->GetMessage(file, &field);
    const Message& nested_defaults =
        defaults.GetReflection()->GetMessage(defaults, &field);
    const Message& nested_config = reflection.GetMessage(config, &field);
    if (Gives(nested_file, nested_defaults, nested_config)) {
      continue;
    }
    if (node != nullptr && editor.KindOf(*node) == Kind::kHeader) {
      UpdateTable(*node->as_table(), name, nested_file, nested_defaults,
                  nested_config, editor);
      continue;
    }
    // Missing, or written inline or with dotted keys: written anew.
    if (node != nullptr) {
      editor.Remove(*node);
    }
    if (!SameMessage(nested_defaults, nested_config)) {
      editor.Append(absl::StrCat(
          "[", name, "]\n",
          RenderTable(nested_config, &nested_defaults, name)));
    }
  }
}

}  // namespace

absl::StatusOr<config::Config> ParseTomlConfig(absl::string_view text) {
  absl::StatusOr<Document> document = Parse(text);
  if (!document.ok()) {
    return document.status();
  }
  return std::move(document->config);
}

absl::StatusOr<std::string> UpdateTomlConfig(absl::string_view text,
                                             const config::Config& config,
                                             const config::Config& defaults) {
  const absl::StatusOr<Document> document = Parse(text);
  if (!document.ok()) {
    return document.status();
  }
  Editor editor(text);
  UpdateTable(document->table, "", document->config, defaults, config, editor);
  absl::StatusOr<std::string> updated = std::move(editor).Apply();
  if (!updated.ok()) {
    return updated;
  }
  // Guards the file against a slip in the edits above.
  const absl::StatusOr<Document> check = Parse(*updated);
  if (!check.ok()) {
    return absl::InternalError(absl::StrCat(
        "config.toml: the updated file does not read: ", check.status()));
  }
  if (!Gives(check->config, defaults, config)) {
    return absl::InternalError(
        "config.toml: the updated file does not give the settings");
  }
  if (*updated == text) {
    return updated;
  }
  // The tidying never fails a save: a slip in it keeps the edits as they are.
  std::string tidied = Tidy(*updated, check->table);
  if (const absl::StatusOr<Document> again = Parse(tidied);
      again.ok() && Gives(again->config, defaults, config)) {
    return tidied;
  }
  return updated;
}

}  // namespace mozc::floatype
