#include "floatype/zenz/letter_reading.h"

#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace mozc::floatype {

absl::Span<const absl::string_view> LetterNames(char letter) {
  using Names = std::vector<absl::string_view>;
  static const Names* const kNames = new Names[26]{
      {"えー", "えい"},         // A
      {"びー"},                 // B
      {"しー"},                 // C
      {"でぃー", "でー"},       // D
      {"いー"},                 // E
      {"えふ"},                 // F
      {"じー"},                 // G
      {"えいち", "えっち"},     // H
      {"あい"},                 // I
      {"じぇー", "じぇい"},     // J
      {"けー", "けい"},         // K
      {"える"},                 // L
      {"えむ"},                 // M
      {"えぬ"},                 // N
      {"おー"},                 // O
      {"ぴー"},                 // P
      {"きゅー"},               // Q
      {"あーる"},               // R
      {"えす"},                 // S
      {"てぃー", "てー"},       // T
      {"ゆー"},                 // U
      {"ぶい", "ゔぃー"},       // V
      {"だぶりゅー", "だぶる"}, // W
      {"えっくす"},             // X
      {"わい"},                 // Y
      {"ぜっと", "ぜーた"},     // Z
  };
  const unsigned char c = static_cast<unsigned char>(letter);
  if (!absl::ascii_isalpha(c)) {
    return {};
  }
  return kNames[absl::ascii_tolower(c) - 'a'];
}

}  // namespace mozc::floatype
