#include "gui/config_dialog/english_words_dialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "floatype/config/settings_file.h"
#include "floatype/english/user_words.h"

namespace mozc::gui {
namespace {

using floatype::EnglishWordEdit;

constexpr char kKeep[] = "英字にする";
constexpr char kTakeOut[] = "英字にしない";

// The first lines of a file the list makes.
constexpr char kHeader[] =
    "# floatype: かなの入力中でも英字のまま残す英単語。1 行に 1 語。\n"
    "# -word と書いた語は、英字にしない。\n";

constexpr char kNote[] =
    "英字にする：かなの入力中に打っても英字のまま残す語。"
    "英字にしない：英字にする語から外す語。\n"
    "初めから：そのまま打つと かな としても読める語のうち、"
    "日本語では打たない語（page、time）。英字にしないに回せます。"
    "初めから → 自分で：その語をファイルで変えたもの。"
    "一般の英単語（約 8 万 8 千語）は、ここには出ません。\n"
    "変えた内容は english_words.txt に書かれ、1 秒ほどあとのキーから効きます。";

QString Text(const char* utf8) { return QString::fromUtf8(utf8); }

struct File {
  std::string path;
  std::string text;
  floatype::FileStamp stamp;
};

File Read(std::string path) {
  File file{.path = std::move(path)};
  file.stamp = floatype::StampOf(file.path);
  if (absl::StatusOr<std::string> text = FileUtil::GetContents(file.path);
      text.ok()) {
    file.text = *std::move(text);
  }
  return file;
}

// A row; its source cell holds whether the file has a line of the word.
void AddRow(QTableWidget* table, absl::string_view word, const char* edit,
            bool in_file, bool preset) {
  const int row = table->rowCount();
  table->insertRow(row);
  table->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(word)));
  table->setItem(row, 1, new QTableWidgetItem(Text(edit)));
  QTableWidgetItem* source =
      new QTableWidgetItem(Text(!in_file ? "初めから"
                                : preset ? "初めから → 自分で"
                                         : "自分で"));
  source->setData(Qt::UserRole, in_file);
  table->setItem(row, 2, source);
}

// The file's words in its order, then the preset words it leaves alone.
void Fill(QTableWidget* table, const File& file) {
  table->setRowCount(0);
  const std::vector<std::string> presets =
      absl::StrSplit(floatype::PresetEnglishWords(), '\n', absl::SkipEmpty());
  const std::set<std::string> preset(presets.begin(), presets.end());
  std::set<std::string> shown;
  for (const absl::string_view line : absl::StrSplit(file.text, '\n')) {
    const floatype::UserEnglishWords one =
        floatype::ParseUserEnglishWords(line);
    const bool keeps = !one.words.empty();
    const absl::string_view word =
        absl::StripTrailingAsciiWhitespace(keeps ? one.words : one.removed);
    const std::string lower = absl::AsciiStrToLower(word);
    if (!word.empty() && shown.insert(lower).second) {
      AddRow(table, word, keeps ? kKeep : kTakeOut, /*in_file=*/true,
             preset.contains(lower));
    }
  }
  for (const std::string& word : presets) {
    if (shown.insert(word).second) {
      AddRow(table, word, kKeep, /*in_file=*/false, /*preset=*/true);
    }
  }
}

int ApplyFilter(QTableWidget* table, const QString& filter) {
  int shown = 0;
  for (int row = 0; row < table->rowCount(); ++row) {
    const bool matches =
        table->item(row, 0)->text().contains(filter, Qt::CaseInsensitive);
    table->setRowHidden(row, !matches);
    shown += matches;
  }
  return shown;
}

}  // namespace

std::unique_ptr<QDialog> NewEnglishWordsDialog(QWidget* parent) {
  auto dialog = std::make_unique<QDialog>(parent);
  dialog->setWindowTitle(Text("英字のまま残す英単語"));
  dialog->resize(560, 520);

  QLineEdit* word = new QLineEdit();
  word->setObjectName(QStringLiteral("word"));
  word->setPlaceholderText(Text("足す語（figma）"));
  QComboBox* edit = new QComboBox();
  edit->setObjectName(QStringLiteral("edit"));
  edit->addItem(Text(kKeep));
  edit->addItem(Text(kTakeOut));
  QPushButton* add = new QPushButton(Text("足す"));
  add->setObjectName(QStringLiteral("add"));
  add->setEnabled(false);
  QLabel* message = new QLabel();
  message->setObjectName(QStringLiteral("message"));
  message->setWordWrap(true);
  message->setStyleSheet(QStringLiteral("color: #c0392b;"));
  message->hide();

  QLineEdit* filter = new QLineEdit();
  filter->setObjectName(QStringLiteral("filter"));
  filter->setPlaceholderText(Text("語で絞り込む"));
  filter->setClearButtonEnabled(true);

  QTableWidget* table = new QTableWidget(0, 3);
  table->setObjectName(QStringLiteral("words"));
  table->setHorizontalHeaderLabels(
      {Text("語"), Text("扱い"), Text("どこから")});
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  table->verticalHeader()->hide();
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);

  QPushButton* take_out = new QPushButton(Text("選んだ語を英字にしない"));
  take_out->setObjectName(QStringLiteral("takeOut"));
  take_out->setEnabled(false);
  QPushButton* forget = new QPushButton(Text("選んだ行をファイルから消す"));
  forget->setObjectName(QStringLiteral("forget"));
  forget->setEnabled(false);
  QPushButton* reveal = new QPushButton(Text("Finder で表示"));
  QLabel* count = new QLabel();
  count->setObjectName(QStringLiteral("count"));
  QPushButton* close = new QPushButton(Text("閉じる"));
  QLabel* note = new QLabel(Text(kNote));
  note->setWordWrap(true);
  QFont small = note->font();
  small.setPointSizeF(small.pointSizeF() * 0.9);
  note->setFont(small);

  QHBoxLayout* adding = new QHBoxLayout();
  adding->addWidget(word, 1);
  adding->addWidget(edit);
  adding->addWidget(add);
  QHBoxLayout* buttons = new QHBoxLayout();
  buttons->addWidget(take_out);
  buttons->addWidget(forget);
  buttons->addWidget(reveal);
  buttons->addStretch();
  buttons->addWidget(count);
  buttons->addWidget(close);
  QVBoxLayout* layout = new QVBoxLayout(dialog.get());
  layout->addLayout(adding);
  layout->addWidget(message);
  layout->addWidget(filter);
  layout->addWidget(table, 1);
  layout->addLayout(buttons);
  layout->addWidget(note);

  // What the list shows, read when it opens and after each change.
  auto file = std::make_shared<File>(
      Read(floatype::UserFilePath(floatype::kEnglishWordsFile)));
  const auto refresh = [table, filter, count, file] {
    Fill(table, *file);
    count->setText(Text("%1 語").arg(ApplyFilter(table, filter->text())));
  };
  const auto show_message = [message](const QString& text) {
    message->setText(text);
    message->setVisible(!text.isEmpty());
  };
  // Writes the edits, unless the file changed since it was read: then, as
  // the dictionary tool does, nothing is written, and the list reads it again.
  const auto apply = [file, refresh, show_message](
                         const std::vector<std::string>& words,
                         EnglishWordEdit how) {
    if (!(floatype::StampOf(file->path) == file->stamp)) {
      *file = Read(file->path);
      refresh();
      show_message(
          Text("開いたあとに english_words.txt が書き換えられたので、"
               "変えずに読み直しました。もう一度操作してください。"));
      return false;
    }
    std::string text = file->stamp.size < 0 ? std::string(kHeader) : file->text;
    for (const std::string& one : words) {
      text = floatype::EditUserEnglishWords(text, one, how);
    }
    if (const absl::Status status = floatype::WriteAtomically(file->path, text);
        !status.ok()) {
      show_message(Text("保存できませんでした（%1）")
                       .arg(QString::fromUtf8(status.message())));
      return false;
    }
    *file = Read(file->path);
    refresh();
    show_message(QString());
    return true;
  };
  // The selected words kept in letters, or those with a line in the file.
  const auto selected = [table](bool in_file) {
    std::vector<std::string> words;
    std::set<int> rows;
    for (const QTableWidgetItem* item : table->selectedItems()) {
      rows.insert(item->row());
    }
    for (const int row : rows) {
      if (in_file ? table->item(row, 2)->data(Qt::UserRole).toBool()
                  : table->item(row, 1)->text() == Text(kKeep)) {
        words.push_back(table->item(row, 0)->text().toStdString());
      }
    }
    return words;
  };

  QObject::connect(word, &QLineEdit::textChanged, dialog.get(),
                   [add, show_message](const QString& text) {
                     add->setEnabled(!text.trimmed().isEmpty());
                     show_message(QString());
                   });
  const auto add_word = [word, edit, apply, show_message] {
    const std::string typed = word->text().trimmed().toStdString();
    // The file's own rule, so that the list never writes a line left out.
    if (floatype::ParseUserEnglishWords(typed).words !=
        absl::StrCat(typed, "\n")) {
      show_message(Text("「%1」は使えません。英字だけの 3〜32 "
                        "文字の語が使えます。")
                       .arg(word->text().trimmed()));
      return;
    }
    if (apply({typed}, edit->currentIndex() == 0 ? EnglishWordEdit::kKeep
                                                 : EnglishWordEdit::kTakeOut)) {
      word->clear();
    }
  };
  QObject::connect(add, &QPushButton::clicked, dialog.get(), add_word);
  QObject::connect(word, &QLineEdit::returnPressed, dialog.get(), add_word);
  QObject::connect(
      filter, &QLineEdit::textChanged, dialog.get(),
      [table, count](const QString& text) {
        count->setText(Text("%1 語").arg(ApplyFilter(table, text)));
      });
  QObject::connect(table, &QTableWidget::itemSelectionChanged, dialog.get(),
                   [take_out, forget, selected] {
                     take_out->setEnabled(!selected(false).empty());
                     forget->setEnabled(!selected(true).empty());
                   });
  QObject::connect(
      take_out, &QPushButton::clicked, dialog.get(),
      [selected, apply] { apply(selected(false), EnglishWordEdit::kTakeOut); });
  QObject::connect(
      forget, &QPushButton::clicked, dialog.get(),
      [selected, apply] { apply(selected(true), EnglishWordEdit::kForget); });
  QObject::connect(reveal, &QPushButton::clicked, dialog.get(), [file] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QString::fromStdString(FileUtil::Dirname(file->path))));
  });
  QObject::connect(close, &QPushButton::clicked, dialog.get(),
                   &QDialog::accept);
  refresh();
  return dialog;
}

}  // namespace mozc::gui
