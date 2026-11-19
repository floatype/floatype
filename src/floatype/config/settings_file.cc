#include "floatype/config/settings_file.h"

#include <limits.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <spawn.h>
#include <sys/wait.h>
#endif  // __APPLE__

#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>

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
#include "floatype/config/text_config.h"
#include "floatype/config/toml_config.h"
#include "protocol/config.pb.h"

namespace mozc::floatype {
namespace {

constexpr absl::string_view kErrorTitle =
    "floatype の設定ファイルに誤りがあります";

struct State {
  absl::Mutex mutex;
  std::string path;
  bool report_errors = false;
  bool writable = false;
  // The file as last read or written.
  FileStamp stamp;
  absl::Time last_check = absl::InfinitePast();
  std::string last_error;
  bool loaded = false;
  std::string user_files_dir;
  bool report_user_file_errors = false;
};

State& GetState() {
  static absl::NoDestructor<State> state;
  return *state;
}

void Notify(absl::string_view title, absl::string_view message) {
#ifdef __APPLE__
  // AppleScript string literals: quotes and backslashes would end them.
  const auto quote = [](absl::string_view text) {
    return absl::StrReplaceAll(text, {{"\\", "＼"}, {"\"", "”"}, {"\n", " "}});
  };
  const std::string script =
      absl::StrCat("display notification \"", quote(message),
                   "\" with title \"", quote(title), "\"");
  const char* argv[] = {"/usr/bin/osascript", "-e", script.c_str(), nullptr};
  pid_t pid;
  if (posix_spawn(&pid, argv[0], nullptr, nullptr,
                  const_cast<char* const*>(argv), nullptr) == 0) {
    // Reaped aside: this runs while the user types.
    std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();
  }
#endif  // __APPLE__
}

// The settings in `text` over `defaults`.
absl::StatusOr<config::Config> SettingsOf(absl::string_view text,
                                          const config::Config& defaults) {
  absl::StatusOr<config::Config> file = ParseTomlConfig(text);
  if (!file.ok()) {
    return file.status();
  }
  config::Config config = defaults;
  ApplyOverlay(*file, config);
  return config;
}

// `stored` with the old config.txtpb next to `path` applied, if it reads.
config::Config Migrate(const std::string& path, const config::Config& stored) {
  config::Config config = stored;
  const std::string old_path =
      FileUtil::JoinPath(FileUtil::Dirname(path), "config.txtpb");
  if (absl::StatusOr<std::string> text = FileUtil::GetContents(old_path);
      text.ok()) {
    if (absl::StatusOr<config::Config> overlay = ParseTextConfig(*text);
        overlay.ok()) {
      ApplyOverlay(*overlay, config);
    } else {
      LOG(WARNING) << old_path << " is left out: " << overlay.status();
    }
  }
  return config;
}

}  // namespace

FileStamp StampOf(const std::string& path) {
  struct stat info;
  if (stat(path.c_str(), &info) != 0) {
    return FileStamp();
  }
#ifdef __APPLE__
  const int64_t mtime_ns =
      info.st_mtimespec.tv_sec * 1'000'000'000LL + info.st_mtimespec.tv_nsec;
#else   // __APPLE__
  const int64_t mtime_ns =
      info.st_mtim.tv_sec * 1'000'000'000LL + info.st_mtim.tv_nsec;
#endif  // __APPLE__
  return {mtime_ns, static_cast<int64_t>(info.st_size),
          static_cast<int64_t>(info.st_ino), static_cast<int64_t>(info.st_dev)};
}

absl::Status WriteAtomically(std::string path, absl::string_view text) {
  if (char resolved[PATH_MAX]; realpath(path.c_str(), resolved) != nullptr) {
    path = resolved;
  }
  const std::string dir = FileUtil::Dirname(path);
  for (const std::string& d : {FileUtil::Dirname(dir), dir}) {
    if (absl::Status status = FileUtil::CreateDirectory(d); !status.ok()) {
      return status;
    }
  }
  const std::string temp = absl::StrCat(path, ".tmp");
  if (absl::Status status = FileUtil::SetContents(temp, text); !status.ok()) {
    return status;
  }
  return FileUtil::AtomicRename(temp, path);
}

std::string DefaultSettingsPath() {
  const char* home = std::getenv("HOME");
  return FileUtil::JoinPath(
      {home == nullptr ? "" : home, ".config", "floatype", "config.toml"});
}

void SetSettingsPath(std::string path, bool report_errors, bool writable) {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  state.path = std::move(path);
  state.report_errors = report_errors;
  state.writable = writable;
  state.stamp = FileStamp();
  state.last_check = absl::InfinitePast();
  state.last_error.clear();
  state.loaded = false;
  state.user_files_dir =
      state.path.empty() ? "" : FileUtil::Dirname(state.path);
  state.report_user_file_errors = report_errors;
}

void SetUserFilesDir(std::string dir, bool report_errors) {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  state.user_files_dir = std::move(dir);
  state.report_user_file_errors = report_errors;
}

std::string UserFilePath(absl::string_view name) {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  return state.user_files_dir.empty()
             ? ""
             : FileUtil::JoinPath(state.user_files_dir, name);
}

void ReportUserFileError(absl::string_view title, absl::string_view message) {
  LOG(ERROR) << title << ": " << message;
  State& state = GetState();
  bool report = false;
  {
    absl::MutexLock lock(state.mutex);
    report = state.report_user_file_errors;
  }
  if (report) {
    Notify(title, message);
  }
}

FileWatch::FileWatch(std::string path)
    : path_(std::move(path)),
      stamp_(StampOf(path_)),
      last_check_(Clock::GetAbslTime()) {}

bool FileWatch::Changed() {
  const absl::Time now = Clock::GetAbslTime();
  if (path_.empty() || now - last_check_ < absl::Seconds(1)) {
    return false;
  }
  last_check_ = now;
  const FileStamp stamp = StampOf(path_);
  if (stamp == stamp_) {
    return false;
  }
  stamp_ = stamp;
  return true;
}

bool SettingsFileEnabled() {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  return !state.path.empty();
}

bool SettingsFileLoaded() {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  return state.loaded;
}

bool SettingsFileWritable() {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  return state.writable;
}

bool SettingsFileChanged() {
  State& state = GetState();
  absl::MutexLock lock(state.mutex);
  const absl::Time now = Clock::GetAbslTime();
  if (state.path.empty() || now - state.last_check < absl::Seconds(1)) {
    return false;
  }
  state.last_check = now;
  return !(StampOf(state.path) == state.stamp);
}

absl::StatusOr<config::Config> ReadSettingsFile(const config::Config& defaults,
                                                const config::Config& stored,
                                                bool& migrated) {
  migrated = false;
  State& state = GetState();
  std::string error;
  bool report = false;
  {
    absl::MutexLock lock(state.mutex);
    state.last_check = Clock::GetAbslTime();
    state.stamp = StampOf(state.path);
    if (state.stamp.size < 0) {
      migrated = true;
      state.last_error.clear();
      state.loaded = true;
      config::Config config = Migrate(state.path, stored);
      if (state.writable) {
        absl::StatusOr<std::string> text =
            UpdateTomlConfig("", config, defaults);
        const absl::Status status =
            text.ok() ? WriteAtomically(state.path, *text) : text.status();
        if (status.ok()) {
          state.stamp = StampOf(state.path);
        } else {
          LOG(ERROR) << "Cannot write " << state.path << ": " << status;
        }
      }
      return config;
    }
    absl::StatusOr<std::string> text = FileUtil::GetContents(state.path);
    absl::StatusOr<config::Config> config =
        text.ok() ? SettingsOf(*text, defaults) : text.status();
    if (config.ok()) {
      state.last_error.clear();
      state.loaded = true;
      return config;
    }
    error = std::string(config.status().message());
    report = state.report_errors && error != state.last_error;
    state.last_error = error;
  }
  LOG(ERROR) << "floatype settings file: " << error;
  if (report) {
    Notify(kErrorTitle, error);
  }
  return absl::InvalidArgumentError(error);
}

absl::StatusOr<config::Config> WriteSettingsFile(
    const config::Config& config, const config::Config& defaults) {
  State& state = GetState();
  absl::Status status;
  bool report = false;
  {
    absl::MutexLock lock(state.mutex);
    if (!state.writable) {
      return absl::FailedPreconditionError("This process does not write");
    }
    absl::StatusOr<std::string> text = FileUtil::GetContents(state.path);
    if (!text.ok() && StampOf(state.path).size < 0) {
      text = "";
    }
    absl::StatusOr<std::string> updated =
        text.ok() ? UpdateTomlConfig(*text, config, defaults) : text.status();
    absl::StatusOr<config::Config> settings =
        updated.ok() ? SettingsOf(*updated, defaults) : updated.status();
    if (settings.ok() && *updated != *text) {
      status = WriteAtomically(state.path, *updated);
    } else {
      status = settings.status();
    }
    if (status.ok()) {
      // Our own write is not a change to read back.
      state.stamp = StampOf(state.path);
      state.last_check = Clock::GetAbslTime();
      state.last_error.clear();
      state.loaded = true;
      return settings;
    }
    report = state.report_errors;
  }
  LOG(ERROR) << "Cannot save the settings: " << status;
  if (report) {
    Notify(
        "floatype の設定を保存できませんでした",
        absl::StrCat(status.message(), "（設定ファイルは書き換えていません）"));
  }
  return status;
}

}  // namespace mozc::floatype
