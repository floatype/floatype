// The settings file through Mozc's ConfigHandler, as the converter uses it.

#include <string>

#include "base/file_util.h"
#include "base/system_util.h"
#include "config/config_handler.h"
#include "floatype/config/text_config.h"
#include "protocol/config.pb.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::floatype {
namespace {

using config::Config;
using config::ConfigHandler;

class ConfigHandlerOverlayTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    path_ = FileUtil::JoinPath(SystemUtil::GetUserProfileDirectory(),
                               "config.txtpb");
  }
  void TearDown() override {
    SetTextConfigPath("", false);
    ConfigHandler::Reload();
  }

  std::string path_;
};

TEST_F(ConfigHandlerOverlayTest, TheFileOverridesAndNeverLeaksIntoTheDb) {
  ASSERT_OK(FileUtil::SetContents(path_, "suggestions_size: 7\n"));
  SetTextConfigPath(path_, false);
  ConfigHandler::Reload();
  EXPECT_EQ(ConfigHandler::GetSharedConfig()->suggestions_size(), 7);

  // The settings screen saves what it was shown, with one change of its own.
  Config shown = ConfigHandler::GetCopiedConfig();
  shown.set_incognito_mode(true);
  ConfigHandler::SetConfig(shown);
  EXPECT_TRUE(ConfigHandler::GetSharedConfig()->incognito_mode());
  EXPECT_EQ(ConfigHandler::GetSharedConfig()->suggestions_size(), 7);

  // Without the line, the field is the settings screen's again.
  ASSERT_OK(FileUtil::SetContents(path_, "\n"));
  ConfigHandler::Reload();
  EXPECT_EQ(ConfigHandler::GetSharedConfig()->suggestions_size(), 3);
  EXPECT_TRUE(ConfigHandler::GetSharedConfig()->incognito_mode());
}

TEST_F(ConfigHandlerOverlayTest, ABrokenFileKeepsTheLastGoodSettings) {
  ASSERT_OK(FileUtil::SetContents(path_, "use_zenz: false\n"));
  SetTextConfigPath(path_, false);
  ConfigHandler::Reload();
  ASSERT_FALSE(ConfigHandler::GetSharedConfig()->use_zenz());

  ASSERT_OK(FileUtil::SetContents(
      path_, "use_zenz: false\nzenz_delay_millisec: -1\n"));
  ConfigHandler::Reload();
  EXPECT_FALSE(ConfigHandler::GetSharedConfig()->use_zenz());
}

}  // namespace
}  // namespace mozc::floatype
