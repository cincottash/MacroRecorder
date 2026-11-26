// The app data folder: chosen by the user at first run (or "don't keep app data": a temporary folder deleted on
// close). The choice itself is remembered where the app can find it before anything else is opened:
// Windows registry HKCU\Software\<org>\<app>\DataFolder, elsewhere ~/.config/<app>/data_folder.
#pragma once

#include <QString>

class QWidget;

namespace appdata {

QString suggestedFolder();
QString rememberedChoice();                 // empty if never chosen; "<none>" for "don't keep"
void rememberChoice(const QString& folder);
// Resolve the folder to use: --data-folder override, the remembered choice, or ask (first run).
// Returns an empty string if the user cancelled the first-run dialog.
QString resolve(const QString& override, QWidget* parent);
// The settings file of the folder already chosen (or given with --data-folder), without asking anything; empty
// before the first-run choice and for "don't keep". Read at start for the language, before any window opens.
QString knownSettingsFile(const QString& override);
bool isTemporary();
bool usesOverride();                        // this run's folder came from --data-folder (portable use, tests)
// File > Settings > Start Over: forget the folder choice, so the next start asks again (the welcome window).
// Does nothing while --data-folder is in use: that folder is the caller's choice, not the remembered one.
void forgetChoice();
QString settingsFile();                     // <folder>/settings.json
QString folder();
// File > Settings > Change app data folder: ask, move settings.json, remember. False if cancelled.
bool change(QWidget* parent, QString* message);

}  // namespace appdata
