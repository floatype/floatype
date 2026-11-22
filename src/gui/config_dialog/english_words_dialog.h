// floatype: the settings screen's list of the English words that mixed English
// input keeps in letters: those the user added or took out
// (english_words.txt, whose lines it edits) and the preset ones, read only.

#ifndef MOZC_GUI_CONFIG_DIALOG_ENGLISH_WORDS_DIALOG_H_
#define MOZC_GUI_CONFIG_DIALOG_ENGLISH_WORDS_DIALOG_H_

#include <QDialog>
#include <QWidget>
#include <memory>

namespace mozc::gui {

// The list, not yet shown. Needs the user files' directory
// (floatype::SetUserFilesDir).
std::unique_ptr<QDialog> NewEnglishWordsDialog(QWidget* parent);

}  // namespace mozc::gui

#endif  // MOZC_GUI_CONFIG_DIALOG_ENGLISH_WORDS_DIALOG_H_
