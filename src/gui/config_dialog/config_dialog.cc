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

#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <istream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "base/config_file_stream.h"
#include "base/port.h"
#include "base/run_level.h"
#include "base/version.h"
#include "client/client.h"
#include "config/config_handler.h"
#include "floatype/look/window_look.h"
#include "gui/base/stats_config_util.h"
#include "gui/base/util.h"
#include "gui/config_dialog/keymap_editor.h"
#include "gui/config_dialog/look_preview.h"
#include "gui/config_dialog/roman_table_editor.h"
#include "gui/config_dialog/zenz_memory_dialog.h"
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
  SetUpUpdate();
  WrapTabsInScrollAreas();

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

  // Reset texts explicitly for translations.
  configDialogButtonBox->button(QDialogButtonBox::Ok)->setText(tr("  Ok  "));
  configDialogButtonBox->button(QDialogButtonBox::Cancel)
      ->setText(tr("Cancel"));
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));

  // signal/slot
  QObject::connect(configDialogButtonBox, SIGNAL(clicked(QAbstractButton*)),
                   this, SLOT(clicked(QAbstractButton*)));
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

  // Event handlers to enable 'Apply' button, as Mozc's, made after Reload()
  // so that loading leaves it off. floatype listens to the value signals, so
  // that every change counts, and redraws the 外観 preview, which reads the
  // controls.
  const auto changed = [this] {
    EnableApplyButton();
    RefreshLook();
  };
  for (QPushButton* button : findChildren<QPushButton*>()) {
    // Not OK / Cancel / Apply, nor the preview's switches, which are
    // checkable, nor "Finder で表示": they change no setting.
    if (button->parentWidget() != configDialogButtonBox &&
        !button->isCheckable() &&
        !button->property("changes_nothing").toBool()) {
      connect(button, &QPushButton::clicked, this,
              &ConfigDialog::EnableApplyButton);
    }
  }
  for (QCheckBox* box : findChildren<QCheckBox*>()) {
    connect(box, &QCheckBox::toggled, this, changed);
  }
  for (QSpinBox* box : findChildren<QSpinBox*>()) {
    connect(box, &QSpinBox::valueChanged, this, changed);
  }
  for (QComboBox* box : findChildren<QComboBox*>()) {
    connect(box, &QComboBox::currentIndexChanged, this, changed);
  }
  for (QSlider* slider : findChildren<QSlider*>()) {
    connect(slider, &QSlider::valueChanged, this, changed);
  }
  connect(characterFormEditor, &QTableWidget::itemChanged, this,
          &ConfigDialog::EnableApplyButton);
  // floatype: made after loading, when the table's rows are whole.
  connect(characterFormEditor, &QTableWidget::itemChanged, this,
          &ConfigDialog::RefreshNumberForm);
  // 'Apply' button is disabled on launching.
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
  // In scroll areas, a wheel turned over a control scrolls the page unless
  // the control has the focus: otherwise scrolling would change settings.
  for (QWidget* widget : findChildren<QWidget*>()) {
    if (qobject_cast<QAbstractSpinBox*>(widget) != nullptr ||
        qobject_cast<QComboBox*>(widget) != nullptr ||
        qobject_cast<QSlider*>(widget) != nullptr) {
      widget->setFocusPolicy(Qt::StrongFocus);
      widget->installEventFilter(this);
    }
  }
  // As wide as the widest page, and short enough for a 13" screen; the
  // pages scroll.
  resize(minimumSizeHint().width(), 680);
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

// Gives the forms' labels one width, the widest's.
void AlignLabels(std::initializer_list<QFormLayout*> forms) {
  std::vector<QWidget*> labels;
  int width = 0;
  for (QFormLayout* form : forms) {
    for (int row = 0; row < form->rowCount(); ++row) {
      if (QLayoutItem* item = form->itemAt(row, QFormLayout::LabelRole);
          item != nullptr && item->widget() != nullptr) {
        labels.push_back(item->widget());
        width = std::max(width, item->widget()->sizeHint().width());
      }
    }
  }
  for (QWidget* label : labels) label->setMinimumWidth(width);
}

QColor Color(uint32_t rgb) {
  return QColor((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

const google::protobuf::FieldDescriptor* ColorField(
    const QPushButton* button, const google::protobuf::Message& style) {
  return style.GetDescriptor()->FindFieldByName(
      button->property("field").toString().toStdString());
}

// The button's color from `style`: set, or unset (no "rgb").
void LoadColor(QPushButton* button, const google::protobuf::Message& style) {
  const auto* field = ColorField(button, style);
  button->setProperty(
      "rgb", style.GetReflection()->HasField(style, field)
                 ? QVariant(style.GetReflection()->GetUInt32(style, field))
                 : QVariant());
}

void SaveColor(const QPushButton* button, google::protobuf::Message* style) {
  const auto* field = ColorField(button, *style);
  if (const QVariant rgb = button->property("rgb"); rgb.isValid()) {
    style->GetReflection()->SetUInt32(style, field, rgb.toUInt());
  } else {
    style->GetReflection()->ClearField(style, field);
  }
}

// Shows the color `shown` gives the button's field: a swatch and its hex.
void ShowColor(QPushButton* button, const google::protobuf::Message& shown) {
  const uint32_t rgb =
      shown.GetReflection()->GetUInt32(shown, ColorField(button, shown));
  QPixmap swatch(28, 28);
  swatch.setDevicePixelRatio(2);
  swatch.fill(Qt::transparent);
  QPainter painter(&swatch);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QColor(0, 0, 0, 80));
  painter.setBrush(Color(rgb));
  painter.drawRoundedRect(QRectF(0.5, 0.5, 13, 13), 3, 3);
  painter.end();
  button->setIcon(QIcon(swatch));
  button->setText(QStringLiteral("#%1").arg(rgb, 6, 16, QChar('0')).toUpper());
  button->setProperty("shown", rgb);
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
  zenzContextFromCommitsCheckBox_ = AddCheckBox(
      zenz,
      "前の文を教えないアプリ（ターミナルなど）では、直前に確定した文を "
      "zenz に見せる");

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
  mixedEnglishSpaceCommitsCheckBox_ = AddCheckBox(
      assist,
      "　└ 英単語のあとの Space は、変換せずに確定して半角の空白を入れる");
  mixedEnglishEnterCommitsCheckBox_ = AddCheckBox(
      assist,
      "　└ 英単語のあとすぐ Enter を押しても、英字で確定する（update）");
  mixedEnglishLeadingSymbolsCheckBox_ = AddCheckBox(
      assist,
      "　└ かなの無い入力は、頭の記号も打ったとおりにする（/review、"
      "--help）");
  for (QWidget* part : std::initializer_list<QWidget*>{
           mixedEnglishSpaceCommitsCheckBox_, mixedEnglishEnterCommitsCheckBox_,
           mixedEnglishLeadingSymbolsCheckBox_}) {
    connect(mixedEnglishCheckBox_, &QCheckBox::toggled, part,
            &QWidget::setEnabled);
  }
  slashAfterAsciiCheckBox_ = AddCheckBox(
      assist,
      "英字や数字のすぐ後の / を、・ ではなく / のまま打つ（2026/11/20、"
      "test/spec）");
  displayAmbiguousCheckBox_ = AddCheckBox(
      assist, "長い規則の途中のローマ字を、いまの かな で見せる（n を ん で）");

  // 詳細 (入力補助 in Mozc's Japanese text). It has no spacer at its end.
  QFormLayout* memory =
      InsertGroup(inputSupportTabLayout, inputSupportTabLayout->count(),
                  "zenz のメモリと学習");
  zenzStopWhenIdleCheckBox_ =
      AddCheckBox(memory,
                  "打っていない間は止める（zenz のメモリ 約 30MB を返す。"
                  "1 分で止まり、次の 1 文字目で起きる）");
  zenzLearningCheckBox_ =
      AddCheckBox(memory,
                  "zenz の補正の採用と却下を覚える（確定した補正、"
                  "退けた語、control+delete で止めた補正）");
  zenzSuggestionLearningCheckBox_ =
      AddCheckBox(memory, "zenz が直した確定を予測候補に覚えさせる");
  zenzTeachesMozcCheckBox_ = AddCheckBox(
      memory,
      "zenz が直した語を、確定したら変換の順位にも覚えさせる（次から zenz "
      "を待たずに出る。zenz は、覚えた語を別の文で直せる）");
  zenzRememberedMinCharsSpinBox_ = NewSpinBox(1, 20, 1, " 文字以上");
  memory->addRow(Text("覚えた補正をすぐ出す読みの長さ（1 でいつも）"),
                 zenzRememberedMinCharsSpinBox_);
  QPushButton* learned = new QPushButton(Text("学習データを見る…"));
  learned->setObjectName(QStringLiteral("zenzMemoryButton"));
  learned->setProperty("changes_nothing", true);
  connect(learned, &QPushButton::clicked, this,
          [this] { NewZenzMemoryDialog(this)->exec(); });
  QHBoxLayout* learned_row = new QHBoxLayout();
  learned_row->addWidget(learned);
  learned_row->addStretch();
  memory->addRow(learned_row);

  // 一般: the width of digits, the 全角・半角 table's digit row in one choice.
  numberFormComboBox_ = new QComboBox();
  for (const char* item :
       {"半角", "全角", "打つと全角・変換は前回の形（Mozc の既定）",
        "入力補助タブの表で個別に設定"}) {
    numberFormComboBox_->addItem(Text(item));
  }
  {
    const int row = gridLayout->rowCount();
    gridLayout->addWidget(WithBadge(new QLabel(Text("数字の入力"))), row, 0);
    gridLayout->addWidget(numberFormComboBox_, row, 1);
  }
  connect(numberFormComboBox_, &QComboBox::activated, this, [this](int index) {
    constexpr std::pair<config::Config::CharacterForm,
                        config::Config::CharacterForm>
        kForms[] = {{config::Config::HALF_WIDTH, config::Config::HALF_WIDTH},
                    {config::Config::FULL_WIDTH, config::Config::FULL_WIDTH},
                    {config::Config::FULL_WIDTH, config::Config::LAST_FORM}};
    if (index >= 0 && index < static_cast<int>(std::size(kForms))) {
      characterFormEditor->SetForms("0", kForms[index].first,
                                    kForms[index].second);
    }
    RefreshNumberForm();
  });
  // サジェスト
  suggestAddedWordsCheckBox_ = new QCheckBox(
      Text("足した辞書（人名、地名、SudachiDict、ニコニコ・pixiv）の語も出す"
           "（オフなら、読みを最後まで打って変換したときだけ出る）"));
  gridLayout_4->addWidget(WithBadge(suggestAddedWordsCheckBox_),
                          gridLayout_4->rowCount(), 0, 1, 3);

  configDialogTabWidget->insertTab(
      configDialogTabWidget->indexOf(suggestionsTab) + 1, SetUpLookTab(),
      Text("外観"));

  // One label column through 一般's groups, so that their controls line up.
  AlignLabels({live, zenz, assist});

  // Settings that only matter while their switch is on follow it.
  for (QWidget* widget : std::initializer_list<QWidget*>{
           zenzTimeoutSpinBox_, zenzSuggestionLearningCheckBox_,
           zenzTeachesMozcCheckBox_, zenzLearningCheckBox_,
           zenzRememberedMinCharsSpinBox_, zenzStopWhenIdleCheckBox_,
           zenzContextFromCommitsCheckBox_, zenzDelaySpinBox_}) {
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
           indicatorShapeComboBox_, indicatorHeightSpinBox_,
           indicatorFontSizeSpinBox_, indicatorCornerRadiusSpinBox_,
           indicatorOpacitySpinBox_}) {
    connect(showIndicatorCheckBox_, &QCheckBox::toggled, widget,
            &QWidget::setEnabled);
  }
}

// A row over Mozc's buttons: what the badge means, and where the settings
// are saved.
void ConfigDialog::SetUpFooter() {
  QHBoxLayout* legend = new QHBoxLayout();
  legend->addWidget(NewBadge());
  legend->addWidget(new QLabel(Text("Floatype で足した項目")));
  legend->addStretch();
  legend->addWidget(new QLabel(Text("保存先 ~/.config/floatype/config.toml")));
  QPushButton* reveal = new QPushButton(Text("Finder で表示"));
  reveal->setProperty("changes_nothing", true);
  connect(reveal, &QPushButton::clicked, this, [] {
    const QString path = QDir::homePath() + "/.config/floatype/config.toml";
    if (QFileInfo::exists(path)) {
      QProcess::startDetached(QStringLiteral("open"), {"-R", path});
    } else {
      QDesktopServices::openUrl(
          QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
  });
  legend->addWidget(reveal);
  centralLayout->insertLayout(centralLayout->indexOf(footerLayout), legend);
}

namespace {
QString& UpdateCurl() {
  static QString* curl = new QString(QStringLiteral("/usr/bin/curl"));
  return *curl;
}
QString& UpdateOpen() {
  static QString* open = new QString(QStringLiteral("/usr/bin/open"));
  return *open;
}
}  // namespace

void ConfigDialog::SetUpdateToolsForTesting(const QString& curl,
                                            const QString& open) {
  UpdateCurl() = curl;
  UpdateOpen() = open;
}

void ConfigDialog::SetUpUpdate() {
  QFormLayout* group = InsertGroup(miscTabLayout, 0, "更新");
  updateButton_ = new QPushButton(Text("更新を確認"));
  updateButton_->setObjectName(QStringLiteral("updateButton"));
  updateButton_->setProperty("changes_nothing", true);
  updateLabel_ = new QLabel(
      Text("今の版は %1。押したときだけ GitHub に問い合わせます")
          .arg(QString::fromStdString(Version::GetFloatypeVersion())));
  updateLabel_->setObjectName(QStringLiteral("updateLabel"));
  QHBoxLayout* row = new QHBoxLayout();
  row->addWidget(updateButton_);
  row->addWidget(updateLabel_);
  row->addStretch();
  group->addRow(row);
  connect(updateButton_, &QPushButton::clicked, this,
          &ConfigDialog::UpdateClicked);
}

void ConfigDialog::ShowUpdate(const char* text, bool busy) {
  updateLabel_->setText(Text(text));
  updateButton_->setEnabled(!busy);
}

void ConfigDialog::RunCurl(
    const QStringList& arguments, int seconds,
    std::function<void(bool ok, const QByteArray& output)> done) {
  QProcess* curl = new QProcess(this);
  connect(curl, &QProcess::finished, this,
          [curl, done](int code, QProcess::ExitStatus status) {
            done(status == QProcess::NormalExit && code == 0,
                 curl->readAllStandardOutput());
            curl->deleteLater();
          });
  connect(curl, &QProcess::errorOccurred, this,
          [curl, done](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
              done(false, QByteArray());
              curl->deleteLater();
            }
          });
  // https alone, on redirects too.
  curl->start(
      UpdateCurl(),
      QStringList{QStringLiteral("-sS"), QStringLiteral("-L"),
                  QStringLiteral("--proto"), QStringLiteral("=https"),
                  QStringLiteral("--proto-redir"), QStringLiteral("=https"),
                  QStringLiteral("--max-time"), QString::number(seconds)} +
          arguments);
}

void ConfigDialog::UpdateClicked() {
  if (update_.kind == floatype::UpdateCheck::kNewer) {
    DownloadUpdate();
  } else {
    CheckForUpdate();
  }
}

void ConfigDialog::CheckForUpdate() {
  ShowUpdate("確認しています…", /*busy=*/true);
  RunCurl(
      {QStringLiteral("-H"),
       QStringLiteral("Accept: application/vnd.github+json"),
       QStringLiteral("-w"), QStringLiteral("\n%{http_code}"),
       QString::fromUtf8(floatype::kLatestReleaseUrl.data(),
                         floatype::kLatestReleaseUrl.size())},
      20, [this](bool ok, const QByteArray& output) {
        // The status is the last line; the answer comes before it.
        const int end = output.lastIndexOf('\n');
        const QJsonObject release =
            QJsonDocument::fromJson(output.left(end)).object();
        std::vector<floatype::ReleaseAsset> assets;
        for (const QJsonValue& value :
             release.value(QStringLiteral("assets")).toArray()) {
          const QJsonObject asset = value.toObject();
          assets.push_back(
              {asset.value(QStringLiteral("name")).toString().toStdString(),
               asset.value(QStringLiteral("browser_download_url"))
                   .toString()
                   .toStdString()});
        }
        update_ = floatype::CheckLatestRelease(
            ok && end >= 0 ? output.mid(end + 1).trimmed().toInt() : 0,
            release.value(QStringLiteral("tag_name")).toString().toStdString(),
            assets, Version::GetFloatypeVersion());
        switch (update_.kind) {
          case floatype::UpdateCheck::kNewer:
            updateLabel_->setText(
                Text("新しい版 %1 があります（今は %2）")
                    .arg(
                        QString::fromStdString(update_.version),
                        QString::fromStdString(Version::GetFloatypeVersion())));
            updateButton_->setText(Text("ダウンロードして入れる"));
            updateButton_->setEnabled(true);
            break;
          case floatype::UpdateCheck::kUpToDate:
            ShowUpdate("最新です", /*busy=*/false);
            break;
          case floatype::UpdateCheck::kNoRelease:
            ShowUpdate("まだリリースがありません", /*busy=*/false);
            break;
          case floatype::UpdateCheck::kFailed:
            ShowUpdate("確認できませんでした", /*busy=*/false);
            break;
        }
      });
}

void ConfigDialog::DownloadUpdate() {
  ShowUpdate("ダウンロードしています…", /*busy=*/true);
  // Kept after the dialog closes: the Installer reads the package from it.
  auto folder = std::make_shared<QTemporaryDir>(QDir::tempPath() +
                                                "/floatype-update-XXXXXX");
  folder->setAutoRemove(false);
  const QString package = folder->filePath(QStringLiteral("floatype.pkg"));
  const QString checksum = package + QStringLiteral(".sha256");
  const auto failed = [this, folder](const char* text) {
    QDir(folder->path()).removeRecursively();
    update_ = floatype::UpdateCheck();
    updateButton_->setText(Text("更新を確認"));
    ShowUpdate(text, /*busy=*/false);
  };
  RunCurl(
      {QStringLiteral("-f"), QStringLiteral("-o"), checksum,
       QString::fromStdString(update_.checksum_url)},
      60,
      [this, folder, package, checksum, failed](bool ok, const QByteArray&) {
        if (!ok) {
          failed("ダウンロードできませんでした");
          return;
        }
        RunCurl(
            {QStringLiteral("-f"), QStringLiteral("-o"), package,
             QString::fromStdString(update_.package_url)},
            900, [this, package, checksum, failed](bool ok, const QByteArray&) {
              QFile file(package);
              QFile named(checksum);
              QCryptographicHash hash(QCryptographicHash::Sha256);
              if (!ok || !file.open(QIODevice::ReadOnly) ||
                  !named.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
                failed("ダウンロードできませんでした");
                return;
              }
              if (!floatype::ChecksumMatches(
                      named.readAll().toStdString(),
                      hash.result().toHex().toStdString())) {
                failed(
                    "ダウンロードした物の SHA-256 が合わないので、"
                    "消しました");
                return;
              }
              // The Installer replaces this dialog too.
              QProcess::startDetached(UpdateOpen(), {package});
              QWidget::close();
            });
      });
}

QPushButton* ConfigDialog::NewColorButton(const char* field, const char* label,
                                          QComboBox* theme) {
  QPushButton* button = new QPushButton();
  button->setProperty("field", QString::fromUtf8(field));
  // The native color panel on macOS.
  connect(button, &QPushButton::clicked, this, [this, button, label, theme] {
    const QColor color = QColorDialog::getColor(
        Color(button->property("shown").toUInt()), this, Text(label));
    if (!color.isValid()) {
      return;
    }
    button->setProperty("rgb", static_cast<uint32_t>(color.rgb() & 0xffffff));
    if (theme != nullptr) {
      theme->setCurrentIndex(config::Config::CandidateStyle::CUSTOM);
    }
    RefreshLook();
  });
  return button;
}

QWidget* ConfigDialog::NewWindowForm(WindowControls& controls) {
  QWidget* page = new QWidget();
  QFormLayout* form = new QFormLayout(page);
  form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  controls.form = form;
  // In Theme's order.
  controls.theme =
      AddComboBox(form, "配色",
                  {"自動（システムに合わせる）", "明るい", "暗い", "指定する"});
  for (const auto& [field, label] :
       std::initializer_list<std::pair<const char*, const char*>>{
           {"background_color", "背景"},
           {"text_color", "文字"},
           {"focused_background_color", "選択中の背景"},
           {"focused_border_color", "選択中の枠"},
           {"border_color", "枠線"}}) {
    QPushButton* button = NewColorButton(field, label, controls.theme);
    form->addRow(Text(label), button);
    controls.colors.push_back(button);
  }
  QWidget* size = new QWidget();
  QHBoxLayout* size_layout = new QHBoxLayout(size);
  size_layout->setContentsMargins(0, 0, 0, 0);
  controls.size = new QSlider(Qt::Horizontal);
  controls.size->setRange(50, 300);
  controls.size->setSingleStep(5);
  controls.size->setPageStep(10);
  controls.size->setMinimumWidth(140);
  controls.size_value = new QLabel();
  controls.size_value->setMinimumWidth(
      controls.size_value->fontMetrics().horizontalAdvance("300%"));
  size_layout->addWidget(controls.size);
  size_layout->addWidget(controls.size_value);
  form->addRow(Text("文字の大きさ"), size);
  controls.corner_radius = NewSpinBox(0, 24, 1, " px");
  form->addRow(Text("角の丸み"), controls.corner_radius);
  controls.opacity = NewSpinBox(30, 100, 5, " %");
  form->addRow(Text("不透明度"), controls.opacity);
  // Not in Shadow's order, so each item carries its value.
  controls.shadow = new QComboBox();
  controls.shadow->addItem(Text("なし"), config::Config::CandidateStyle::NONE);
  controls.shadow->addItem(Text("弱い"), config::Config::CandidateStyle::SOFT);
  controls.shadow->addItem(Text("強い"),
                           config::Config::CandidateStyle::STRONG);
  form->addRow(Text("影"), controls.shadow);
  controls.font = new QComboBox();
  controls.font->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  controls.font->setMinimumContentsLength(14);
  controls.font->addItem(Text("システム（標準）"), QString());
  for (const QString& family : QFontDatabase::families()) {
    controls.font->addItem(family, family);
  }
  form->addRow(Text("フォント"), controls.font);
  return page;
}

QWidget* ConfigDialog::SetUpLookTab() {
  QWidget* look = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(look);

  // Which look the preview and the form are for. Checkable push buttons,
  // which leave Apply as it is: switching changes no setting.
  QHBoxLayout* targets = new QHBoxLayout();
  targets->setSpacing(0);
  QButtonGroup* target_group = new QButtonGroup(look);
  int id = 0;
  for (const char* name :
       {"候補の窓", "予測候補の窓", "ルビ", "インジケータ"}) {
    QPushButton* button = new QPushButton(Text(name));
    button->setCheckable(true);
    button->setChecked(id == 0);
    button->setObjectName(QStringLiteral("lookTarget%1").arg(id));
    target_group->addButton(button, id++);
    targets->addWidget(button);
  }
  targets->addSpacing(8);
  targets->addWidget(NewBadge());
  targets->addStretch();
  layout->addLayout(targets);

  QHBoxLayout* body = new QHBoxLayout();
  QVBoxLayout* left = new QVBoxLayout();
  lookPreview_ = new LookPreview([this](QPainter& painter, const QRectF& area) {
    PaintPreview(painter, area);
  });
  lookPreview_->setFixedSize(380, 330);
  left->addWidget(lookPreview_);
  QHBoxLayout* shades = new QHBoxLayout();
  shades->setSpacing(0);
  QButtonGroup* shade_group = new QButtonGroup(look);
  previewDark_ =
      QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
  id = 0;
  for (const char* name : {"明", "暗"}) {
    QPushButton* button = new QPushButton(Text(name));
    button->setCheckable(true);
    button->setChecked((id == 1) == previewDark_);
    button->setObjectName(id == 0 ? QStringLiteral("previewLight")
                                  : QStringLiteral("previewDark"));
    button->setToolTip(Text("プレビューの背景と、自動の配色が従う明るさ"));
    shade_group->addButton(button, id++);
    shades->addWidget(button);
  }
  shades->addStretch();
  left->addLayout(shades);
  kindMarkCheckBox_ =
      new QCheckBox(Text("予測候補の一覧と変換候補を、帯の左の丸の色で見分ける"));
  kindMarkCheckBox_->setToolTip(
      Text("予測候補の一覧は青、変換候補は灰色の丸"));
  left->addWidget(kindMarkCheckBox_);
  left->addStretch();
  body->addLayout(left);

  lookStack_ = new QStackedWidget();
  lookStack_->addWidget(NewWindowForm(candidateControls_));
  lookStack_->addWidget(NewWindowForm(suggestControls_));

  QWidget* ruby_page = new QWidget();
  QFormLayout* ruby = new QFormLayout(ruby_page);
  ruby->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  liveConversionRubyCheckBox_ =
      AddCheckBox(ruby, "ルビを出す（ライブ変換の上に読みを小さく）");
  rubyFontSizeSpinBox_ = NewSpinBox(6, 72, 1, " pt");
  ruby->addRow(Text("文字の大きさ"), rubyFontSizeSpinBox_);
  for (const auto& [field, label] :
       std::initializer_list<std::pair<const char*, const char*>>{
           {"text_color", "文字色"}, {"background_color", "背景色"}}) {
    QPushButton* button = NewColorButton(field, label, nullptr);
    ruby->addRow(Text(label), button);
    rubyColorButtons_.push_back(button);
  }
  rubyCornerRadiusSpinBox_ = NewSpinBox(0, 24, 1, " px");
  ruby->addRow(Text("角の丸み"), rubyCornerRadiusSpinBox_);
  rubyOpacitySpinBox_ = NewSpinBox(30, 100, 5, " %");
  ruby->addRow(Text("不透明度"), rubyOpacitySpinBox_);
  lookStack_->addWidget(ruby_page);

  // The ranges are those the settings file takes, so that loading never
  // changes a value.
  QWidget* indicator_page = new QWidget();
  QFormLayout* indicator = new QFormLayout(indicator_page);
  indicator->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  showIndicatorCheckBox_ = AddCheckBox(indicator, "入力中の文字の近くに出す");
  indicatorShapeComboBox_ =
      AddComboBox(indicator, "形", {"角丸の帯", "角丸の四角", "丸"});
  indicatorLabelComboBox_ = AddComboBox(
      indicator, "文字", {"入力中 / Mozc / Zenz", "あ / M / Z", "色だけ"});
  indicatorPlacementComboBox_ =
      AddComboBox(indicator, "位置", {"入力中の文字の上", "下", "左横"});
  for (const auto& [field, label] :
       std::initializer_list<std::pair<const char*, const char*>>{
           {"reading_color", "入力中の色"},
           {"mozc_color", "Mozc の色"},
           {"zenz_pending_color", "zenz を待つ間の色"},
           {"zenz_color", "Zenz の色"},
           {"text_color", "文字色"}}) {
    QPushButton* button = NewColorButton(field, label, nullptr);
    indicator->addRow(Text(label), button);
    indicatorColorButtons_.push_back(button);
  }
  indicatorHeightSpinBox_ = NewSpinBox(8, 200, 1, " px");
  indicator->addRow(Text("高さ"), indicatorHeightSpinBox_);
  indicatorFontSizeSpinBox_ = NewSpinBox(6, 72, 1, " pt");
  indicator->addRow(Text("文字の大きさ"), indicatorFontSizeSpinBox_);
  indicatorCornerRadiusSpinBox_ = NewSpinBox(0, 100, 1, " px");
  indicator->addRow(Text("角の丸み（角丸の四角）"),
                    indicatorCornerRadiusSpinBox_);
  indicatorOpacitySpinBox_ = NewSpinBox(0, 100, 5, " %");
  indicator->addRow(Text("不透明度"), indicatorOpacitySpinBox_);
  lookStack_->addWidget(indicator_page);

  body->addWidget(lookStack_, 1, Qt::AlignTop);
  layout->addLayout(body);
  layout->addStretch();

  connect(target_group, &QButtonGroup::idClicked, lookStack_,
          &QStackedWidget::setCurrentIndex);
  connect(lookStack_, &QStackedWidget::currentChanged, lookPreview_,
          [this] { lookPreview_->update(); });
  connect(shade_group, &QButtonGroup::idClicked, this, [this](int id) {
    previewDark_ = id == 1;
    RefreshLook();
  });
  return look;
}

// Each page in a frameless scroll area, so that the window fits a small
// screen; the page's width is kept.
void ConfigDialog::WrapTabsInScrollAreas() {
  const int current = configDialogTabWidget->currentIndex();
  for (int i = 0; i < configDialogTabWidget->count(); ++i) {
    QWidget* page = configDialogTabWidget->widget(i);
    const QString title = configDialogTabWidget->tabText(i);
    configDialogTabWidget->removeTab(i);
    QScrollArea* scroll = new QScrollArea();
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    scroll->setMinimumWidth(page->minimumSizeHint().width() +
                            scroll->verticalScrollBar()->sizeHint().width());
    page->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    configDialogTabWidget->insertTab(i, scroll, title);
  }
  configDialogTabWidget->setCurrentIndex(current);
}

void ConfigDialog::LoadLookControls(const config::Config& config) {
  kindMarkCheckBox_->setChecked(config.candidate_kind_mark());
  for (const auto& [controls, style] :
       {std::pair{&candidateControls_, &config.candidate_style()},
        std::pair{&suggestControls_, &config.suggest_style()}}) {
    controls->theme->setCurrentIndex(style->theme());
    for (QPushButton* button : controls->colors) LoadColor(button, *style);
    controls->size->setValue(style->size_percent());
    controls->corner_radius->setValue(style->corner_radius());
    controls->opacity->setValue(style->opacity_percent());
    controls->shadow->setCurrentIndex(
        controls->shadow->findData(style->shadow()));
    // A family not installed stays as it is.
    const QString family = QString::fromStdString(style->font_family());
    if (controls->font->findData(family) < 0) {
      controls->font->addItem(family, family);
    }
    controls->font->setCurrentIndex(controls->font->findData(family));
  }
  const config::Config::RubyStyle& ruby = config.ruby_style();
  rubyFontSizeSpinBox_->setValue(ruby.font_size());
  rubyCornerRadiusSpinBox_->setValue(ruby.corner_radius());
  rubyOpacitySpinBox_->setValue(ruby.opacity_percent());
  for (QPushButton* button : rubyColorButtons_) LoadColor(button, ruby);
  const config::Config::IndicatorStyle& indicator = config.indicator_style();
  indicatorShapeComboBox_->setCurrentIndex(indicator.shape());
  indicatorLabelComboBox_->setCurrentIndex(indicator.label());
  indicatorPlacementComboBox_->setCurrentIndex(indicator.placement());
  indicatorHeightSpinBox_->setValue(indicator.height());
  indicatorFontSizeSpinBox_->setValue(indicator.font_size());
  indicatorCornerRadiusSpinBox_->setValue(indicator.corner_radius());
  indicatorOpacitySpinBox_->setValue(indicator.opacity_percent());
  for (QPushButton* button : indicatorColorButtons_) {
    LoadColor(button, indicator);
  }
  RefreshLook();
}

// Into the styles `config` has, keeping the fields without a control.
void ConfigDialog::SaveLookControls(config::Config* config) const {
  config->set_candidate_kind_mark(kindMarkCheckBox_->isChecked());
  for (const auto& [controls, style] :
       {std::pair{&candidateControls_, config->mutable_candidate_style()},
        std::pair{&suggestControls_, config->mutable_suggest_style()}}) {
    style->set_theme(static_cast<config::Config::CandidateStyle::Theme>(
        controls->theme->currentIndex()));
    for (const QPushButton* button : controls->colors) {
      SaveColor(button, style);
    }
    style->set_size_percent(controls->size->value());
    style->set_corner_radius(controls->corner_radius->value());
    style->set_opacity_percent(controls->opacity->value());
    style->set_shadow(static_cast<config::Config::CandidateStyle::Shadow>(
        controls->shadow->currentData().toInt()));
    style->set_font_family(
        controls->font->currentData().toString().toStdString());
  }
  config::Config::RubyStyle* ruby = config->mutable_ruby_style();
  ruby->set_font_size(rubyFontSizeSpinBox_->value());
  ruby->set_corner_radius(rubyCornerRadiusSpinBox_->value());
  ruby->set_opacity_percent(rubyOpacitySpinBox_->value());
  for (const QPushButton* button : rubyColorButtons_) SaveColor(button, ruby);
  config::Config::IndicatorStyle* indicator = config->mutable_indicator_style();
  indicator->set_shape(static_cast<config::Config::IndicatorStyle::Shape>(
      indicatorShapeComboBox_->currentIndex()));
  indicator->set_label(static_cast<config::Config::IndicatorStyle::Label>(
      indicatorLabelComboBox_->currentIndex()));
  indicator->set_placement(
      static_cast<config::Config::IndicatorStyle::Placement>(
          indicatorPlacementComboBox_->currentIndex()));
  indicator->set_height(indicatorHeightSpinBox_->value());
  indicator->set_font_size(indicatorFontSizeSpinBox_->value());
  indicator->set_corner_radius(indicatorCornerRadiusSpinBox_->value());
  indicator->set_opacity_percent(indicatorOpacitySpinBox_->value());
  for (const QPushButton* button : indicatorColorButtons_) {
    SaveColor(button, indicator);
  }
}

void ConfigDialog::RefreshLook() {
  config::Config config = base_config_;
  SaveLookControls(&config);
  for (const auto& [controls, style] :
       {std::pair{&candidateControls_, &config.candidate_style()},
        std::pair{&suggestControls_, &config.suggest_style()}}) {
    // An unset color shows the one the window gets.
    const floatype::WindowLook look =
        floatype::ResolveWindowLook(*style, previewDark_);
    config::Config::CandidateStyle shown = *style;
    shown.set_background_color(look.background);
    shown.set_text_color(look.text);
    shown.set_focused_background_color(look.focused_background);
    shown.set_focused_border_color(look.focused_border);
    shown.set_border_color(look.border);
    const bool custom =
        style->theme() == config::Config::CandidateStyle::CUSTOM;
    for (QPushButton* button : controls->colors) {
      controls->form->setRowVisible(button, custom);
      ShowColor(button, shown);
    }
    controls->size_value->setText(
        QStringLiteral("%1%").arg(controls->size->value()));
  }
  for (QPushButton* button : rubyColorButtons_) {
    ShowColor(button, config.ruby_style());
  }
  for (QPushButton* button : indicatorColorButtons_) {
    ShowColor(button, config.indicator_style());
  }
  lookPreview_->update();
}

void ConfigDialog::PaintPreview(QPainter& painter, const QRectF& area) const {
  config::Config config = base_config_;
  SaveLookControls(&config);
  PaintBackdrop(painter, area, previewDark_);
  switch (lookStack_->currentIndex()) {
    case 0:
      PaintCandidateWindow(
          painter, area,
          floatype::ResolveWindowLook(config.candidate_style(), previewDark_),
          false, config.candidate_kind_mark());
      break;
    case 1:
      PaintCandidateWindow(
          painter, area,
          floatype::ResolveWindowLook(config.suggest_style(), previewDark_),
          true);
      break;
    case 2:
      PaintRuby(painter, area, config.ruby_style(), previewDark_);
      break;
    default:
      PaintIndicators(painter, area, config.indicator_style());
      break;
  }
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
  SET_CHECKBOX(zenzTeachesMozcCheckBox_, zenz_teaches_mozc);
  SET_CHECKBOX(zenzContextFromCommitsCheckBox_, zenz_context_from_commits);
  zenzRememberedMinCharsSpinBox_->setValue(config.zenz_remembered_min_chars());
  SET_CHECKBOX(zenzStopWhenIdleCheckBox_, zenz_stop_when_idle);
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
  SET_CHECKBOX(mixedEnglishSpaceCommitsCheckBox_, mixed_english_space_commits);
  SET_CHECKBOX(mixedEnglishEnterCommitsCheckBox_, mixed_english_enter_commits);
  SET_CHECKBOX(mixedEnglishLeadingSymbolsCheckBox_,
               mixed_english_leading_symbols);
  for (QWidget* part : std::initializer_list<QWidget*>{
           mixedEnglishSpaceCommitsCheckBox_, mixedEnglishEnterCommitsCheckBox_,
           mixedEnglishLeadingSymbolsCheckBox_}) {
    part->setEnabled(config.mixed_english());
  }
  SET_CHECKBOX(slashAfterAsciiCheckBox_, slash_after_ascii);
  SET_CHECKBOX(suggestAddedWordsCheckBox_, suggest_added_words);
  SET_CHECKBOX(displayAmbiguousCheckBox_, display_ambiguous_romaji);
  SET_CHECKBOX(showIndicatorCheckBox_, show_indicator);
  LoadLookControls(config);

  // tab5
  SetSendStatsCheckBox();
  SET_CHECKBOX(incognitoModeCheckBox, incognito_mode);
  SET_CHECKBOX(presentationModeCheckBox, presentation_mode);

  // tab6
  SET_COMBOBOX(verboseLevelComboBox, int, verbose_level);
  SET_CHECKBOX(checkDefaultCheckBox, check_default);
  SET_COMBOBOX(yenSignComboBox, YenSignCharacter, yen_sign_character);

  characterFormEditor->Load(config);
  RefreshNumberForm();

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
  GET_CHECKBOX(zenzTeachesMozcCheckBox_, zenz_teaches_mozc);
  GET_CHECKBOX(zenzContextFromCommitsCheckBox_, zenz_context_from_commits);
  config->set_zenz_remembered_min_chars(
      zenzRememberedMinCharsSpinBox_->value());
  GET_CHECKBOX(zenzStopWhenIdleCheckBox_, zenz_stop_when_idle);
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
  GET_CHECKBOX(mixedEnglishSpaceCommitsCheckBox_, mixed_english_space_commits);
  GET_CHECKBOX(mixedEnglishEnterCommitsCheckBox_, mixed_english_enter_commits);
  GET_CHECKBOX(mixedEnglishLeadingSymbolsCheckBox_,
               mixed_english_leading_symbols);
  GET_CHECKBOX(slashAfterAsciiCheckBox_, slash_after_ascii);
  GET_CHECKBOX(suggestAddedWordsCheckBox_, suggest_added_words);
  GET_CHECKBOX(displayAmbiguousCheckBox_, display_ambiguous_romaji);
  GET_CHECKBOX(showIndicatorCheckBox_, show_indicator);
  SaveLookControls(config);

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

void ConfigDialog::clicked(QAbstractButton* button) {
  switch (configDialogButtonBox->buttonRole(button)) {
    case QDialogButtonBox::AcceptRole:
      if (Update()) {
        QWidget::close();
      }
      break;
    case QDialogButtonBox::ApplyRole:
      // floatype: off again until the next change.
      if (Update()) {
        configDialogButtonBox->button(QDialogButtonBox::Apply)
            ->setEnabled(false);
      }
      break;
    case QDialogButtonBox::RejectRole:
      QWidget::close();
      break;
    default:
      break;
  }
}

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
  }
}

void ConfigDialog::EditRomanTable() {
  std::string output;
  if (gui::RomanTableEditorDialog::Show(this, custom_roman_table_, &output)) {
    custom_roman_table_ = output;
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
  }
}

void ConfigDialog::LaunchAdministrationDialog() {
  if constexpr (port::IsWindows()) {
    client_->LaunchTool("administration_dialog", "");
  }
}

// Shows the table's digit row in the 数字の入力 choice; a pair the choice does
// not offer shows as set in the table, and is left as it is.
void ConfigDialog::RefreshNumberForm() {
  const QSignalBlocker blocker(numberFormComboBox_);
  const auto forms = characterFormEditor->Forms("0");
  int index = 3;
  if (forms.has_value()) {
    const auto [preedit, conversion] = *forms;
    if (preedit == config::Config::HALF_WIDTH &&
        conversion == config::Config::HALF_WIDTH) {
      index = 0;
    } else if (preedit == config::Config::FULL_WIDTH &&
               conversion == config::Config::FULL_WIDTH) {
      index = 1;
    } else if (preedit == config::Config::FULL_WIDTH &&
               conversion == config::Config::LAST_FORM) {
      index = 2;
    }
  }
  numberFormComboBox_->setCurrentIndex(index);
}

void ConfigDialog::EnableApplyButton() {
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

// Catch MouseButtonRelease event to toggle the CheckBoxes
bool ConfigDialog::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::Wheel) {
    if (QWidget* widget = qobject_cast<QWidget*>(obj);
        widget != nullptr && !widget->hasFocus()) {
      for (QWidget* parent = widget->parentWidget(); parent != nullptr;
           parent = parent->parentWidget()) {
        if (QScrollArea* scroll = qobject_cast<QScrollArea*>(parent)) {
          QCoreApplication::sendEvent(scroll->verticalScrollBar(), event);
          break;
        }
      }
      return true;
    }
  }
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
