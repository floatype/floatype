#include "floatype/update/update_check.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace mozc::floatype {
namespace {

// The three numbers a version starts with, or nullopt.
std::optional<std::array<int, 3>> Numbers(absl::string_view version) {
  const absl::string_view head =
      version.substr(0, version.find_first_not_of("0123456789."));
  const std::vector<absl::string_view> parts = absl::StrSplit(head, '.');
  std::array<int, 3> numbers;
  if (parts.size() != 3) {
    return std::nullopt;
  }
  for (size_t i = 0; i < 3; ++i) {
    if (parts[i].empty() || parts[i].size() > 6 ||
        !absl::SimpleAtoi(parts[i], &numbers[i])) {
      return std::nullopt;
    }
  }
  return numbers;
}

}  // namespace

bool IsNewerVersion(absl::string_view a, absl::string_view b) {
  const std::optional<std::array<int, 3>> left = Numbers(a);
  const std::optional<std::array<int, 3>> right = Numbers(b);
  return left.has_value() && right.has_value() && *left > *right;
}

UpdateCheck CheckLatestRelease(int http_status, absl::string_view tag,
                               const std::vector<ReleaseAsset>& assets,
                               absl::string_view current) {
  UpdateCheck check;
  if (http_status == 404) {
    check.kind = UpdateCheck::kNoRelease;
    return check;
  }
  // The tag is the version and nothing else: v0.1.0.
  if (http_status != 200 || !absl::ConsumePrefix(&tag, "v") ||
      !Numbers(tag).has_value() ||
      tag.find_first_not_of("0123456789.") != absl::string_view::npos ||
      !Numbers(current).has_value()) {
    return check;
  }
  check.version = std::string(tag);
  if (!IsNewerVersion(tag, current)) {
    check.kind = UpdateCheck::kUpToDate;
    return check;
  }
  const std::string prefix = absl::StrCat("floatype-", tag);
  for (const ReleaseAsset& asset : assets) {
    if (!asset.name.starts_with(prefix) ||
        !asset.url.starts_with(kReleaseDownloadPrefix)) {
      continue;
    }
    if (asset.name.ends_with(".pkg")) {
      check.package_url = asset.url;
    } else if (asset.name.ends_with(".pkg.sha256")) {
      check.checksum_url = asset.url;
    }
  }
  if (!check.package_url.empty() && !check.checksum_url.empty()) {
    check.kind = UpdateCheck::kNewer;
  }
  return check;
}

bool ChecksumMatches(absl::string_view checksum_file,
                     absl::string_view hex_digest) {
  const absl::string_view named =
      checksum_file.substr(0, checksum_file.find_first_of(" \t\r\n"));
  return named.size() == 64 && hex_digest.size() == 64 &&
         absl::EqualsIgnoreCase(named, hex_digest);
}

}  // namespace mozc::floatype
