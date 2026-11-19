// The user dictionary as a text file, ~/.config/floatype/user_dictionary.tsv:
// Mozc's dictionary tool export (reading, word, part of speech as the tool
// names it, comment; tab separated), with a "[name]" line starting each
// dictionary. Lines starting with # and blank lines are comments.

#ifndef MOZC_FLOATYPE_CONFIG_USER_DICTIONARY_TEXT_H_
#define MOZC_FLOATYPE_CONFIG_USER_DICTIONARY_TEXT_H_

#include <cstddef>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "protocol/user_dictionary_storage.pb.h"

namespace mozc::floatype {

// The dictionaries in `text`, or the first error, "N 行目: ...". Mozc's
// checks apply: at most `max_dictionaries` dictionaries and `max_entries`
// words in each, names unique, words as ValidateEntry wants them.
absl::StatusOr<user_dictionary::UserDictionaryStorage> ParseUserDictionary(
    absl::string_view text, size_t max_dictionaries, size_t max_entries);

// `storage` as text, keeping the comments of `old_text`: those at its head
// stay at the head, those at its end at the end, and those before a line go
// before the same line if it is still there. Words Mozc would not use (an
// empty reading, say) are left out.
std::string FormatUserDictionary(
    const user_dictionary::UserDictionaryStorage& storage,
    absl::string_view old_text);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_CONFIG_USER_DICTIONARY_TEXT_H_
