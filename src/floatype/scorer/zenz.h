// Text handling for zenz-v3 prompts and outputs, shared by every zenz caller.

#ifndef FLOATYPE_SCORER_ZENZ_H_
#define FLOATYPE_SCORER_ZENZ_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace floatype::zenz {

// Builds a zenz-v3 conversion prompt, as AzooKeyKanaKanjiConverter's
// ZenzPromptBuilder does: U+EE02 and the last 40 characters of the left context
// (when there is any), then U+EE00, the katakana reading and U+EE01.
std::string BuildPrompt(std::string_view left_context,
                        std::string_view reading);

// Returns the byte offset of the first zenz tag (U+EE00..U+EE0F) or line break
// in generated text, where the conversion ends; npos when there is none.
size_t FindStop(std::string_view text);

// `text` with full-width ASCII (U+FF01..U+FF5E) in half width, so that
// conversions that differ only in width compare equal, as Mozc shows them.
std::string FoldWidth(std::string_view text);

}  // namespace floatype::zenz

#endif  // FLOATYPE_SCORER_ZENZ_H_
