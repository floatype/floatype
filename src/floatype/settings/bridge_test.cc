#include "floatype/settings/bridge.h"

#include <memory>
#include <string>

#include "absl/strings/match.h"
#include "client/client.h"
#include "client/client_interface.h"
#include "client/client_mock.h"
#include "config/config_handler.h"
#include "protocol/config.pb.h"
#include "testing/gmock.h"
#include "testing/gunit.h"

namespace mozc::floatype {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// A converter whose settings live here, so that no test reaches a real one.
class FakeConverter : public client::ClientFactoryInterface {
 public:
  std::unique_ptr<client::ClientInterface> NewClient() override {
    auto client = std::make_unique<NiceMock<client::ClientMock>>();
    ON_CALL(*client, CheckVersionOrRestartServer())
        .WillByDefault(Return(reachable));
    ON_CALL(*client, GetConfig(_)).WillByDefault([this](config::Config* c) {
      *c = settings;
      return true;
    });
    ON_CALL(*client, ClearUserHistory()).WillByDefault([this] {
      ++clears;
      return reachable;
    });
    ON_CALL(*client, ClearUserPrediction()).WillByDefault(Return(true));
    ON_CALL(*client, SetConfig(_))
        .WillByDefault([this](const config::Config& c) {
          settings = c;
          ++saves;
          return true;
        });
    return client;
  }

  config::Config settings;
  bool reachable = true;
  int saves = 0;
  int clears = 0;
};

class BridgeTest : public ::testing::Test {
 protected:
  void SetUp() override { client::ClientFactory::SetClientFactory(&fake_); }
  void TearDown() override { client::ClientFactory::SetClientFactory(nullptr); }

  FakeConverter fake_;
};

TEST_F(BridgeTest, LoadsTheConvertersSettings) {
  fake_.settings.set_zenz_learning(false);
  char* lines = FloatypeSettingsLoad();
  ASSERT_NE(lines, nullptr);
  EXPECT_TRUE(absl::StrContains(lines, "zenz_learning = false\n"));
  FloatypeSettingsFree(lines);
}

TEST_F(BridgeTest, SavesOnlyTheChange) {
  fake_.settings.set_use_live_conversion(false);
  EXPECT_EQ(FloatypeSettingsSave("zenz_learning = false\n"), nullptr);
  EXPECT_EQ(fake_.saves, 1);
  EXPECT_FALSE(fake_.settings.zenz_learning());
  EXPECT_TRUE(fake_.settings.has_use_live_conversion());
  EXPECT_FALSE(fake_.settings.use_live_conversion());
  EXPECT_FALSE(fake_.settings.has_preedit_method()) << "unset stays unset";
}

TEST_F(BridgeTest, SaysWhyNothingWasSaved) {
  char* error = FloatypeSettingsSave("no_such_field = 1\n");
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(fake_.saves, 0);
  FloatypeSettingsFree(error);

  fake_.reachable = false;
  EXPECT_EQ(FloatypeSettingsLoad(), nullptr);
  error = FloatypeSettingsSave("zenz_learning = false\n");
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(fake_.saves, 0);
  FloatypeSettingsFree(error);
}

TEST_F(BridgeTest, ResetsToTheDefaults) {
  fake_.settings.set_zenz_learning(false);
  EXPECT_EQ(FloatypeSettingsReset(), nullptr);
  EXPECT_EQ(fake_.settings.SerializeAsString(),
            config::ConfigHandler::DefaultConfig().SerializeAsString());
}

TEST_F(BridgeTest, ClearsTheHistory) {
  EXPECT_EQ(FloatypeSettingsClearHistory(), nullptr);
  EXPECT_EQ(fake_.clears, 1);
  fake_.reachable = false;
  char* error = FloatypeSettingsClearHistory();
  ASSERT_NE(error, nullptr);
  FloatypeSettingsFree(error);
}

TEST(BridgeUpdateTest, TellsANewerRelease) {
  char* version = nullptr;
  char* package = nullptr;
  char* checksum = nullptr;
  const std::string prefix =
      "https://github.com/floatype/floatype/releases/download/v999.0.0/";
  const std::string assets =
      "floatype-999.0.0+mozc.3.35.6354.pkg\t" + prefix +
      "floatype-999.0.0+mozc.3.35.6354.pkg\n"
      "floatype-999.0.0+mozc.3.35.6354.pkg.sha256\t" + prefix +
      "floatype-999.0.0+mozc.3.35.6354.pkg.sha256\n";
  EXPECT_EQ(FloatypeSettingsCheckRelease(200, "v999.0.0", assets.c_str(),
                                         &version, &package, &checksum),
            0);
  ASSERT_NE(version, nullptr);
  EXPECT_STREQ(version, "999.0.0");
  EXPECT_TRUE(absl::EndsWith(package, ".pkg"));
  EXPECT_TRUE(absl::EndsWith(checksum, ".sha256"));
  FloatypeSettingsFree(version);
  FloatypeSettingsFree(package);
  FloatypeSettingsFree(checksum);

  EXPECT_EQ(FloatypeSettingsCheckRelease(404, "", "", &version, &package,
                                         &checksum),
            2);
  EXPECT_EQ(version, nullptr);
  EXPECT_EQ(FloatypeSettingsCheckRelease(500, "", "", &version, &package,
                                         &checksum),
            3);
}

TEST(BridgeUpdateTest, MatchesTheChecksumFile) {
  const std::string hex(64, 'a');
  EXPECT_TRUE(FloatypeSettingsChecksumMatches((hex + "  x.pkg\n").c_str(),
                                              hex.c_str()));
  EXPECT_FALSE(FloatypeSettingsChecksumMatches(
      (std::string(64, 'b') + "  x.pkg\n").c_str(), hex.c_str()));
}

}  // namespace
}  // namespace mozc::floatype
