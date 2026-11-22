// Custom themes: reading a theme from a single file (the app's own format, or a VS Code color theme), writing the
// app's format, and storing imported themes in the settings so they're kept between runs.
#pragma once

#include "Themes.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace themefile {

struct Result {
    themes::Theme theme;   // group "Custom Themes", a key starting with "custom_", every color filled in
    QStringList notes;     // things worth telling the user (a renamed theme, low contrast)
    QString error;         // non-empty: nothing usable was found
    bool ok() const { return error.isEmpty(); }
};

// The color keys every theme has, in the order they're written.
const QStringList& colorKeys();

// Reads a theme file. `fallbackName` names the theme when the file doesn't (normally the file's name).
Result parse(const QByteArray& data, const QString& fallbackName);
Result read(const QString& path);

// The app's own format, with every color: what Export writes, and what's kept in the settings.
QByteArray write(const themes::Theme& t, bool indented = true);

// The custom themes kept in the settings (one compact JSON text each). Entries that no longer parse are skipped.
QList<themes::Theme> fromStored(const QStringList& stored);

QString keyFor(const QString& name);   // "custom_" + the name in lower case, letters and digits only

}  // namespace themefile
