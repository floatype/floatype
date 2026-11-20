// floatype: the settings screen's list of what zenz learned (ZenzMemory): the
// words and corrections remembered, to look through and to remove. The files
// are what is remembered; the list reads them and removes lines from them.

#ifndef MOZC_GUI_CONFIG_DIALOG_ZENZ_MEMORY_DIALOG_H_
#define MOZC_GUI_CONFIG_DIALOG_ZENZ_MEMORY_DIALOG_H_

#include <QDialog>
#include <QWidget>
#include <memory>

namespace mozc::gui {

// The list, not yet shown.
std::unique_ptr<QDialog> NewZenzMemoryDialog(QWidget* parent);

}  // namespace mozc::gui

#endif  // MOZC_GUI_CONFIG_DIALOG_ZENZ_MEMORY_DIALOG_H_
