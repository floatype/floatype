#include "gui/config_dialog/zenz_memory_dialog.h"

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <cstddef>
#include <iterator>
#include <memory>
#include <set>

#include "base/system_util.h"
#include "floatype/zenz/zenz_memory.h"

namespace mozc::gui {
namespace {

using floatype::ZenzMemory;

struct Kind {
  const char* label;
  ZenzMemory& (*memory)();
};

// In the order they are listed.
constexpr Kind kKinds[] = {
    {"退けた語", &ZenzMemory::TurnedDown},
    {"1 回退けた語", &ZenzMemory::RefusedOnce},
    {"すぐ出す補正", &ZenzMemory::Accepted},
    {"止めた補正", &ZenzMemory::Stopped},
};

constexpr char kNote[] =
    "退けた語：別の候補に選び直した語。zenz はこの語を出しません。"
    "自分で選んで確定すると消えます。\n"
    "1 回退けた語：補正を Space で戻して、そのまま確定した語。"
    "もう 1 回で「退けた語」になります。\n"
    "すぐ出す補正：そのまま確定した補正。次に同じ読みで、zenz "
    "を待たずに出します。\n"
    "止めた補正：control+delete で止めた補正。同じ読みでは出しません。\n"
    "消した内容は、1 秒ほどあとの変換から効きます。Mozc "
    "が覚えた候補の順は、ここには出ません。";

QString Text(const char* utf8) { return QString::fromUtf8(utf8); }

// Lists every memory, the newest of each first.
void Fill(QTableWidget* table) {
  table->setRowCount(0);
  for (size_t kind = 0; kind < std::size(kKinds); ++kind) {
    const auto entries = kKinds[kind].memory().Entries();
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
      const int row = table->rowCount();
      table->insertRow(row);
      QTableWidgetItem* label = new QTableWidgetItem(Text(kKinds[kind].label));
      label->setData(Qt::UserRole, static_cast<int>(kind));
      table->setItem(row, 0, label);
      table->setItem(
          row, 1, new QTableWidgetItem(QString::fromStdString(entry->first)));
      table->setItem(
          row, 2, new QTableWidgetItem(QString::fromStdString(entry->second)));
    }
  }
}

// Shows the rows whose reading or writing has `filter` in it. Returns how
// many.
int ApplyFilter(QTableWidget* table, const QString& filter) {
  int shown = 0;
  for (int row = 0; row < table->rowCount(); ++row) {
    const bool matches = filter.isEmpty() ||
                         table->item(row, 1)->text().contains(filter) ||
                         table->item(row, 2)->text().contains(filter);
    table->setRowHidden(row, !matches);
    shown += matches;
  }
  return shown;
}

}  // namespace

std::unique_ptr<QDialog> NewZenzMemoryDialog(QWidget* parent) {
  auto dialog = std::make_unique<QDialog>(parent);
  dialog->setWindowTitle(Text("zenz の学習データ"));
  dialog->resize(600, 460);

  QLineEdit* filter = new QLineEdit();
  filter->setObjectName(QStringLiteral("filter"));
  filter->setPlaceholderText(Text("読みか表記で絞り込む"));
  filter->setClearButtonEnabled(true);

  QTableWidget* table = new QTableWidget(0, 3);
  table->setObjectName(QStringLiteral("entries"));
  table->setHorizontalHeaderLabels({Text("種類"), Text("読み"), Text("表記")});
  table->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table->verticalHeader()->hide();
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);

  QPushButton* remove = new QPushButton(Text("選んだ行を消す"));
  remove->setObjectName(QStringLiteral("remove"));
  remove->setEnabled(false);
  QPushButton* clear = new QPushButton(Text("すべて消す"));
  clear->setObjectName(QStringLiteral("clear"));
  QPushButton* reveal = new QPushButton(Text("Finder で表示"));
  QLabel* count = new QLabel();
  count->setObjectName(QStringLiteral("count"));
  QPushButton* close = new QPushButton(Text("閉じる"));
  close->setDefault(true);
  QLabel* note = new QLabel(Text(kNote));
  note->setWordWrap(true);
  QFont small = note->font();
  small.setPointSizeF(small.pointSizeF() * 0.9);
  note->setFont(small);

  QHBoxLayout* buttons = new QHBoxLayout();
  buttons->addWidget(remove);
  buttons->addWidget(clear);
  buttons->addWidget(reveal);
  buttons->addStretch();
  buttons->addWidget(count);
  buttons->addWidget(close);
  QVBoxLayout* layout = new QVBoxLayout(dialog.get());
  layout->addWidget(filter);
  layout->addWidget(table, 1);
  layout->addLayout(buttons);
  layout->addWidget(note);

  // The files may change under the list (the converter learns as the user
  // types), so every change here reads them again.
  const auto refresh = [table, filter, count] {
    Fill(table);
    count->setText(Text("%1 件").arg(ApplyFilter(table, filter->text())));
  };
  QObject::connect(
      filter, &QLineEdit::textChanged, dialog.get(),
      [table, count](const QString& text) {
        count->setText(Text("%1 件").arg(ApplyFilter(table, text)));
      });
  QObject::connect(table, &QTableWidget::itemSelectionChanged, dialog.get(),
                   [table, remove] {
                     remove->setEnabled(!table->selectedItems().isEmpty());
                   });
  QObject::connect(
      remove, &QPushButton::clicked, dialog.get(), [table, refresh] {
        std::set<int> rows;
        for (const QTableWidgetItem* item : table->selectedItems()) {
          rows.insert(item->row());
        }
        for (const int row : rows) {
          const int kind = table->item(row, 0)->data(Qt::UserRole).toInt();
          kKinds[kind].memory().Remove(
              table->item(row, 1)->text().toStdString(),
              table->item(row, 2)->text().toStdString());
        }
        refresh();
      });
  QDialog* shown = dialog.get();
  QObject::connect(clear, &QPushButton::clicked, shown, [shown, refresh] {
    if (QMessageBox::question(
            shown, shown->windowTitle(),
            Text("zenz が覚えた内容をすべて消します。元には戻せません。")) !=
        QMessageBox::Yes) {
      return;
    }
    for (const Kind& kind : kKinds) {
      kind.memory().Clear();
    }
    refresh();
  });
  QObject::connect(reveal, &QPushButton::clicked, shown, [] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QString::fromStdString(SystemUtil::GetUserProfileDirectory())));
  });
  QObject::connect(close, &QPushButton::clicked, shown, &QDialog::accept);
  refresh();
  return dialog;
}

}  // namespace mozc::gui
