#include "floatype/settings/bridge.h"

#include <algorithm>
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
#include "floatype/indicator/indicator_layout.h"
#include "floatype/look/window_look.h"
#include "floatype/settings/config_lines.h"
#include "floatype/update/update_check.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_style.pb.h"

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

int FloatypeSettingsWindowLook(const char* lines, int suggestion, int dark,
                               FloatypeWindowLook* out) {
  const absl::StatusOr<mozc::config::Config> config =
      mozc::floatype::ApplyConfigLines(mozc::config::Config(), lines);
  if (!config.ok()) {
    return 0;
  }
  const mozc::floatype::WindowLook look = mozc::floatype::ResolveWindowLook(
      suggestion ? config->suggest_style() : config->candidate_style(), dark);
  const mozc::renderer::RendererStyle style =
      mozc::floatype::RendererStyleFor(look);
  *out = FloatypeWindowLook{
      .dark = look.dark,
      .background = look.background,
      .text = look.text,
      .secondary_text = look.secondary_text,
      .focused_background = look.focused_background,
      .focused_text = look.focused_text,
      .focused_secondary_text = look.focused_secondary_text,
      .focused_border = look.focused_border,
      .focused_border_alpha = look.focused_border_alpha,
      .border = look.border,
      .border_alpha = look.border_alpha,
      .size_percent = static_cast<double>(look.size_percent),
      .window_padding = look.window_padding,
      .row_height = look.row_height,
      .row_padding = look.row_padding,
      .row_corner_radius = look.row_corner_radius,
      .number_width = look.number_width,
      .footer_top_padding = look.footer_top_padding,
      .footer_bottom_padding = look.footer_bottom_padding,
      .corner_radius = look.corner_radius,
      .opacity = look.opacity,
      .shadow_radius = look.shadow_radius,
      .shadow_offset_y = look.shadow_offset_y,
      .shadow_opacity = look.shadow_opacity,
      .shadow_color = look.shadow_color,
      .candidate_font_size = style.candidate_style().font_size(),
      .number_font_size = style.shortcut_style().font_size(),
      .description_font_size = style.description_style().font_size(),
      .footer_font_size = style.footer_style().font_size(),
      .kind_mark_color = mozc::floatype::KindMarkColor(false, look.dark),
  };
  const std::string& minimum = style.column_minimum_width_string();
  const size_t size =
      std::min(minimum.size(), sizeof(out->column_minimum_width) - 1);
  std::memcpy(out->column_minimum_width, minimum.data(), size);
  out->column_minimum_width[size] = '\0';
  return 1;
}

char* FloatypeSettingsIndicatorLabel(int state, const char* label) {
  mozc::config::Config::IndicatorStyle::Label value =
      mozc::config::Config::IndicatorStyle::FULL_LABEL;
  mozc::config::Config::IndicatorStyle::Label_Parse(label, &value);
  return Copy(mozc::floatype::IndicatorLabel(
      static_cast<mozc::commands::Output::Indicator::State>(state), value));
}
