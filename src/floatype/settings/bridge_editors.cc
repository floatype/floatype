// floatype: the bridge's part for the settings screen's editor windows
// (issue #9): what they show and change, read and written here, so that the
// Swift side only draws.

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/system_util.h"
#include "floatype/settings/bridge.h"
#include "floatype/zenz/zenz_memory.h"

namespace {

using ::mozc::floatype::ZenzMemory;

char* Copy(absl::string_view text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  std::memcpy(out, text.data(), text.size());
  out[text.size()] = '\0';
  return out;
}

// In the order they are listed, as FloatypeSettingsZenzMemory numbers them.
ZenzMemory& (*const kMemories[])() = {
    &ZenzMemory::TurnedDown, &ZenzMemory::RefusedOnce, &ZenzMemory::Accepted,
    &ZenzMemory::Stopped};

}  // namespace

extern "C" {

char* FloatypeSettingsZenzMemory(void) {
  std::string out;
  for (int kind = 0; kind < std::ssize(kMemories); ++kind) {
    const auto entries = kMemories[kind]().Entries();
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
      absl::StrAppend(&out, kind, "\t", entry->first, "\t", entry->second,
                      "\n");
    }
  }
  return Copy(out);
}

void FloatypeSettingsZenzForget(int kind, const char* reading,
                                const char* text) {
  if (kind >= 0 && kind < std::ssize(kMemories)) {
    kMemories[kind]().Remove(reading, text);
  }
}

void FloatypeSettingsZenzForgetAll(void) {
  for (ZenzMemory& (*memory)() : kMemories) {
    memory().Clear();
  }
}

char* FloatypeSettingsProfileDirectory(void) {
  return Copy(mozc::SystemUtil::GetUserProfileDirectory());
}

}  // extern "C"
