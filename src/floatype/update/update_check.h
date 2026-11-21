// Decides, from what GitHub says of floatype's latest release, whether there
// is a newer version to install. The settings dialog asks when the user
// presses its button, and only then: nothing else in floatype uses the
// network (floatype/eval/local_only.py).

#ifndef MOZC_FLOATYPE_UPDATE_UPDATE_CHECK_H_
#define MOZC_FLOATYPE_UPDATE_UPDATE_CHECK_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc::floatype {

// Where the latest release is asked for, and where its files must come from.
inline constexpr absl::string_view kLatestReleaseUrl =
    "https://api.github.com/repos/floatype/floatype/releases/latest";
inline constexpr absl::string_view kReleaseDownloadPrefix =
    "https://github.com/floatype/floatype/releases/download/";

// A file attached to a release.
struct ReleaseAsset {
  std::string name;
  std::string url;
};

struct UpdateCheck {
  enum Kind {
    kNewer,      // `version` can be installed from `package_url`.
    kUpToDate,   // The latest release is this version or an older one.
    kNoRelease,  // Nothing is published yet.
    kFailed,     // The answer could not be used.
  };
  Kind kind = kFailed;
  // With kNewer and kUpToDate: the release's version, three numbers.
  std::string version;
  // With kNewer: the installer package and the file with its SHA-256.
  std::string package_url;
  std::string checksum_url;
};

// `http_status`, `tag` and `assets` are the answer to kLatestReleaseUrl: its
// status, its tag_name (v0.1.0) and its assets. `current` is this build's
// version (0.1.0+mozc.3.35.6354). A release is used only when its tag is three
// numbers and it has floatype-<version>....pkg and that name with .sha256,
// both under kReleaseDownloadPrefix.
UpdateCheck CheckLatestRelease(int http_status, absl::string_view tag,
                               const std::vector<ReleaseAsset>& assets,
                               absl::string_view current);

// Whether version `a` is newer than `b`. Each starts with three numbers
// (0.1.0, 0.1.0+mozc.3.35.6354); false when either does not.
bool IsNewerVersion(absl::string_view a, absl::string_view b);

// Whether `checksum_file`, as shasum writes it ("<hex>  <name>"), names the
// SHA-256 `hex_digest`.
bool ChecksumMatches(absl::string_view checksum_file,
                     absl::string_view hex_digest);

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_UPDATE_UPDATE_CHECK_H_
