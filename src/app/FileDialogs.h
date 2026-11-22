#pragma once
// Options for every file and folder dialog of the app. The Windows dialog is drawn by Windows and can't be
// see-through, so while View > Window Opacity is below 100 % the app's own dialog is used (themed, and it follows
// the opacity). At 100 % the system's dialog is kept.

#include <QFileDialog>

namespace filedialogs {

inline int& opacityPercent()
{
    static int pct = 100;   // set by the main window as the slider moves
    return pct;
}

inline QFileDialog::Options options(QFileDialog::Options base = {})
{
    if (opacityPercent() < 100) base |= QFileDialog::DontUseNativeDialog;
    return base;
}

} // namespace filedialogs
