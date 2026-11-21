#include "floatype/update/update_check.h"

#include <string>
#include <vector>

#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

constexpr char kCurrent[] = "0.1.0+mozc.3.35.6354";
const char kDownload[] =
    "https://github.com/floatype/floatype/releases/download/v0.2.0/";

std::vector<ReleaseAsset> Assets(const std::string& base = kDownload) {
  return {{"floatype-0.2.0+mozc.3.35.6354.pkg",
           base + "floatype-0.2.0+mozc.3.35.6354.pkg"},
          {"floatype-0.2.0+mozc.3.35.6354.pkg.sha256",
           base + "floatype-0.2.0+mozc.3.35.6354.pkg.sha256"}};
}

TEST(UpdateCheckTest, ComparesTheThreeNumbers) {
  EXPECT_TRUE(IsNewerVersion("0.1.1", kCurrent));
  EXPECT_TRUE(IsNewerVersion("0.10.0", "0.9.9"));
  EXPECT_TRUE(IsNewerVersion("1.0.0", "0.99.99"));
  EXPECT_FALSE(IsNewerVersion("0.1.0", kCurrent));
  EXPECT_FALSE(IsNewerVersion("0.0.9", kCurrent));
  // Not three numbers: never newer.
  EXPECT_FALSE(IsNewerVersion("1.0", kCurrent));
  EXPECT_FALSE(IsNewerVersion("latest", kCurrent));
  EXPECT_FALSE(IsNewerVersion("1..0", kCurrent));
  EXPECT_FALSE(IsNewerVersion("9.9.9", "unknown"));
}

TEST(UpdateCheckTest, ANewerReleaseWithItsPackageAndChecksum) {
  const UpdateCheck check =
      CheckLatestRelease(200, "v0.2.0", Assets(), kCurrent);
  EXPECT_EQ(check.kind, UpdateCheck::kNewer);
  EXPECT_EQ(check.version, "0.2.0");
  EXPECT_EQ(check.package_url,
            std::string(kDownload) + "floatype-0.2.0+mozc.3.35.6354.pkg");
  EXPECT_EQ(check.checksum_url, std::string(kDownload) +
                                    "floatype-0.2.0+mozc.3.35.6354.pkg.sha256");
}

TEST(UpdateCheckTest, TheSameOrAnOlderReleaseIsUpToDate) {
  EXPECT_EQ(CheckLatestRelease(200, "v0.1.0", Assets(), kCurrent).kind,
            UpdateCheck::kUpToDate);
  EXPECT_EQ(CheckLatestRelease(200, "v0.0.9", Assets(), kCurrent).kind,
            UpdateCheck::kUpToDate);
}

TEST(UpdateCheckTest, NothingPublishedYet) {
  // GitHub answers 404 while there are only drafts.
  EXPECT_EQ(CheckLatestRelease(404, "", {}, kCurrent).kind,
            UpdateCheck::kNoRelease);
}

TEST(UpdateCheckTest, AnAnswerThatCannotBeUsedFails) {
  // Another status, a tag that is not a version, a tag with more after it.
  EXPECT_EQ(CheckLatestRelease(500, "v0.2.0", Assets(), kCurrent).kind,
            UpdateCheck::kFailed);
  EXPECT_EQ(CheckLatestRelease(200, "", {}, kCurrent).kind,
            UpdateCheck::kFailed);
  EXPECT_EQ(CheckLatestRelease(200, "nightly", Assets(), kCurrent).kind,
            UpdateCheck::kFailed);
  EXPECT_EQ(CheckLatestRelease(200, "v0.2.0; rm -rf", Assets(), kCurrent).kind,
            UpdateCheck::kFailed);
  EXPECT_EQ(CheckLatestRelease(200, "0.2.0", Assets(), kCurrent).kind,
            UpdateCheck::kFailed);
  // A release without the package, or without its checksum.
  EXPECT_EQ(CheckLatestRelease(200, "v0.2.0", {}, kCurrent).kind,
            UpdateCheck::kFailed);
  std::vector<ReleaseAsset> no_checksum = Assets();
  no_checksum.pop_back();
  EXPECT_EQ(CheckLatestRelease(200, "v0.2.0", no_checksum, kCurrent).kind,
            UpdateCheck::kFailed);
  // Files from another host, another repository, or over http.
  for (const char* base :
       {"https://example.com/floatype/floatype/releases/download/v0.2.0/",
        "https://github.com/other/floatype/releases/download/v0.2.0/",
        "http://github.com/floatype/floatype/releases/download/v0.2.0/",
        "https://github.com.example.com/floatype/floatype/releases/"
        "download/"}) {
    EXPECT_EQ(CheckLatestRelease(200, "v0.2.0", Assets(base), kCurrent).kind,
              UpdateCheck::kFailed)
        << base;
  }
  // A package of another version in the release.
  EXPECT_EQ(CheckLatestRelease(200, "v0.3.0", Assets(), kCurrent).kind,
            UpdateCheck::kFailed);
}

TEST(UpdateCheckTest, TheChecksumFileNamesTheDigest) {
  const std::string digest(64, 'a');
  EXPECT_TRUE(ChecksumMatches(digest + "  floatype-0.2.0.pkg\n", digest));
  EXPECT_TRUE(ChecksumMatches(std::string(64, 'A') + "  x.pkg\n", digest));
  EXPECT_FALSE(ChecksumMatches(std::string(64, 'b') + "  x.pkg\n", digest));
  EXPECT_FALSE(ChecksumMatches("", digest));
  EXPECT_FALSE(ChecksumMatches("aaaa  x.pkg\n", "aaaa"));
  EXPECT_FALSE(ChecksumMatches("<html>Not Found</html>", digest));
}

}  // namespace
}  // namespace mozc::floatype
