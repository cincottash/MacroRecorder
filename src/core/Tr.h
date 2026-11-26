// Text the user sees goes through T(), so it shows in the language chosen at the first start (File > Settings >
// Language). The translations are in src/app/i18n/<code>.json; text without one shows in English.
#pragma once

#include <QCoreApplication>
#include <QString>

// English text the user sees (a label, tooltip, message or log line)
inline QString T(const char* english) { return QCoreApplication::translate("app", english); }

// The same, with a note for the translator where the same English needs different words in different places
// ("Wait" the step type, "Wait" the verb). The note is part of the key in the translation file.
inline QString T(const char* english, const char* note) { return QCoreApplication::translate("app", english, note); }
