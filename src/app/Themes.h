// Theme table and the function that styles the whole app (or one widget, for hover previews) from it.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace themes {

// Color keys: bg card border text muted accent accent_hover accent_text input track log_bg
//             ok ok_bg warn warn_bg warn_border err err_bg
struct Theme {
    QString group;  // menu group, e.g. "Dark Themes"
    QString name;   // shown in the menu
    QString key;    // saved in settings
    QHash<QString, QString> colors;
    QString c(const QString& k) const { return colors.value(k); }
};

const QList<Theme>& builtIn();   // the themes that come with the app
const QList<Theme>& all();       // the built-in themes, then the custom ones
const Theme* find(const QString& key);
QStringList groups();            // the built-in groups, in menu order
QString customGroup();           // "Custom Themes": the group of imported themes
void setCustom(const QList<Theme>& list);   // replaces the imported themes (references from all() become invalid)
// A group's or theme's name in the app's language ("Dark Themes", "Light", "Paper"); proper names such as
// "Dracula" stay as they are. Names and groups stay English in the code, the settings and theme files.
QString displayName(const QString& name);
bool systemIsDark();
const Theme& forChoice(const QString& key);  // "system" or unknown keys -> Light/Dark by the OS setting

// The app's base look: Fusion, without the underlined shortcut letters in menus and on buttons (Alt+F and the
// other Alt shortcuts still work). Called once, right after the QApplication is made.
void installStyle();

// Style the whole application, or only `only` and its children (for previews).
void apply(const Theme& t, QWidget* only = nullptr);

}  // namespace themes
