#include "floatype/config/text_config.h"

#include <sys/stat.h>

#ifdef __APPLE__
#include <spawn.h>
#include <sys/wait.h>
#endif  // __APPLE__

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/file_util.h"
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

absl::Status CheckRanges(const Message& message,
                         const TextFormat::ParseInfoTree* tree,
                         absl::Span<const Range> ranges) {
  const Descriptor& descriptor = *message.GetDescriptor();
  for (const Range& range : ranges) {
    const FieldDescriptor* field = descriptor.FindFieldByName(range.field);
    if (field == nullptr ||
        !message.GetReflection()->HasField(message, field)) {
      continue;
    }
    const int64_t value = IntegerValue(message, *field);
    if (value < range.min || value > range.max) {
      const int line = tree == nullptr ? -1 : tree->GetLocation(field, -1).line;
      return absl::InvalidArgumentError(absl::StrCat(
          line + 1, " 行目: ", range.field, " は ", range.min, "〜", range.max,
          " の範囲で書いてください（", value, " と書かれています）"));
    }
  }
  return absl::OkStatus();
}

struct State {
  absl::Mutex mutex;
  std::string path;
  bool report_errors = false;
  config::Config overlay;
  // The file as last read: modification time and size; -1 when missing.
  int64_t mtime_ns = -1;
  int64_t size = -1;
  absl::Time last_check = absl::InfinitePast();
  std::string last_error;
};

State& GetState() {
  static absl::NoDestructor<State> state;
  return *state;
}

void FileStamp(const std::string& path, int64_t& mtime_ns, int64_t& size) {
  struct stat info;
  if (stat(path.c_str(), &info) != 0) {
    mtime_ns = size = -1;
    return;
  }
#ifdef __APPLE__
  mtime_ns =
      info.st_mtimespec.tv_sec * 1'000'000'000LL + info.st_mtimespec.tv_nsec;
#else   // __APPLE__
  mtime_ns = info.st_mtim.tv_sec * 1'000'000'000LL + info.st_mtim.tv_nsec;
#endif  // __APPLE__
  size = info.st_size;
}

void Notify(const std::string& message) {
#ifdef __APPLE__
  // AppleScript string literal: quotes and backslashes would end it.
  const std::string text =
      absl::StrReplaceAll(message, {{"\\", "＼"}, {"\"", "”"}, {"\n", " "}});
  const std::string script =
      absl::StrCat("display notification \"", text,
                   "\" with title \"floatype の設定ファイルに誤りがあります\"");
  const char* argv[] = {"/usr/bin/osascript", "-e", script.c_str(), nullptr};
  pid_t pid;
  if (posix_spawn(&pid, argv[0], nullptr, nullptr,
                  const_cast<char* const*>(argv), nullptr) == 0) {
    // osascript returns at once after posting.
    waitpid(pid, nullptr, 0);
  }
#endif  // __APPLE__
}

}  // namespace

std::string DefaultTextConfigPath() {
  const char* home = std::getenv("HOME");
  return FileUtil::JoinPath(
      {home == nullptr ? "" : home, ".config", "floatype", "config.txtpb"});
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
  if (absl::Status status = CheckRanges(config, &tree, kConfigRanges);
      !status.ok()) {
    return status;
  }
  const FieldDescriptor* indicator =
      config::Config::descriptor()->FindFieldByName("indicator_style");
  if (absl::Status status =
          CheckRanges(config.indicator_style(),
                      tree.GetTreeForNested(indicator, -1), kIndicatorRanges);
      !status.ok()) {
    return status;
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

void RestoreOverlaidFields(const config::Config& overlay,
                           const config::Config& stored,
                           config::Config& config) {
  std::vector<const FieldDescriptor*> fields;
  overlay.GetReflection()->ListFields(overlay, &fields);
  config::Config copy = stored;
  config.GetReflection()->SwapFields(&config, &copy, fields);
}

void SetTextConfigPath(std::string path, bool report_errors) {
  State& state = GetState();
  {
    absl::MutexLock lock(state.mutex);
    state.path = std::move(path);
    state.report_errors = report_errors;
  }
  ReloadTextConfig();
}

void ReloadTextConfig() {
  State& state = GetState();
  std::string error;
  bool report = false;
  {
    absl::MutexLock lock(state.mutex);
    if (state.path.empty()) {
      return;
    }
    state.last_check = Clock::GetAbslTime();
    FileStamp(state.path, state.mtime_ns, state.size);
    if (state.mtime_ns < 0) {
      state.overlay.Clear();  // No file: the settings screen decides.
      state.last_error.clear();
      return;
    }
    absl::StatusOr<std::string> text = FileUtil::GetContents(state.path);
    absl::StatusOr<config::Config> parsed =
        text.ok() ? ParseTextConfig(*text) : text.status();
    if (parsed.ok()) {
      state.overlay = *std::move(parsed);
      state.last_error.clear();
      return;
    }
    // Keeps the last good settings (none when it was broken from the start).
    error = std::string(parsed.status().message());
    report = state.report_errors && error != state.last_error;
    state.last_error = error;
  }
  LOG(ERROR) << "floatype settings file: " << error;
  if (report) {
    Notify(error);
  }
}

bool TextConfigChanged() {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  const absl::Time now = Clock::GetAbslTime();
  if (state.path.empty() || now - state.last_check < absl::Seconds(1)) {
    return false;
  }
  state.last_check = now;
  int64_t mtime_ns;
  int64_t size;
  FileStamp(state.path, mtime_ns, size);
  return mtime_ns != state.mtime_ns || size != state.size;
}

config::Config TextConfigOverlay() {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  return state.overlay;
}

}  // namespace mozc::floatype
