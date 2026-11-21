// Windows virtual-key codes <-> the portable key names stored in macro files.
#pragma once

#include <QKeySequence>
#include <QString>

namespace platform::win {

QString keyName(unsigned vk);                  // empty if unknown
unsigned keyCode(const QString& name);         // 0 if unknown
bool isExtendedKey(unsigned vk);               // needs KEYEVENTF_EXTENDEDKEY when injected
bool isModifierKey(unsigned vk);
QString modifierName(unsigned vk);             // "Shift", "Ctrl", "Alt", "Win" or empty
// A Qt key sequence (first combination) -> virtual key + MOD_* flags. False if the key can't be a hotkey.
bool hotkeyFromSequence(const QKeySequence& seq, unsigned* vk, unsigned* mods);

}  // namespace platform::win
