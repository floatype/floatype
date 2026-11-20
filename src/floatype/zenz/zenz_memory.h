// floatype: what the user did with zenz's corrections (spec section 2, zenz
// learning). Four memories: the corrections the user stopped (Ctrl+Delete
// while one is shown), which zenz does not make again; the ones the user
// committed, which are shown at once the next time the reading comes; the
// words the user chose another candidate over, which zenz does not bring
// back; and the words of a correction left once for Mozc's result, which are
// turned down the second time.

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

  // Whether zenz's `text` for the hiragana `reading` is remembered.
  bool Contains(absl::string_view reading, absl::string_view text) const;
  // The text remembered last for `reading`; empty when there is none.
  std::string Latest(absl::string_view reading) const;
  // Remembers it, on disk too. Remembering it again makes it the newest.
  void Add(absl::string_view reading, absl::string_view text);
  void Remove(absl::string_view reading, absl::string_view text);
  // Forgets everything, as clearing the user history does.
  void Clear();

  // The memories in the user profile directory. They follow the directory, so
  // tests with a profile of their own get memories of their own.
  static ZenzMemory& Stopped();
  static ZenzMemory& Accepted();
  // Words, not sentences: the reading and the writing of the content word of
  // a candidate (かんじ and 完治 for 完治に).
  static ZenzMemory& TurnedDown();
  // Words too, left for Mozc's result once with nothing else chosen.
  static ZenzMemory& RefusedOnce();

 private:
  void Save() const;

  std::string path_;
  std::deque<std::string> order_;  // "<reading>\t<text>", oldest first.
  absl::flat_hash_set<std::string> entries_;
};

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ZENZ_ZENZ_MEMORY_H_
