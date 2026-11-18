#include "floatype/zenz/zenz_memory.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "base/system_util.h"

namespace mozc::floatype {
namespace {

std::string Key(absl::string_view reading, absl::string_view text) {
  return absl::StrCat(reading, "\t", text);
}

}  // namespace

ZenzMemory::ZenzMemory(std::string path) : path_(std::move(path)) {
  const absl::StatusOr<std::string> contents = FileUtil::GetContents(path_);
  if (!contents.ok()) {
    return;  // Nothing stopped yet.
  }
  for (const absl::string_view line :
       absl::StrSplit(*contents, '\n', absl::SkipEmpty())) {
    if (line.find('\t') != absl::string_view::npos &&
        stopped_.insert(std::string(line)).second) {
      order_.emplace_back(line);
    }
  }
  while (order_.size() > kCapacity) {
    stopped_.erase(order_.front());
    order_.pop_front();
  }
}

bool ZenzMemory::Stopped(absl::string_view reading,
                         absl::string_view text) const {
  return stopped_.contains(Key(reading, text));
}

void ZenzMemory::Stop(absl::string_view reading, absl::string_view text) {
  std::string key = Key(reading, text);
  if (key.find('\n') != std::string::npos || !stopped_.insert(key).second) {
    return;
  }
  order_.push_back(std::move(key));
  if (order_.size() > kCapacity) {
    stopped_.erase(order_.front());
    order_.pop_front();
  }
  Save();
}

void ZenzMemory::Clear() {
  order_.clear();
  stopped_.clear();
  Save();
}

void ZenzMemory::Save() const {
  const std::string contents =
      order_.empty() ? "" : absl::StrCat(absl::StrJoin(order_, "\n"), "\n");
  if (absl::Status status = FileUtil::SetContents(path_, contents);
      !status.ok()) {
    LOG(ERROR) << "Cannot save the stopped zenz corrections: " << status;
  }
}

ZenzMemory& ZenzMemory::Default() {
  static absl::NoDestructor<std::unique_ptr<ZenzMemory>> memory;
  const std::string path = FileUtil::JoinPath(
      SystemUtil::GetUserProfileDirectory(), "floatype_zenz_stopped.tsv");
  if (*memory == nullptr || (*memory)->path_ != path) {
    *memory = std::make_unique<ZenzMemory>(path);
  }
  return **memory;
}

}  // namespace mozc::floatype
