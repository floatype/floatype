// floatype: the corrections the user stopped (Ctrl+Delete while one is shown),
// so that zenz does not make them again (spec section 2, zenz learning).

#ifndef MOZC_FLOATYPE_ZENZ_ZENZ_MEMORY_H_
#define MOZC_FLOATYPE_ZENZ_ZENZ_MEMORY_H_

#include <cstddef>
#include <deque>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace mozc::floatype {

class ZenzMemory {
 public:
  // At most this many are kept; the oldest go first.
  static constexpr size_t kCapacity = 1000;

  // The memory kept in `path`, one "<reading>\t<text>" line each.
  explicit ZenzMemory(std::string path);

  // Whether zenz's `text` for the hiragana `reading` was stopped.
  bool Stopped(absl::string_view reading, absl::string_view text) const;
  // Remembers it, on disk too.
  void Stop(absl::string_view reading, absl::string_view text);
  // Forgets everything, as clearing the user history does.
  void Clear();

  // The memory in the user profile directory. It follows the directory, so
  // tests with a profile of their own get a memory of their own.
  static ZenzMemory& Default();

 private:
  void Save() const;

  std::string path_;
  std::deque<std::string> order_;  // "<reading>\t<text>", oldest first.
  absl::flat_hash_set<std::string> stopped_;
};

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ZENZ_ZENZ_MEMORY_H_
