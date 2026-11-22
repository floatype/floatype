// floatype: the settings screen's look (issue #9): the tabs as a list with
// icons on the left, the shown tab's name large over its settings, and each
// group of settings in a rounded box. Laid over Mozc's dialog from outside,
// so that Mozc's own files change by one call.

#ifndef MOZC_GUI_CONFIG_DIALOG_MODERN_LOOK_H_
#define MOZC_GUI_CONFIG_DIALOG_MODERN_LOOK_H_

#include <QTabWidget>

namespace mozc::gui {

// Call once the tabs are all there. `tabs` stays the one that holds the
// pages; its tab bar is hidden and the list switches it.
void ApplyModernLook(QTabWidget* tabs);

}  // namespace mozc::gui

#endif  // MOZC_GUI_CONFIG_DIALOG_MODERN_LOOK_H_
