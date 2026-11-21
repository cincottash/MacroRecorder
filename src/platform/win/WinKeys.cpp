#include "WinKeys.h"

#include <windows.h>

namespace platform::win {

namespace {

struct KeyInfo { unsigned vk; const char* name; };
// Punctuation keys are named by their US-layout meaning; the same physical key is pressed on any layout.
constexpr KeyInfo kKeys[] = {
    {VK_BACK, "Backspace"}, {VK_TAB, "Tab"}, {VK_RETURN, "Enter"}, {VK_PAUSE, "Pause"}, {VK_CAPITAL, "CapsLock"},
    {VK_ESCAPE, "Escape"}, {VK_SPACE, "Space"}, {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"}, {VK_END, "End"},
    {VK_HOME, "Home"}, {VK_LEFT, "Left"}, {VK_UP, "Up"}, {VK_RIGHT, "Right"}, {VK_DOWN, "Down"},
    {VK_SNAPSHOT, "PrintScreen"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_APPS, "Menu"},
    {VK_NUMPAD0, "Num0"}, {VK_NUMPAD1, "Num1"}, {VK_NUMPAD2, "Num2"}, {VK_NUMPAD3, "Num3"}, {VK_NUMPAD4, "Num4"},
    {VK_NUMPAD5, "Num5"}, {VK_NUMPAD6, "Num6"}, {VK_NUMPAD7, "Num7"}, {VK_NUMPAD8, "Num8"}, {VK_NUMPAD9, "Num9"},
    {VK_MULTIPLY, "NumMultiply"}, {VK_ADD, "NumAdd"}, {VK_SUBTRACT, "NumSubtract"}, {VK_DECIMAL, "NumDecimal"},
    {VK_DIVIDE, "NumDivide"}, {VK_NUMLOCK, "NumLock"}, {VK_SCROLL, "ScrollLock"},
    {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"}, {VK_MENU, "Alt"}, {VK_LWIN, "Win"},
    {VK_OEM_1, "Semicolon"}, {VK_OEM_PLUS, "Equals"}, {VK_OEM_COMMA, "Comma"}, {VK_OEM_MINUS, "Minus"},
    {VK_OEM_PERIOD, "Period"}, {VK_OEM_2, "Slash"}, {VK_OEM_3, "Backquote"}, {VK_OEM_4, "LeftBracket"},
    {VK_OEM_5, "Backslash"}, {VK_OEM_6, "RightBracket"}, {VK_OEM_7, "Quote"}, {VK_OEM_102, "IntlBackslash"},
    {VK_VOLUME_MUTE, "VolumeMute"}, {VK_VOLUME_DOWN, "VolumeDown"}, {VK_VOLUME_UP, "VolumeUp"},
    {VK_MEDIA_NEXT_TRACK, "MediaNext"}, {VK_MEDIA_PREV_TRACK, "MediaPrevious"}, {VK_MEDIA_STOP, "MediaStop"},
    {VK_MEDIA_PLAY_PAUSE, "MediaPlayPause"}, {VK_BROWSER_BACK, "BrowserBack"}, {VK_BROWSER_FORWARD, "BrowserForward"},
};

}  // namespace

QString modifierName(unsigned vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return QStringLiteral("Shift");
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return QStringLiteral("Ctrl");
    case VK_MENU: case VK_LMENU: case VK_RMENU: return QStringLiteral("Alt");
    case VK_LWIN: case VK_RWIN: return QStringLiteral("Win");
    default: return {};
    }
}

bool isModifierKey(unsigned vk) { return !modifierName(vk).isEmpty(); }

QString keyName(unsigned vk)
{
    const QString mod = modifierName(vk);
    if (!mod.isEmpty()) return mod;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return QString(QChar(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return QStringLiteral("F%1").arg(vk - VK_F1 + 1);
    for (const auto& k : kKeys)
        if (k.vk == vk) return QString::fromLatin1(k.name);
    return QStringLiteral("VK_0x") + QString::number(vk, 16).toUpper().rightJustified(2, QLatin1Char('0'));
}

unsigned keyCode(const QString& name)
{
    if (name.size() == 1) {
        const QChar c = name.at(0).toUpper();
        if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))) return c.unicode();
    }
    if (name.size() >= 2 && name.at(0) == QLatin1Char('F')) {
        bool ok = false;
        const int n = name.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (name.startsWith(QLatin1String("VK_0x"))) {
        bool ok = false;
        const unsigned v = name.mid(5).toUInt(&ok, 16);
        return ok ? v : 0;
    }
    for (const auto& k : kKeys)
        if (name.compare(QLatin1String(k.name), Qt::CaseInsensitive) == 0) return k.vk;
    return 0;
}

bool isExtendedKey(unsigned vk)
{
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_RCONTROL: case VK_RMENU:
    case VK_LWIN: case VK_RWIN: case VK_APPS: case VK_DIVIDE: case VK_NUMLOCK: case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

bool hotkeyFromSequence(const QKeySequence& seq, unsigned* vk, unsigned* mods)
{
    if (seq.isEmpty()) return false;
    const QKeyCombination kc = seq[0];
    const Qt::Key k = kc.key();
    unsigned v = 0;
    if (k >= Qt::Key_A && k <= Qt::Key_Z) v = 'A' + (k - Qt::Key_A);
    else if (k >= Qt::Key_0 && k <= Qt::Key_9) v = '0' + (k - Qt::Key_0);
    else if (k >= Qt::Key_F1 && k <= Qt::Key_F24) v = VK_F1 + (k - Qt::Key_F1);
    else {
        switch (k) {
        case Qt::Key_Escape: v = VK_ESCAPE; break;
        case Qt::Key_Space: v = VK_SPACE; break;
        case Qt::Key_Tab: v = VK_TAB; break;
        case Qt::Key_Return: case Qt::Key_Enter: v = VK_RETURN; break;
        case Qt::Key_Backspace: v = VK_BACK; break;
        case Qt::Key_Insert: v = VK_INSERT; break;
        case Qt::Key_Delete: v = VK_DELETE; break;
        case Qt::Key_Home: v = VK_HOME; break;
        case Qt::Key_End: v = VK_END; break;
        case Qt::Key_PageUp: v = VK_PRIOR; break;
        case Qt::Key_PageDown: v = VK_NEXT; break;
        case Qt::Key_Left: v = VK_LEFT; break;
        case Qt::Key_Right: v = VK_RIGHT; break;
        case Qt::Key_Up: v = VK_UP; break;
        case Qt::Key_Down: v = VK_DOWN; break;
        case Qt::Key_Pause: v = VK_PAUSE; break;
        case Qt::Key_Print: v = VK_SNAPSHOT; break;
        case Qt::Key_ScrollLock: v = VK_SCROLL; break;
        default: return false;
        }
    }
    unsigned m = 0;
    const Qt::KeyboardModifiers km = kc.keyboardModifiers();
    if (km & Qt::ControlModifier) m |= MOD_CONTROL;
    if (km & Qt::AltModifier) m |= MOD_ALT;
    if (km & Qt::ShiftModifier) m |= MOD_SHIFT;
    if (km & Qt::MetaModifier) m |= MOD_WIN;
    *vk = v;
    *mods = m;
    return true;
}

}  // namespace platform::win
