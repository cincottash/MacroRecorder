// Portable key names (as stored in macro files) <-> X11 keysyms and Linux input-event key codes.
#pragma once

#include <QKeySequence>
#include <QString>

namespace platform::lnx {

QString nameFromKeysym(unsigned long keysym);      // unshifted keysym -> "A", "Enter", ...; empty if unknown
unsigned long keysymFromName(const QString& name); // 0 if unknown
QString modifierFromKeysym(unsigned long keysym);  // "Shift", "Ctrl", "Alt", "AltGr", "Win" or empty
unsigned long keysymFromChar(uint codepoint);      // the keysym that types this character
uint charFromKeysym(unsigned long keysym);         // 0 if it doesn't type a character
bool keysymFromQt(const QKeyCombination& kc, unsigned long* keysym, unsigned* x11Mods);

int evdevFromName(const QString& name);            // Linux KEY_* code, 0 if unknown
// US-layout key + Shift for an ASCII character (uinput has no idea of the keyboard layout). False if none.
bool evdevFromAscii(char c, int* code, bool* shift);

}  // namespace platform::lnx
