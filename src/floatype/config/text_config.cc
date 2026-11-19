#include "floatype/config/text_config.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/protobuf/text_format.h"
#include "google/protobuf/io/tokenizer.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {
namespace {

using ::google::protobuf::Descriptor;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::Message;
using ::google::protobuf::Reflection;
using ::google::protobuf::TextFormat;

class LineErrors : public ::google::protobuf::io::ErrorCollector {
 public:
  void RecordError(int line, ::google::protobuf::io::ColumnNumber column,
                   absl::string_view message) override {
    if (first_.empty()) {
      first_ =
          absl::StrCat(line + 1, " 行目 ", column + 1, " 文字目: ", message);
    }
  }
  const std::string& first() const { return first_; }

 private:
  std::string first_;
};

struct Range {
  absl::string_view field;
  int64_t min;
  int64_t max;
};

// The numbers whose type alone does not keep them sensible (spec section 5).
constexpr Range kConfigRanges[] = {
    {"live_conversion_delay_millisec", 0, 2000},
    {"live_conversion_min_chars", 1, 10},
    {"zenz_delay_millisec", 0, 5000},
    {"zenz_timeout_millisec", 50, 5000},
    {"live_conversion_waits_for_zenz_millisec", 0, 1000},
    {"candidate_font_scale_percent", 50, 300},
};
constexpr Range kCandidateRanges[] = {
    {"text_color", 0, 0xffffff},
    {"focused_background_color", 0, 0xffffff},
    {"focused_border_color", 0, 0xffffff},
    {"border_color", 0, 0xffffff},
};
constexpr Range kRubyRanges[] = {
    {"font_size", 6, 72},
    {"text_color", 0, 0xffffff},
    {"background_color", 0, 0xffffff},
};
constexpr Range kIndicatorRanges[] = {
    {"reading_color", 0, 0xffffff},
    {"mozc_color", 0, 0xffffff},
    {"zenz_pending_color", 0, 0xffffff},
    {"zenz_color", 0, 0xffffff},
    {"text_color", 0, 0xffffff},
    {"width", 8, 400},
    {"height", 8, 200},
    {"font_size", 6, 72},
    {"corner_radius", 0, 100},
    {"opacity_percent", 0, 100},
    {"gap", 0, 200},
    {"offset_x", -1000, 1000},
    {"offset_y", -1000, 1000},
};

int64_t IntegerValue(const Message& message, const FieldDescriptor& field) {
  const Reflection& reflection = *message.GetReflection();
  switch (field.cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      return reflection.GetInt32(message, &field);
    case FieldDescriptor::CPPTYPE_UINT32:
      return reflection.GetUInt32(message, &field);
    case FieldDescriptor::CPPTYPE_INT64:
      return reflection.GetInt64(message, &field);
    default:
      return static_cast<int64_t>(reflection.GetUInt64(message, &field));
  }
}

absl::Span<const Range> RangesOf(const Descriptor* descriptor) {
  if (descriptor == config::Config::descriptor()) {
    return kConfigRanges;
  }
  if (descriptor == config::Config::CandidateStyle::descriptor()) {
    return kCandidateRanges;
  }
  if (descriptor == config::Config::RubyStyle::descriptor()) {
    return kRubyRanges;
  }
  if (descriptor == config::Config::IndicatorStyle::descriptor()) {
    return kIndicatorRanges;
  }
  return {};
}

absl::Status CheckRanges(const Message& message,
                         const TextFormat::ParseInfoTree* tree) {
  const Descriptor& descriptor = *message.GetDescriptor();
  for (const Range& range : RangesOf(&descriptor)) {
    const FieldDescriptor* field = descriptor.FindFieldByName(range.field);
    if (field == nullptr ||
        !message.GetReflection()->HasField(message, field)) {
      continue;
    }
    const int line = tree == nullptr ? -1 : tree->GetLocation(field, -1).line;
    if (absl::Status status =
            CheckConfigRange(*field, IntegerValue(message, *field), line + 1);
        !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status CheckConfigRange(const FieldDescriptor& field, int64_t value,
                              int line) {
  for (const Range& range : RangesOf(field.containing_type())) {
    if (range.field == field.name() &&
        (value < range.min || value > range.max)) {
      return absl::InvalidArgumentError(absl::StrCat(
          line, " 行目: ", range.field, " は ", range.min, "〜", range.max,
          " の範囲で書いてください（", value, " と書かれています）"));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<config::Config> ParseTextConfig(absl::string_view text) {
  config::Config config;
  TextFormat::Parser parser;
  LineErrors errors;
  TextFormat::ParseInfoTree tree;
  parser.RecordErrorsTo(&errors);
  parser.WriteLocationsTo(&tree);
  if (!parser.ParseFromString(text, &config)) {
    return absl::InvalidArgumentError(errors.first());
  }
  if (absl::Status status = CheckRanges(config, &tree); !status.ok()) {
    return status;
  }
  for (const absl::string_view name :
       {"candidate_style", "ruby_style", "indicator_style"}) {
    const FieldDescriptor* field =
        config::Config::descriptor()->FindFieldByName(name);
    if (absl::Status status = CheckRanges(
            config.GetReflection()->GetMessage(config, field),
            tree.GetTreeForNested(field, -1));
        !status.ok()) {
      return status;
    }
  }
  return config;
}

void ApplyOverlay(const config::Config& overlay, config::Config& config) {
  std::vector<const FieldDescriptor*> fields;
  overlay.GetReflection()->ListFields(overlay, &fields);
  for (const FieldDescriptor* field : fields) {
    if (field->is_repeated()) {
      config.GetReflection()->ClearField(&config, field);
    }
  }
  config.MergeFrom(overlay);
}

}  // namespace mozc::floatype
