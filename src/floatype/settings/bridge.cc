#include "floatype/settings/bridge.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "client/client.h"
#include "client/client_interface.h"
#include "floatype/settings/config_lines.h"
#include "protocol/config.pb.h"

namespace {

char* Copy(absl::string_view text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  std::memcpy(out, text.data(), text.size());
  out[text.size()] = '\0';
  return out;
}

// The settings as the converter has them now, or nullptr.
std::unique_ptr<mozc::client::ClientInterface> Connected(
    mozc::config::Config& config) {
  std::unique_ptr<mozc::client::ClientInterface> client =
      mozc::client::ClientFactory::NewClient();
  if (!client->CheckVersionOrRestartServer() || !client->GetConfig(&config)) {
    return nullptr;
  }
  return client;
}

}  // namespace

extern "C" {

char* FloatypeSettingsLoad(void) {
  mozc::config::Config config;
  if (Connected(config) == nullptr) {
    return nullptr;
  }
  return Copy(mozc::floatype::ConfigLines(config));
}

char* FloatypeSettingsSave(const char* toml) {
  mozc::config::Config loaded;
  const std::unique_ptr<mozc::client::ClientInterface> client =
      Connected(loaded);
  if (client == nullptr) {
    return Copy("変換エンジンにつながりませんでした");
  }
  const absl::StatusOr<mozc::config::Config> changed =
      mozc::floatype::ApplyConfigLines(loaded, toml);
  if (!changed.ok()) {
    return Copy(changed.status().message());
  }
  if (!client->SetConfig(*changed)) {
    return Copy("変換エンジンが設定を受け取りませんでした");
  }
  return nullptr;
}

void FloatypeSettingsFree(char* text) { std::free(text); }

}  // extern "C"
