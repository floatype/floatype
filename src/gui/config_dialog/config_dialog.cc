// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Qt component of configure dialog for Mozc
#include "gui/config_dialog/config_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <istream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "base/config_file_stream.h"
#include "base/port.h"
#include "base/run_level.h"
#include "client/client.h"
#include "config/config_handler.h"
#include "gui/base/stats_config_util.h"
#include "gui/base/util.h"
#include "gui/config_dialog/keymap_editor.h"
#include "gui/config_dialog/roman_table_editor.h"
#include "protocol/config.pb.h"
#include "session/keymap.h"

#if defined(__ANDROID__) || defined(__wasm__)
#error "This platform is not supported."
#endif  // __ANDROID__ || __wasm__

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <QGuiApplication>
// clang-format on

#include "gui/base/win_util.h"
#endif  // _WIN32

#ifdef __APPLE__
#include "base/mac/mac_util.h"
#endif  // __APPLE__

namespace mozc {
namespace gui {

// Qt Style sheet for the config dialog.
// https://doc.qt.io/qt-6/stylesheet-reference.html
constexpr absl::string_view kQss = R"(
QFrame[class="setting-group-layout"] {
  padding: 0em 1em;
}
QFrame[class="setting-group-header"] {
}
QFrame[class="setting-group-line"] {
}
)";

ConfigDialog::ConfigDialog()
    : client_(client::ClientFactory::NewClient()),
      initial_preedit_method_(0),
      initial_use_keyboard_to_change_preedit_method_(false),
      initial_use_mode_indicator_(true) {
  setupUi(this);
  setStyleSheet(QString::fromUtf8(kQss.data(), kQss.size()));

  // Remove the context help button (question mark button) from the window.
  Qt::WindowFlags flags = windowFlags();
  flags &= ~Qt::WindowContextHelpButtonHint;
  setWindowFlags(flags);

  setWindowModality(Qt::NonModal);

  if constexpr (port::IsWindows()) {
    miscStartupWidget->setVisible(false);
  } else if constexpr (port::IsAppleBase()) {
    miscDefaultIMEWidget->setVisible(false);
    miscAdministrationWidget->setVisible(false);
    // floatype: set as is; Mozc's translations have no "%1 Preferences".
    setWindowTitle(
        QString::fromUtf8("%1 の環境設定").arg(GuiUtil::ProductName()));
  } else if constexpr (port::IsLinuxBase()) {
    miscDefaultIMEWidget->setVisible(false);
    miscAdministrationWidget->setVisible(false);
    miscStartupWidget->setVisible(false);
  }

#ifdef NDEBUG
  // disable logging options
  miscLoggingWidget->setVisible(false);

  if constexpr (port::IsLinuxBase()) {
    // The last "misc" tab has no valid configs on Linux
    constexpr int kMiscTabIndex = 6;
    configDialogTabWidget->removeTab(kMiscTabIndex);
  }
#endif  // NDEBUG

  suggestionsSizeSpinBox->setRange(1, 9);
  SetUpFloatypeSettings();
  SetUpFooter();

  punctuationsSettingComboBox->addItem(QString::fromUtf8("、。"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("，．"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("、．"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("，。"));

  symbolsSettingComboBox->addItem(QString::fromUtf8("「」・"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("[]／"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("「」／"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("[]・"));

  keymapSettingComboBox->addItem(tr("Custom keymap"));
  keymapSettingComboBox->addItem(tr("ATOK"));
  keymapSettingComboBox->addItem(tr("MS-IME"));
  keymapSettingComboBox->addItem(tr("Kotoeri"));

  keymapname_sessionkeymap_map_[tr("ATOK")] = config::Config::ATOK;
  keymapname_sessionkeymap_map_[tr("MS-IME")] = config::Config::MSIME;
  keymapname_sessionkeymap_map_[tr("Kotoeri")] = config::Config::KOTOERI;

  inputModeComboBox->addItem(tr("Romaji"));
  inputModeComboBox->addItem(tr("Kana"));
  if constexpr (port::IsWindows()) {
    // These options changing the preedit method by a hot key are only
    // supported by Windows.
    inputModeComboBox->addItem(tr("Romaji (switchable)"));
    inputModeComboBox->addItem(tr("Kana (switchable)"));
  }

  spaceCharacterFormComboBox->addItem(tr("Follow input mode"));
  spaceCharacterFormComboBox->addItem(tr("Fullwidth"));
  spaceCharacterFormComboBox->addItem(tr("Halfwidth"));

  selectionShortcutModeComboBox->addItem(tr("No shortcut"));
  selectionShortcutModeComboBox->addItem(tr("1 -- 9"));
  selectionShortcutModeComboBox->addItem(tr("A -- L"));

  historyLearningLevelComboBox->addItem(tr("Yes"));
  historyLearningLevelComboBox->addItem(tr("Yes (don't record new data)"));
  historyLearningLevelComboBox->addItem(tr("No"));

  shiftKeyModeSwitchComboBox->addItem(tr("Off"));
  shiftKeyModeSwitchComboBox->addItem(tr("Alphanumeric"));
  shiftKeyModeSwitchComboBox->addItem(tr("Katakana"));

  numpadCharacterFormComboBox->addItem(tr("Follow input mode"));
  numpadCharacterFormComboBox->addItem(tr("Fullwidth"));
  numpadCharacterFormComboBox->addItem(tr("Halfwidth"));
  numpadCharacterFormComboBox->addItem(tr("Direct input"));

  verboseLevelComboBox->addItem(tr("0"));
  verboseLevelComboBox->addItem(tr("1"));
  verboseLevelComboBox->addItem(tr("2"));

  yenSignComboBox->addItem(tr("Yen Sign ¥"));
  yenSignComboBox->addItem(tr("Backslash \\"));

  if constexpr (!port::IsAppleBase()) {
    // On Windows/Linux, yenSignCombBox can be hidden.
    yenSignLabel->hide();
    yenSignComboBox->hide();
    // On Windows/Linux, useJapaneseLayout checkbox should be invisible.
    useJapaneseLayout->hide();
  }

  if constexpr (!port::IsWindows()) {
    // Mode indicator is available only on Windows.
    useModeIndicator->hide();
  }

  // signal/slot
  QObject::connect(clearUserHistoryButton, SIGNAL(clicked()), this,
                   SLOT(ClearUserHistory()));
  QObject::connect(editUserDictionaryButton, SIGNAL(clicked()), this,
                   SLOT(EditUserDictionary()));
  QObject::connect(editKeymapButton, SIGNAL(clicked()), this,
                   SLOT(EditKeymap()));
  QObject::connect(resetToDefaultsButton, SIGNAL(clicked()), this,
                   SLOT(ResetToDefaults()));
  QObject::connect(editRomanTableButton, SIGNAL(clicked()), this,
                   SLOT(EditRomanTable()));
  QObject::connect(inputModeComboBox, SIGNAL(currentIndexChanged(int)), this,
                   SLOT(SelectInputModeSetting(int)));
  QObject::connect(useAutoConversion, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectAutoConversionSetting(int)));
  QObject::connect(historySuggestCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(dictionarySuggestCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(realtimeConversionCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(launchAdministrationDialogButton, SIGNAL(clicked()), this,
                   SLOT(LaunchAdministrationDialog()));
  QObject::connect(launchAdministrationDialogButtonForUsageStats,
                   SIGNAL(clicked()), this, SLOT(LaunchAdministrationDialog()));

  // When clicking these messages, CheckBoxs corresponding
  // to them should be toggled.
  // We cannot use connect/slot as QLabel doesn't define
  // clicked slot by default.
  usageStatsMessage->installEventFilter(this);
  incognitoModeMessage->installEventFilter(this);

  if constexpr (!port::IsWindows()) {
    checkDefaultCheckBox->setVisible(false);
    checkDefaultLine->setVisible(false);
    checkDefaultLabel->setVisible(false);
  }

  if constexpr (port::IsWindows()) {
    launchAdministrationDialogButton->setEnabled(true);
    // if the current application is not elevated by UAC,
    // add a shield icon
    if (!mozc::RunLevel::IsElevatedByUAC()) {
      const QIcon& vista_shield_icon =
          QApplication::style()->standardIcon(QStyle::SP_VistaShield);
      launchAdministrationDialogButton->setIcon(vista_shield_icon);
      launchAdministrationDialogButtonForUsageStats->setIcon(vista_shield_icon);
    }

    usageStatsCheckBox->setDisabled(true);
    usageStatsCheckBox->setVisible(false);
    usageStatsMessage->setDisabled(true);
    usageStatsMessage->setVisible(false);
  } else {
    launchAdministrationDialogButton->setEnabled(false);
    launchAdministrationDialogButton->setVisible(false);
    launchAdministrationDialogButtonForUsageStats->setEnabled(false);
    launchAdministrationDialogButtonForUsageStats->setVisible(false);
    administrationLine->setVisible(false);
    administrationLabel->setVisible(false);
    dictionaryPreloadingAndUACLabel->setVisible(false);
  }

  if constexpr (port::IsLinuxBase()) {
    // On Linux, disable all fields for UsageStats
    usageStatsLabel->setEnabled(false);
    usageStatsLabel->setVisible(false);
    usageStatsLine->setEnabled(false);
    usageStatsLine->setVisible(false);
    usageStatsMessage->setEnabled(false);
    usageStatsMessage->setVisible(false);
    usageStatsCheckBox->setEnabled(false);
    usageStatsCheckBox->setVisible(false);
  }

  GuiUtil::ReplaceWidgetLabels(this);

  Reload();

#ifdef _WIN32
  IMEHotKeyDisabledCheckBox->setChecked(WinUtil::GetIMEHotKeyDisabled());
#else   // _WIN32
  IMEHotKeyDisabledCheckBox->setVisible(false);
#endif  // _WIN32

#ifdef CHANNEL_DEV
  usageStatsCheckBox->setEnabled(false);
#endif  // CHANNEL_DEV

  // floatype: every change is saved, a moment later so that holding an arrow
  // on a spin box writes once. Connected after Reload() so loading does not
  // save.
  save_timer_.setSingleShot(true);
  save_timer_.setInterval(300);
  connect(&save_timer_, &QTimer::timeout, this, [this] { Update(); });
  const auto schedule_save = [this] { save_timer_.start(); };
  for (QCheckBox* box : findChildren<QCheckBox*>()) {
    connect(box, &QCheckBox::toggled, this, schedule_save);
  }
  for (QSpinBox* box : findChildren<QSpinBox*>()) {
    connect(box, &QSpinBox::valueChanged, this, schedule_save);
  }
  for (QComboBox* box : findChildren<QComboBox*>()) {
    connect(box, &QComboBox::currentIndexChanged, this, schedule_save);
  }
  connect(characterFormEditor, &QTableWidget::itemChanged, this, schedule_save);
  // Without OK, Return would press whichever button Qt picks as the default.
  for (QPushButton* button : findChildren<QPushButton*>()) {
    button->setAutoDefault(false);
    button->setDefault(false);
  }
}

void ConfigDialog::done(int result) {
  // Closing saves a change still waiting for the timer.
  if (save_timer_.isActive()) {
    save_timer_.stop();
    Update();
  }
  QDialog::done(result);
}

bool ConfigDialog::SetConfig(const config::Config& config) {
  if (!client_->CheckVersionOrRestartServer()) {
    LOG(ERROR) << "CheckVersionOrRestartServer failed";
    return false;
  }

  if (!client_->SetConfig(config)) {
    LOG(ERROR) << "SetConfig failed";
    return false;
  }

  return true;
}

bool ConfigDialog::GetConfig(config::Config* config) {
  if (!client_->CheckVersionOrRestartServer()) {
    LOG(ERROR) << "CheckVersionOrRestartServer failed";
    return false;
  }

  if (!client_->GetConfig(config)) {
    LOG(ERROR) << "GetConfig failed";
    return false;
  }

  return true;
}

void ConfigDialog::Reload() {
  config::Config config;
  if (!GetConfig(&config)) {
    QMessageBox::critical(this, windowTitle(),
                          tr("Failed to get current config values."));
  }
  ConvertFromProto(config);

  SelectAutoConversionSetting(static_cast<int>(config.use_auto_conversion()));

  initial_preedit_method_ = static_cast<int>(config.preedit_method());
  initial_use_keyboard_to_change_preedit_method_ =
      config.use_keyboard_to_change_preedit_method();
  initial_use_mode_indicator_ = config.use_mode_indicator();
}

bool ConfigDialog::Update() {
  config::Config config;
  ConvertToProto(&config);

  if (config.session_keymap() == config::Config::CUSTOM &&
      config.custom_keymap_table().empty()) {
    QMessageBox::warning(this, windowTitle(),
                         tr("The current custom keymap table is empty. "
                            "When custom keymap is selected, "
                            "you must customize it."));
    return false;
  }

  if constexpr (port::IsWindows()) {
    if ((initial_preedit_method_ !=
         static_cast<int>(config.preedit_method())) ||
        (initial_use_keyboard_to_change_preedit_method_ !=
         config.use_keyboard_to_change_preedit_method())) {
      QMessageBox::information(this, windowTitle(),
                               tr("Romaji/Kana setting is enabled from"
                                  " new applications."));
      initial_preedit_method_ = static_cast<int>(config.preedit_method());
      initial_use_keyboard_to_change_preedit_method_ =
          config.use_keyboard_to_change_preedit_method();
    }
  }

  if constexpr (port::IsWindows()) {
    if (initial_use_mode_indicator_ != config.use_mode_indicator()) {
      QMessageBox::information(this, windowTitle(),
                               tr("Input mode indicator setting is enabled from"
                                  " new applications."));
      initial_use_mode_indicator_ = config.use_mode_indicator();
    }
  }

  if (!SetConfig(config)) {
    QMessageBox::critical(this, windowTitle(), tr("Failed to update config"));
  }

#ifdef _WIN32
  if (!WinUtil::SetIMEHotKeyDisabled(IMEHotKeyDisabledCheckBox->isChecked())) {
    // Do not show any dialog here, since this operation will not fail
    // in almost all cases.
    // TODO(taku): better to show dialog?
    LOG(ERROR) << "Failed to update IME HotKey status";
  }
#endif  // _WIN32

#ifdef __APPLE__
  if (startupCheckBox->isChecked()) {
    if (!MacUtil::CheckPrelauncherLoginItemStatus()) {
      MacUtil::AddPrelauncherLoginItem();
    }
  } else {
    if (MacUtil::CheckPrelauncherLoginItemStatus()) {
      MacUtil::RemovePrelauncherLoginItem();
    }
  }
#endif  // __APPLE__

  return true;
}

void ConfigDialog::SetSendStatsCheckBox() {
  // On windows, usage_stats flag is managed by
  // administration_dialog. http://b/2889759
  if constexpr (!port::IsWindows()) {
    const bool val = StatsConfigUtil::IsEnabled();
    usageStatsCheckBox->setChecked(val);
  }
}

void ConfigDialog::GetSendStatsCheckBox() const {
  // On windows, usage_stats flag is managed by
  // administration_dialog. http://b/2889759
  if constexpr (!port::IsWindows()) {
    const bool val = usageStatsCheckBox->isChecked();
    StatsConfigUtil::SetEnabled(val);
  }
}

#define SET_COMBOBOX(combobox, enumname, field)                    \
  do {                                                             \
    (combobox)->setCurrentIndex(static_cast<int>(config.field())); \
  } while (0)

#define SET_CHECKBOX(checkbox, field)       \
  do {                                      \
    (checkbox)->setChecked(config.field()); \
  } while (0)

#define GET_COMBOBOX(combobox, enumname, field)                              \
  do {                                                                       \
    config->set_##field(                                                     \
        static_cast<config::Config_##enumname>((combobox)->currentIndex())); \
  } while (0)

#define GET_CHECKBOX(checkbox, field)             \
  do {                                            \
    config->set_##field((checkbox)->isChecked()); \
  } while (0)

namespace {
static constexpr int kPreeditMethodSize = 2;

void SetComboboxForPreeditMethod(const config::Config& config,
                                 QComboBox* combobox) {
  int index = static_cast<int>(config.preedit_method());
  if constexpr (port::IsWindows()) {
    if (config.use_keyboard_to_change_preedit_method()) {
      index += kPreeditMethodSize;
    }
  }
  combobox->setCurrentIndex(index);
}

void GetComboboxForPreeditMethod(const QComboBox* combobox,
                                 config::Config* config) {
  int index = combobox->currentIndex();
  if (index >= kPreeditMethodSize) {
    // |use_keyboard_to_change_preedit_method| should be true and
    // |index| should be adjusted to smaller than kPreeditMethodSize.
    config->set_preedit_method(
        static_cast<config::Config_PreeditMethod>(index - kPreeditMethodSize));
    config->set_use_keyboard_to_change_preedit_method(true);
  } else {
    config->set_preedit_method(
        static_cast<config::Config_PreeditMethod>(index));
    config->set_use_keyboard_to_change_preedit_method(false);
  }
}
}  // namespace

// The characters Mozkey offers for committing on a typed character.
constexpr const char* kDirectCommitCharacters[] = {"。", "、", "？", "！", "（",
                                                   "）", "「", "」", "・"};

namespace {
QString Text(const char* utf8) { return QString::fromUtf8(utf8); }

// The mark on each setting floatype added to Mozc's.
QLabel* NewBadge() {
  QLabel* badge = new QLabel(QStringLiteral("F"));
  badge->setFixedSize(16, 16);
  badge->setAlignment(Qt::AlignCenter);
  badge->setStyleSheet(QStringLiteral(
      "background-color: black; color: white; border-radius: 4px;"
      " font-weight: bold; font-size: 11px;"));
  badge->setToolTip(Text("Floatype の項目"));
  badge->setAccessibleName(Text("Floatype の項目"));
  return badge;
}

// `widget`, then the badge, packed to the left.
QWidget* WithBadge(QWidget* widget) {
  QWidget* row = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(widget);
  layout->addWidget(NewBadge());
  layout->addStretch();
  return row;
}

// A group that looks like Mozc's in the .ui file (a title and a line over its
// settings) with the badge after the title, put into `tab` at `index`.
QFormLayout* InsertGroup(QVBoxLayout* tab, int index, const char* title) {
  QFrame* header = new QFrame();
  header->setProperty("class", QStringLiteral("setting-group-header"));
  QHBoxLayout* header_layout = new QHBoxLayout(header);
  header_layout->addWidget(new QLabel(Text(title)));
  header_layout->addWidget(NewBadge());
  QFrame* line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  line->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
  header_layout->addWidget(line);
  QFrame* group = new QFrame();
  group->setProperty("class", QStringLiteral("setting-group-layout"));
  QFormLayout* form = new QFormLayout(group);
  // As macOS lays out forms, and as the spin box in Mozc's Suggest tab.
  form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  tab->insertWidget(index, header);
  tab->insertWidget(index + 1, group);
  return form;
}

QSpinBox* NewSpinBox(int min, int max, int step, const char* suffix) {
  QSpinBox* box = new QSpinBox();
  box->setRange(min, max);
  box->setSingleStep(step);
  box->setSuffix(Text(suffix));
  return box;
}

QCheckBox* AddCheckBox(QFormLayout* form, const char* utf8) {
  QCheckBox* box = new QCheckBox(Text(utf8));
  form->addRow(box);
  return box;
}

QComboBox* AddComboBox(QFormLayout* form, const char* label,
                       std::initializer_list<const char*> items) {
  QComboBox* box = new QComboBox();
  for (const char* item : items) box->addItem(Text(item));
  form->addRow(Text(label), box);
  return box;
}
}  // namespace

void ConfigDialog::SetUpFloatypeSettings() {
  // 一般
  const int general = basicTabLayout->indexOf(basicTabBottomSpacer);
  QFormLayout* live = InsertGroup(basicTabLayout, general, "ライブ変換");
  liveConversionCheckBox_ =
      AddCheckBox(live, "手が止まったら Space を押さずに変換する");
  liveConversionDelaySpinBox_ = NewSpinBox(0, 2000, 10, " ms");
  live->addRow(Text("変換するまでの待ち時間"), liveConversionDelaySpinBox_);
  liveConversionMinCharsSpinBox_ = NewSpinBox(1, 10, 1, " 文字");
  live->addRow(Text("変換を始める文字数"), liveConversionMinCharsSpinBox_);

  QFormLayout* zenz = InsertGroup(basicTabLayout, general + 2, "zenz の補正");
  useZenzCheckBox_ = AddCheckBox(zenz, "zenz で変換を補正する");
  zenzDelaySpinBox_ = NewSpinBox(0, 5000, 10, " ms");
  zenz->addRow(Text("zenz に頼むまでの待ち時間"), zenzDelaySpinBox_);
  zenzTimeoutSpinBox_ = NewSpinBox(50, 5000, 50, " ms");
  zenz->addRow(Text("答えを待つ上限"), zenzTimeoutSpinBox_);
  liveConversionWaitSpinBox_ = NewSpinBox(0, 1000, 10, " ms");
  zenz->addRow(Text("ライブ変換で zenz の答えを待つ上限（0 で待たない）"),
               liveConversionWaitSpinBox_);

  QFormLayout* assist = InsertGroup(basicTabLayout, general + 4, "入力の補助");
  useDirectCommitCheckBox_ = AddCheckBox(
      assist, "句読点・記号を打ったら、zenz を待ってその場で確定する");
  // One row, packed to the left: spread over the form's width they did not
  // read as checkboxes for their characters.
  QWidget* characters = new QWidget();
  QHBoxLayout* row = new QHBoxLayout(characters);
  row->setContentsMargins(0, 0, 0, 0);
  for (const char* character : kDirectCommitCharacters) {
    QCheckBox* box = new QCheckBox(Text(character), characters);
    row->addWidget(box);
    directCommitCheckBoxes_.push_back(box);
    connect(useDirectCommitCheckBox_, &QCheckBox::toggled, box,
            &QWidget::setEnabled);
  }
  row->addStretch();
  assist->addRow(Text("確定する文字"), characters);
  mixedEnglishCheckBox_ = AddCheckBox(
      assist,
      "かなの入力中に打った英単語を英字のままにし、インジケータに あ / A "
      "を出す");
  displayAmbiguousCheckBox_ = AddCheckBox(
      assist, "長い規則の途中のローマ字を、いまの かな で見せる（n を ん で）");

  // 詳細 (入力補助 in Mozc's Japanese text). It has no spacer at its end.
  QFormLayout* memory =
      InsertGroup(inputSupportTabLayout, inputSupportTabLayout->count(),
                  "zenz のメモリと学習");
  zenzStopWhenIdleCheckBox_ =
      AddCheckBox(memory,
                  "打っていない間は止める（待機中のメモリ 約 145MB → 約 51MB。"
                  "1 分で止まり、次の 1 文字目で起きる）");
  zenzLowMemoryCheckBox_ =
      AddCheckBox(memory,
                  "軽くする（動いている間のメモリ 約 145MB → 約 87MB、"
                  "補正にかかる時間 約 26ms → 約 50ms）");
  zenzLearningCheckBox_ =
      AddCheckBox(memory,
                  "補正の表示中に Ctrl+Delete "
                  "で止めた補正を覚え、同じ読みでは繰り返さない");
  zenzSuggestionLearningCheckBox_ =
      AddCheckBox(memory, "zenz が直した確定を予測候補に覚えさせる");

  // サジェスト
  suggestAddedWordsCheckBox_ = new QCheckBox(
      Text("足した辞書（人名、地名、SudachiDict、ニコニコ・pixiv）の語も出す"
           "（オフなら、読みを最後まで打って変換したときだけ出る）"));
  gridLayout_4->addWidget(WithBadge(suggestAddedWordsCheckBox_),
                          gridLayout_4->rowCount(), 0, 1, 3);

  // 外観
  QWidget* look = new QWidget();
  QVBoxLayout* look_layout = new QVBoxLayout(look);
  look_layout->setSpacing(0);  // As Mozc's tabs.
  QFormLayout* candidates = InsertGroup(look_layout, 0, "候補の窓と読み");
  candidateFontScaleSpinBox_ = NewSpinBox(50, 300, 10, " %");
  candidates->addRow(Text("文字の大きさ"), candidateFontScaleSpinBox_);
  liveConversionRubyCheckBox_ =
      AddCheckBox(candidates, "ライブ変換の上に読みを小さく出す");
  QFormLayout* indicator = InsertGroup(look_layout, 2, "状態インジケータ");
  showIndicatorCheckBox_ = AddCheckBox(indicator, "入力中の文字の近くに出す");
  indicatorPlacementComboBox_ =
      AddComboBox(indicator, "位置", {"入力中の文字の上", "下", "左横"});
  indicatorLabelComboBox_ = AddComboBox(
      indicator, "文字", {"入力中 / Mozc / Zenz", "あ / M / Z", "色だけ"});
  indicatorShapeComboBox_ =
      AddComboBox(indicator, "形", {"角丸の帯", "角丸の四角", "丸"});
  QLabel* note = new QLabel(
      Text("色や大きさは config.toml で変えられます（例は floatype.app の "
           "Resources/config.example.toml）。"));
  note->setWordWrap(true);
  indicator->addRow(note);
  look_layout->addStretch();
  configDialogTabWidget->insertTab(
      configDialogTabWidget->indexOf(suggestionsTab) + 1, look, Text("外観"));

  // Settings that only matter while their switch is on follow it.
  for (QWidget* widget : std::initializer_list<QWidget*>{
           zenzTimeoutSpinBox_, zenzSuggestionLearningCheckBox_,
           zenzLearningCheckBox_, zenzStopWhenIdleCheckBox_,
           zenzLowMemoryCheckBox_, zenzDelaySpinBox_}) {
    connect(useZenzCheckBox_, &QCheckBox::toggled, widget,
            &QWidget::setEnabled);
  }
  for (QWidget* widget : std::initializer_list<QWidget*>{
           liveConversionDelaySpinBox_, liveConversionMinCharsSpinBox_,
           liveConversionRubyCheckBox_, liveConversionWaitSpinBox_}) {
    connect(liveConversionCheckBox_, &QCheckBox::toggled, widget,
            &QWidget::setEnabled);
  }
  for (QWidget* widget : std::initializer_list<QWidget*>{
           indicatorPlacementComboBox_, indicatorLabelComboBox_,
           indicatorShapeComboBox_}) {
    connect(showIndicatorCheckBox_, &QCheckBox::toggled, widget,
            &QWidget::setEnabled);
  }
}

// Bottom of the window: what the badge means, where the settings go, and
// Mozc's reset. Changes are saved as they are made, so no OK / Cancel / Apply.
void ConfigDialog::SetUpFooter() {
  configDialogButtonBox->hide();
  footerLayout->removeWidget(resetToDefaultsButton);
  footerLayout->insertWidget(0, NewBadge());
  footerLayout->insertWidget(1, new QLabel(Text("Floatype で足した項目")));
  footerLayout->addWidget(new QLabel(
      Text("保存先 ~/.config/floatype/config.toml（変えるとすぐ保存）")));
  QPushButton* reveal = new QPushButton(Text("Finder で表示"));
  connect(reveal, &QPushButton::clicked, this, [] {
    const QString path = QDir::homePath() + "/.config/floatype/config.toml";
    if (QFileInfo::exists(path)) {
      QProcess::startDetached(QStringLiteral("open"), {"-R", path});
    } else {
      QDesktopServices::openUrl(
          QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
  });
  footerLayout->addWidget(reveal);
  footerLayout->addWidget(resetToDefaultsButton);
}

// TODO(taku)
// Actually ConvertFromProto and ConvertToProto are almost the same.
// The difference only SET_ and GET_. We would like to unify the twos.
void ConfigDialog::ConvertFromProto(const config::Config& config) {
  base_config_ = config;
  // tab1
  SetComboboxForPreeditMethod(config, inputModeComboBox);
  SET_COMBOBOX(punctuationsSettingComboBox, PunctuationMethod,
               punctuation_method);
  SET_COMBOBOX(symbolsSettingComboBox, SymbolMethod, symbol_method);
  SET_COMBOBOX(spaceCharacterFormComboBox, FundamentalCharacterForm,
               space_character_form);
  SET_COMBOBOX(selectionShortcutModeComboBox, SelectionShortcut,
               selection_shortcut);
  SET_COMBOBOX(numpadCharacterFormComboBox, NumpadCharacterForm,
               numpad_character_form);
  SET_COMBOBOX(keymapSettingComboBox, SessionKeymap, session_keymap);

  custom_keymap_table_ = config.custom_keymap_table();
  custom_roman_table_ = config.custom_roman_table();

  // tab2
  SET_COMBOBOX(historyLearningLevelComboBox, HistoryLearningLevel,
               history_learning_level);
  SET_CHECKBOX(singleKanjiConversionCheckBox, use_single_kanji_conversion);
  SET_CHECKBOX(symbolConversionCheckBox, use_symbol_conversion);
  SET_CHECKBOX(emoticonConversionCheckBox, use_emoticon_conversion);
  SET_CHECKBOX(dateConversionCheckBox, use_date_conversion);
  SET_CHECKBOX(emojiConversionCheckBox, use_emoji_conversion);
  SET_CHECKBOX(numberConversionCheckBox, use_number_conversion);
  SET_CHECKBOX(calculatorCheckBox, use_calculator);
  SET_CHECKBOX(t13nConversionCheckBox, use_t13n_conversion);
  SET_CHECKBOX(zipcodeConversionCheckBox, use_zip_code_conversion);
  SET_CHECKBOX(spellingCorrectionCheckBox, use_spelling_correction);

  // InfoListConfig
  localUsageDictionaryCheckBox->setChecked(
      config.information_list_config().use_local_usage_dictionary());

  // tab3
  SET_CHECKBOX(autoSwitchCompositionMode, auto_switch_composition_mode);

  SET_CHECKBOX(useAutoConversion, use_auto_conversion);
  kutenCheckBox->setChecked(config.auto_conversion_key() &
                            config::Config::AUTO_CONVERSION_KUTEN);
  toutenCheckBox->setChecked(config.auto_conversion_key() &
                             config::Config::AUTO_CONVERSION_TOUTEN);
  questionMarkCheckBox->setChecked(
      config.auto_conversion_key() &
      config::Config::AUTO_CONVERSION_QUESTION_MARK);
  exclamationMarkCheckBox->setChecked(
      config.auto_conversion_key() &
      config::Config::AUTO_CONVERSION_EXCLAMATION_MARK);

  SET_COMBOBOX(shiftKeyModeSwitchComboBox, ShiftKeyModeSwitch,
               shift_key_mode_switch);

  SET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  SET_CHECKBOX(useModeIndicator, use_mode_indicator);

  // tab4
  SET_CHECKBOX(historySuggestCheckBox, use_history_suggest);
  SET_CHECKBOX(dictionarySuggestCheckBox, use_dictionary_suggest);
  SET_CHECKBOX(realtimeConversionCheckBox, use_realtime_conversion);

  suggestionsSizeSpinBox->setValue(
      std::clamp<int>(config.suggestions_size(), 1, 9));

  // floatype
  SET_CHECKBOX(useZenzCheckBox_, use_zenz);
  zenzTimeoutSpinBox_->setValue(config.zenz_timeout_millisec());
  SET_CHECKBOX(zenzSuggestionLearningCheckBox_, zenz_suggestion_learning);
  SET_CHECKBOX(zenzLearningCheckBox_, zenz_learning);
  SET_CHECKBOX(zenzStopWhenIdleCheckBox_, zenz_stop_when_idle);
  SET_CHECKBOX(zenzLowMemoryCheckBox_, zenz_low_memory);
  SET_CHECKBOX(liveConversionCheckBox_, use_live_conversion);
  liveConversionDelaySpinBox_->setValue(
      config.live_conversion_delay_millisec());
  liveConversionMinCharsSpinBox_->setValue(config.live_conversion_min_chars());
  zenzDelaySpinBox_->setValue(config.zenz_delay_millisec());
  SET_CHECKBOX(liveConversionRubyCheckBox_, live_conversion_ruby);
  liveConversionWaitSpinBox_->setValue(
      config.live_conversion_waits_for_zenz_millisec());
  SET_CHECKBOX(useDirectCommitCheckBox_, use_direct_commit);
  for (size_t i = 0; i < directCommitCheckBoxes_.size(); ++i) {
    directCommitCheckBoxes_[i]->setChecked(absl::StrContains(
        config.direct_commit_characters(), kDirectCommitCharacters[i]));
    directCommitCheckBoxes_[i]->setEnabled(config.use_direct_commit());
  }
  SET_CHECKBOX(mixedEnglishCheckBox_, mixed_english);
  SET_CHECKBOX(suggestAddedWordsCheckBox_, suggest_added_words);
  SET_CHECKBOX(displayAmbiguousCheckBox_, display_ambiguous_romaji);
  candidateFontScaleSpinBox_->setValue(config.candidate_style().size_percent());
  SET_CHECKBOX(showIndicatorCheckBox_, show_indicator);
  indicatorPlacementComboBox_->setCurrentIndex(
      config.indicator_style().placement());
  indicatorLabelComboBox_->setCurrentIndex(config.indicator_style().label());
  indicatorShapeComboBox_->setCurrentIndex(config.indicator_style().shape());

  // tab5
  SetSendStatsCheckBox();
  SET_CHECKBOX(incognitoModeCheckBox, incognito_mode);
  SET_CHECKBOX(presentationModeCheckBox, presentation_mode);

  // tab6
  SET_COMBOBOX(verboseLevelComboBox, int, verbose_level);
  SET_CHECKBOX(checkDefaultCheckBox, check_default);
  SET_COMBOBOX(yenSignComboBox, YenSignCharacter, yen_sign_character);

  characterFormEditor->Load(config);

#ifdef __APPLE__
  startupCheckBox->setChecked(MacUtil::CheckPrelauncherLoginItemStatus());
#endif  // __APPLE__
}

void ConfigDialog::ConvertToProto(config::Config* config) const {
  *config = base_config_;

  // tab1
  GetComboboxForPreeditMethod(inputModeComboBox, config);
  GET_COMBOBOX(punctuationsSettingComboBox, PunctuationMethod,
               punctuation_method);
  GET_COMBOBOX(symbolsSettingComboBox, SymbolMethod, symbol_method);
  GET_COMBOBOX(spaceCharacterFormComboBox, FundamentalCharacterForm,
               space_character_form);
  GET_COMBOBOX(selectionShortcutModeComboBox, SelectionShortcut,
               selection_shortcut);
  GET_COMBOBOX(numpadCharacterFormComboBox, NumpadCharacterForm,
               numpad_character_form);
  GET_COMBOBOX(keymapSettingComboBox, SessionKeymap, session_keymap);

  config->set_custom_keymap_table(custom_keymap_table_);

  config->clear_custom_roman_table();
  if (!custom_roman_table_.empty()) {
    config->set_custom_roman_table(custom_roman_table_);
  }

  // tab2
  GET_COMBOBOX(historyLearningLevelComboBox, HistoryLearningLevel,
               history_learning_level);
  GET_CHECKBOX(singleKanjiConversionCheckBox, use_single_kanji_conversion);
  GET_CHECKBOX(symbolConversionCheckBox, use_symbol_conversion);
  GET_CHECKBOX(emoticonConversionCheckBox, use_emoticon_conversion);
  GET_CHECKBOX(dateConversionCheckBox, use_date_conversion);
  GET_CHECKBOX(emojiConversionCheckBox, use_emoji_conversion);
  GET_CHECKBOX(numberConversionCheckBox, use_number_conversion);
  GET_CHECKBOX(calculatorCheckBox, use_calculator);
  GET_CHECKBOX(t13nConversionCheckBox, use_t13n_conversion);
  GET_CHECKBOX(zipcodeConversionCheckBox, use_zip_code_conversion);
  GET_CHECKBOX(spellingCorrectionCheckBox, use_spelling_correction);

  // InformationListConfig
  config->mutable_information_list_config()->set_use_local_usage_dictionary(
      localUsageDictionaryCheckBox->isChecked());

  // tab3
  GET_CHECKBOX(autoSwitchCompositionMode, auto_switch_composition_mode);

  GET_CHECKBOX(useAutoConversion, use_auto_conversion);

  GET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  GET_CHECKBOX(useModeIndicator, use_mode_indicator);

  uint32_t auto_conversion_key = 0;
  if (kutenCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_KUTEN;
  }
  if (toutenCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_TOUTEN;
  }
  if (questionMarkCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_QUESTION_MARK;
  }
  if (exclamationMarkCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_EXCLAMATION_MARK;
  }
  config->set_auto_conversion_key(auto_conversion_key);

  GET_COMBOBOX(shiftKeyModeSwitchComboBox, ShiftKeyModeSwitch,
               shift_key_mode_switch);

  // tab4
  GET_CHECKBOX(historySuggestCheckBox, use_history_suggest);
  GET_CHECKBOX(dictionarySuggestCheckBox, use_dictionary_suggest);
  GET_CHECKBOX(realtimeConversionCheckBox, use_realtime_conversion);

  config->set_suggestions_size(
      static_cast<uint32_t>(suggestionsSizeSpinBox->value()));

  // floatype
  GET_CHECKBOX(useZenzCheckBox_, use_zenz);
  config->set_zenz_timeout_millisec(zenzTimeoutSpinBox_->value());
  GET_CHECKBOX(zenzSuggestionLearningCheckBox_, zenz_suggestion_learning);
  GET_CHECKBOX(zenzLearningCheckBox_, zenz_learning);
  GET_CHECKBOX(zenzStopWhenIdleCheckBox_, zenz_stop_when_idle);
  GET_CHECKBOX(zenzLowMemoryCheckBox_, zenz_low_memory);
  GET_CHECKBOX(liveConversionCheckBox_, use_live_conversion);
  config->set_live_conversion_delay_millisec(
      liveConversionDelaySpinBox_->value());
  config->set_live_conversion_min_chars(
      liveConversionMinCharsSpinBox_->value());
  config->set_zenz_delay_millisec(zenzDelaySpinBox_->value());
  GET_CHECKBOX(liveConversionRubyCheckBox_, live_conversion_ruby);
  config->set_live_conversion_waits_for_zenz_millisec(
      liveConversionWaitSpinBox_->value());
  GET_CHECKBOX(useDirectCommitCheckBox_, use_direct_commit);
  std::string characters;
  for (size_t i = 0; i < directCommitCheckBoxes_.size(); ++i) {
    if (directCommitCheckBoxes_[i]->isChecked()) {
      characters += kDirectCommitCharacters[i];
    }
  }
  config->set_direct_commit_characters(characters);
  GET_CHECKBOX(mixedEnglishCheckBox_, mixed_english);
  GET_CHECKBOX(suggestAddedWordsCheckBox_, suggest_added_words);
  GET_CHECKBOX(displayAmbiguousCheckBox_, display_ambiguous_romaji);
  config->mutable_candidate_style()->set_size_percent(
      candidateFontScaleSpinBox_->value());
  GET_CHECKBOX(showIndicatorCheckBox_, show_indicator);
  config::Config::IndicatorStyle* style = config->mutable_indicator_style();
  style->set_placement(static_cast<config::Config::IndicatorStyle::Placement>(
      indicatorPlacementComboBox_->currentIndex()));
  style->set_label(static_cast<config::Config::IndicatorStyle::Label>(
      indicatorLabelComboBox_->currentIndex()));
  style->set_shape(static_cast<config::Config::IndicatorStyle::Shape>(
      indicatorShapeComboBox_->currentIndex()));

  // tab5
  GetSendStatsCheckBox();
  GET_CHECKBOX(incognitoModeCheckBox, incognito_mode);
  GET_CHECKBOX(presentationModeCheckBox, presentation_mode);

  // tab6
  config->set_verbose_level(verboseLevelComboBox->currentIndex());
  GET_CHECKBOX(checkDefaultCheckBox, check_default);
  GET_COMBOBOX(yenSignComboBox, YenSignCharacter, yen_sign_character);

  characterFormEditor->Save(config);
}

#undef SET_COMBOBOX
#undef SET_CHECKBOX
#undef GET_COMBOBOX
#undef GET_CHECKBOX

void ConfigDialog::ClearUserHistory() {
  if (QMessageBox::Ok !=
      QMessageBox::question(
          this, windowTitle(), tr("Do you want to clear all history data?"),
          QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel)) {
    return;
  }

  client_->CheckVersionOrRestartServer();

  if (!client_->ClearUserHistory() || !client_->ClearUserPrediction()) {
    QMessageBox::critical(this, windowTitle(),
                          tr("%1 Converter is not running. "
                             "Settings were not saved.")
                              .arg(GuiUtil::ProductName()));
  }
}

void ConfigDialog::EditUserDictionary() {
  client_->LaunchTool("dictionary_tool", "");
}

void ConfigDialog::EditKeymap() {
  std::string current_keymap_table = "";
  const QString keymap_name = keymapSettingComboBox->currentText();
  const std::map<QString, config::Config::SessionKeymap>::const_iterator itr =
      keymapname_sessionkeymap_map_.find(keymap_name);
  if (itr != keymapname_sessionkeymap_map_.end()) {
    // Load from predefined mapping file.
    const char* keymap_file =
        keymap::KeyMapManager::GetKeyMapFileName(itr->second);
    std::unique_ptr<std::istream> ifs(
        ConfigFileStream::LegacyOpen(keymap_file));
    CHECK(ifs.get() != nullptr);  // should never happen
    std::stringstream buffer;
    buffer << ifs->rdbuf();
    current_keymap_table = buffer.str();
  } else {
    current_keymap_table = custom_keymap_table_;
  }
  std::string output;
  if (gui::KeyMapEditorDialog::Show(this, current_keymap_table, &output)) {
    custom_keymap_table_ = output;
    // set keymapSettingComboBox to "Custom keymap"
    keymapSettingComboBox->setCurrentIndex(0);
    save_timer_.start();
  }
}

void ConfigDialog::EditRomanTable() {
  std::string output;
  if (gui::RomanTableEditorDialog::Show(this, custom_roman_table_, &output)) {
    custom_roman_table_ = output;
    save_timer_.start();
  }
}

void ConfigDialog::SelectInputModeSetting(int index) {
  // enable "EDIT" button if roman mode is selected
  editRomanTableButton->setEnabled((index == 0));
}

void ConfigDialog::SelectAutoConversionSetting(int state) {
  kutenCheckBox->setEnabled(static_cast<bool>(state));
  toutenCheckBox->setEnabled(static_cast<bool>(state));
  questionMarkCheckBox->setEnabled(static_cast<bool>(state));
  exclamationMarkCheckBox->setEnabled(static_cast<bool>(state));
}

void ConfigDialog::SelectSuggestionSetting(int state) {
  if (historySuggestCheckBox->isChecked() ||
      dictionarySuggestCheckBox->isChecked() ||
      realtimeConversionCheckBox->isChecked()) {
    presentationModeCheckBox->setEnabled(true);
  } else {
    presentationModeCheckBox->setEnabled(false);
  }
}

void ConfigDialog::ResetToDefaults() {
  const QString message =
      tr("When you reset %1 settings, any changes "
         "you've made will be reverted to the default settings. "
         "Do you want to reset settings? "
         "The following items are not reset with this operation.\n"
         " - Personalization data\n"
         " - Input history\n"
         " - Usage statistics and crash reports\n"
         " - Administrator settings")
          .arg(GuiUtil::ProductName());
  if (QMessageBox::Ok ==
      QMessageBox::question(this, windowTitle(), message,
                            QMessageBox::Ok | QMessageBox::Cancel,
                            QMessageBox::Cancel)) {
    // TODO(taku): remove the dependency to config::ConfigHandler
    // nice to have GET_DEFAULT_CONFIG command
    ConvertFromProto(config::ConfigHandler::DefaultConfig());
    save_timer_.start();
  }
}

void ConfigDialog::LaunchAdministrationDialog() {
  if constexpr (port::IsWindows()) {
    client_->LaunchTool("administration_dialog", "");
  }
}

// Catch MouseButtonRelease event to toggle the CheckBoxes
bool ConfigDialog::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::MouseButtonRelease) {
    if (obj == usageStatsMessage) {
#ifndef CHANNEL_DEV
      usageStatsCheckBox->toggle();
#endif  // CHANNEL_DEV
    } else if (obj == incognitoModeMessage) {
      incognitoModeCheckBox->toggle();
    }
  }
  return QObject::eventFilter(obj, event);
}

}  // namespace gui
}  // namespace mozc
