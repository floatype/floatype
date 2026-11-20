#include "floatype/zenz/zenz_memory.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "floatype/config/settings_file.h"

namespace mozc::floatype {
namespace {

std::string Key(absl::string_view reading, absl::string_view text) {
  return absl::StrCat(reading, "\t", text);
}

// The memory in `file_name` in the user profile directory.
ZenzMemory& InProfile(std::unique_ptr<ZenzMemory>& memory,
                      std::string& loaded_path, absl::string_view file_name) {
  const std::string path =
      FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(), file_name);
  if (memory == nullptr || loaded_path != path) {
    memory = std::make_unique<ZenzMemory>(path);
    loaded_path = path;
  }
  return *memory;
}

}  // namespace

ZenzMemory::ZenzMemory(std::string path) : path_(std::move(path)) {
  stamp_ = StampOf(path_);
  last_check_ = Clock::GetAbslTime();
  Load();
}

void ZenzMemory::Refresh(bool now) const {
  const absl::Time time = Clock::GetAbslTime();
  if (!now && time - last_check_ < absl::Seconds(1)) {
    return;
  }
  last_check_ = time;
  if (const FileStamp stamp = StampOf(path_); !(stamp == stamp_)) {
    stamp_ = stamp;
    Load();
  }
}

void ZenzMemory::Load() const {
  order_.clear();
  entries_.clear();
  const absl::StatusOr<std::string> contents = FileUtil::GetContents(path_);
  if (!contents.ok()) {
    return;  // Nothing remembered yet.
  }
  for (const absl::string_view line :
       absl::StrSplit(*contents, '\n', absl::SkipEmpty())) {
    if (line.find('\t') != absl::string_view::npos &&
        entries_.insert(std::string(line)).second) {
      order_.emplace_back(line);
    }
  }
  while (order_.size() > kCapacity) {
    entries_.erase(order_.front());
    order_.pop_front();
  }
}

bool ZenzMemory::Contains(absl::string_view reading,
                          absl::string_view text) const {
  Refresh(false);
  return entries_.contains(Key(reading, text));
}

std::vector<std::pair<std::string, std::string>> ZenzMemory::Entries() const {
  Refresh(false);
  std::vector<std::pair<std::string, std::string>> entries;
  for (const std::string& line : order_) {
    const size_t tab = line.find('\t');
    entries.emplace_back(line.substr(0, tab), line.substr(tab + 1));
  }
  return entries;
}

std::string ZenzMemory::Latest(absl::string_view reading) const {
  Refresh(false);
  const std::string prefix = absl::StrCat(reading, "\t");
  for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
    if (absl::StartsWith(*it, prefix)) {
      return it->substr(prefix.size());
    }
  }
  return "";
}

void ZenzMemory::Add(absl::string_view reading, absl::string_view text) {
  std::string key = Key(reading, text);
  if (reading.empty() || text.empty() || key.find('\n') != std::string::npos) {
    return;
  }
  Refresh(true);
  if (!entries_.insert(key).second) {
    order_.erase(std::find(order_.begin(), order_.end(), key));
  }
  order_.push_back(std::move(key));
  if (order_.size() > kCapacity) {
    entries_.erase(order_.front());
    order_.pop_front();
  }
  Save();
}

void ZenzMemory::Remove(absl::string_view reading, absl::string_view text) {
  const std::string key = Key(reading, text);
  Refresh(true);
  if (entries_.erase(key) == 0) {
    return;
  }
  order_.erase(std::find(order_.begin(), order_.end(), key));
  Save();
}

void ZenzMemory::Clear() {
  Refresh(true);
  order_.clear();
  entries_.clear();
  Save();
}

void ZenzMemory::Save() {
  const std::string contents =
      order_.empty() ? "" : absl::StrCat(absl::StrJoin(order_, "\n"), "\n");
  if (absl::Status status = WriteAtomically(path_, contents); !status.ok()) {
    LOG(ERROR) << "Cannot save what zenz learned: " << status;
  }
  stamp_ = StampOf(path_);
}

ZenzMemory& ZenzMemory::Stopped() {
  static absl::NoDestructor<std::unique_ptr<ZenzMemory>> memory;
  static absl::NoDestructor<std::string> path;
  return InProfile(*memory, *path, "floatype_zenz_stopped.tsv");
}

ZenzMemory& ZenzMemory::Accepted() {
  static absl::NoDestructor<std::unique_ptr<ZenzMemory>> memory;
  static absl::NoDestructor<std::string> path;
  return InProfile(*memory, *path, "floatype_zenz_accepted.tsv");
}

ZenzMemory& ZenzMemory::TurnedDown() {
  static absl::NoDestructor<std::unique_ptr<ZenzMemory>> memory;
  static absl::NoDestructor<std::string> path;
  return InProfile(*memory, *path, "floatype_zenz_turned_down.tsv");
}

ZenzMemory& ZenzMemory::RefusedOnce() {
  static absl::NoDestructor<std::unique_ptr<ZenzMemory>> memory;
  static absl::NoDestructor<std::string> path;
  return InProfile(*memory, *path, "floatype_zenz_refused_once.tsv");
}

}  // namespace mozc::floatype
