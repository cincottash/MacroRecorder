// Things the app asks of the operating system outside input: after-playback actions, start at login,
// running as administrator, the system sound. Implemented per system (win/WinBackend.cpp, linux/LinuxBackend.cpp);
// the app reaches them through Backend, so tests can replace them.
#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

namespace platform {

enum class SystemAction { Lock, Sleep, LogOff, Restart, ShutDown };

namespace sys {
bool run(SystemAction a, QString* error);
bool startsAtLogin();
bool setStartAtLogin(bool on, QString* error);
bool isElevated();                                            // Windows: running as administrator
bool restartElevated(const QStringList& args, QString* error); // Windows: start again as administrator (asks first)
void beep();
// The list separator of the user's regional settings: what spreadsheets such as Excel expect between CSV fields
// ("," in the US and UK, ";" where a comma is the decimal mark, as in most of Europe).
QChar listSeparator();
// Windows: centre a top-level window on a point given in physical screen pixels (exact on scaled screens).
bool centerWindowAt(quintptr windowId, QPoint physical);
}  // namespace sys

}  // namespace platform
