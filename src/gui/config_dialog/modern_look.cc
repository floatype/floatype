#include "gui/config_dialog/modern_look.h"

#include <QBoxLayout>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <iterator>

namespace mozc::gui {
namespace {

// The groups are Mozc's frames with these classes (config_dialog.ui and
// InsertGroup), so Mozc's later groups take the look too.
constexpr char kQss[] = R"(
QFrame[class="setting-group-layout"] {
  background-color: rgba(128, 128, 128, 0.09);
  border: 1px solid rgba(128, 128, 128, 0.18);
  border-radius: 10px;
  padding: 0.4em 1em;
  margin: 0em 0.2em 0.6em 0.2em;
}
QFrame[class="setting-group-header"] QLabel {
  font-weight: 600;
}
QListWidget#modernSidebar {
  background: transparent;
  border: none;
  outline: none;
}
QListWidget#modernSidebar::item {
  padding: 4px 6px;
  margin: 1px 8px;
  border-radius: 6px;
}
QListWidget#modernSidebar::item:selected {
  background-color: palette(highlight);
  color: palette(highlighted-text);
}
)";

// macOS's settings colors, a tab each in turn.
constexpr QRgb kIconColors[] = {0x0a84ff, 0xff9f0a, 0x30d158, 0xbf5af2,
                                0xff375f, 0x64d2ff, 0x8e8e93};

// A rounded square in `color` with the first character of `title` in white,
// as macOS's settings list draws its icons.
QIcon TabIcon(const QString& title, QColor color, int size) {
  const qreal ratio = 2;  // Sharp on Retina screens.
  QPixmap pixmap(size * ratio, size * ratio);
  pixmap.setDevicePixelRatio(ratio);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawRoundedRect(QRectF(0, 0, size, size), size * 0.24, size * 0.24);
  QFont font = painter.font();
  font.setPixelSize(size * 0.6);
  font.setBold(true);
  painter.setFont(font);
  painter.setPen(Qt::white);
  painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, title.left(1));
  return QIcon(pixmap);
}

// Mozc's group headers draw a line after the title; the boxes replace it.
void HideHeaderLines(QWidget* page) {
  for (QFrame* frame : page->findChildren<QFrame*>()) {
    if (frame->frameShape() == QFrame::HLine &&
        frame->parentWidget()->property("class").toString() ==
            QStringLiteral("setting-group-header")) {
      frame->hide();
      // The line filled the row; without it, keep the title and badge left.
      if (QBoxLayout* row =
              qobject_cast<QBoxLayout*>(frame->parentWidget()->layout())) {
        row->addStretch();
      }
    }
  }
}

}  // namespace

void ApplyModernLook(QTabWidget* tabs) {
  QWidget* dialog = tabs->window();
  dialog->setStyleSheet(dialog->styleSheet() + QString::fromUtf8(kQss));

  QListWidget* sidebar = new QListWidget();
  sidebar->setObjectName(QStringLiteral("modernSidebar"));
  sidebar->setIconSize(QSize(20, 20));
  sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  int width = 0;
  for (int i = 0; i < tabs->count(); ++i) {
    const QString title = tabs->tabText(i);
    QListWidgetItem* item = new QListWidgetItem(
        TabIcon(title, QColor(kIconColors[i % std::size(kIconColors)]), 20),
        title, sidebar);
    item->setSizeHint(QSize(0, 30));
    width = std::max(width, sidebar->fontMetrics().horizontalAdvance(title));
    HideHeaderLines(tabs->widget(i));
  }
  sidebar->setFixedWidth(width + 20 + 60);

  QLabel* title = new QLabel();
  title->setObjectName(QStringLiteral("modernTitle"));
  QFont large = title->font();
  large.setPointSizeF(large.pointSizeF() * 1.7);
  large.setBold(true);
  title->setFont(large);
  title->setContentsMargins(6, 4, 0, 6);

  tabs->tabBar()->hide();
  tabs->setDocumentMode(true);

  // The tabs' place in the dialog goes to the list and a column holding the
  // title over the tabs.
  QWidget* body = new QWidget();
  QHBoxLayout* row = new QHBoxLayout(body);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  QVBoxLayout* column = new QVBoxLayout();
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);
  QWidget* parent = tabs->parentWidget();
  delete parent->layout()->replaceWidget(tabs, body);
  column->addWidget(title);
  column->addWidget(tabs, 1);
  row->addWidget(sidebar);
  row->addLayout(column, 1);

  // Either side may switch the page: the list, or the dialog's own code.
  const auto show = [tabs, sidebar, title](int index) {
    if (tabs->currentIndex() != index) tabs->setCurrentIndex(index);
    if (sidebar->currentRow() != index) sidebar->setCurrentRow(index);
    title->setText(tabs->tabText(index));
  };
  QObject::connect(sidebar, &QListWidget::currentRowChanged, tabs, show);
  QObject::connect(tabs, &QTabWidget::currentChanged, sidebar, show);
  show(tabs->currentIndex());
}

}  // namespace mozc::gui
