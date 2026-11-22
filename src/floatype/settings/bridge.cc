#include "floatype/settings/bridge.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/version.h"
#include "client/client.h"
#include "client/client_interface.h"
#include "config/config_handler.h"
#include "floatype/settings/config_lines.h"
#include "floatype/update/update_check.h"
#include "protocol/config.pb.h"

#ifdef __APPLE__
#include "base/mac/mac_util.h"
#endif  // __APPLE__

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

char* FloatypeSettingsReset(void) {
  mozc::config::Config loaded;
  const std::unique_ptr<mozc::client::ClientInterface> client =
      Connected(loaded);
  if (client == nullptr) {
    return Copy("変換エンジンにつながりませんでした");
  }
  if (!client->SetConfig(mozc::config::ConfigHandler::DefaultConfig())) {
    return Copy("変換エンジンが設定を受け取りませんでした");
  }
  return nullptr;
}

char* FloatypeSettingsClearHistory(void) {
  mozc::config::Config loaded;
  const std::unique_ptr<mozc::client::ClientInterface> client =
      Connected(loaded);
  if (client == nullptr || !client->ClearUserHistory() ||
      !client->ClearUserPrediction()) {
    return Copy("変換エンジンにつながらず、消せませんでした");
  }
  return nullptr;
}

char* FloatypeSettingsLaunchTool(const char* mode) {
  if (!mozc::client::ClientFactory::NewClient()->LaunchTool(mode, "")) {
    return Copy("開けませんでした");
  }
  return nullptr;
}

int FloatypeSettingsStartsAtLogin(void) {
#ifdef __APPLE__
  return mozc::MacUtil::CheckPrelauncherLoginItemStatus();
#else
  return 0;
#endif  // __APPLE__
}

void FloatypeSettingsSetStartsAtLogin(int on) {
#ifdef __APPLE__
  if (on && !mozc::MacUtil::CheckPrelauncherLoginItemStatus()) {
    mozc::MacUtil::AddPrelauncherLoginItem();
  } else if (!on && mozc::MacUtil::CheckPrelauncherLoginItemStatus()) {
    mozc::MacUtil::RemovePrelauncherLoginItem();
  }
#endif  // __APPLE__
}

char* FloatypeSettingsVersion(void) {
  return Copy(mozc::Version::GetFloatypeVersion());
}

const char* FloatypeSettingsLatestReleaseUrl(void) {
  // A constexpr string_view of a literal, so it ends in a NUL.
  return mozc::floatype::kLatestReleaseUrl.data();
}

int FloatypeSettingsCheckRelease(int status, const char* tag,
                                 const char* assets, char** version,
                                 char** package_url, char** checksum_url) {
  std::vector<mozc::floatype::ReleaseAsset> parsed;
  for (const absl::string_view line :
       absl::StrSplit(assets, '\n', absl::SkipEmpty())) {
    const std::vector<absl::string_view> parts =
        absl::StrSplit(line, absl::MaxSplits('\t', 1));
    if (parts.size() == 2) {
      parsed.push_back({std::string(parts[0]), std::string(parts[1])});
    }
  }
  const mozc::floatype::UpdateCheck check = mozc::floatype::CheckLatestRelease(
      status, tag, parsed, mozc::Version::GetFloatypeVersion());
  *version = *package_url = *checksum_url = nullptr;
  if (check.kind == mozc::floatype::UpdateCheck::kNewer) {
    *version = Copy(check.version);
    *package_url = Copy(check.package_url);
    *checksum_url = Copy(check.checksum_url);
  }
  switch (check.kind) {
    case mozc::floatype::UpdateCheck::kNewer:
      return 0;
    case mozc::floatype::UpdateCheck::kUpToDate:
      return 1;
    case mozc::floatype::UpdateCheck::kNoRelease:
      return 2;
    case mozc::floatype::UpdateCheck::kFailed:
      return 3;
  }
  return 3;
}

int FloatypeSettingsChecksumMatches(const char* checksum_file,
                                    const char* hex_digest) {
  return mozc::floatype::ChecksumMatches(checksum_file, hex_digest);
}

}  // extern "C"
