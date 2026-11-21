#include "LinuxKeys.h"

#include <X11/X.h>
#include <X11/XF86keysym.h>
#include <X11/keysym.h>
#include <linux/input-event-codes.h>

namespace platform::lnx {

namespace {

struct K { unsigned long sym; const char* name; int evdev; };
// Punctuation keys are named by their US-layout meaning (the same physical key on any layout), as on Windows.
constexpr K kKeys[] = {
    {XK_Return, "Enter", KEY_ENTER}, {XK_KP_Enter, "Enter", KEY_KPENTER}, {XK_Escape, "Escape", KEY_ESC},
    {XK_Tab, "Tab", KEY_TAB}, {XK_ISO_Left_Tab, "Tab", KEY_TAB}, {XK_space, "Space", KEY_SPACE},
    {XK_BackSpace, "Backspace", KEY_BACKSPACE}, {XK_Delete, "Delete", KEY_DELETE}, {XK_Insert, "Insert", KEY_INSERT},
    {XK_Home, "Home", KEY_HOME}, {XK_End, "End", KEY_END}, {XK_Prior, "PageUp", KEY_PAGEUP}, {XK_Next, "PageDown", KEY_PAGEDOWN},
    {XK_Left, "Left", KEY_LEFT}, {XK_Right, "Right", KEY_RIGHT}, {XK_Up, "Up", KEY_UP}, {XK_Down, "Down", KEY_DOWN},
    {XK_Caps_Lock, "CapsLock", KEY_CAPSLOCK}, {XK_Print, "PrintScreen", KEY_SYSRQ}, {XK_Pause, "Pause", KEY_PAUSE},
    {XK_Menu, "Menu", KEY_COMPOSE}, {XK_Num_Lock, "NumLock", KEY_NUMLOCK}, {XK_Scroll_Lock, "ScrollLock", KEY_SCROLLLOCK},
    {XK_Shift_L, "Shift", KEY_LEFTSHIFT}, {XK_Shift_R, "Shift", KEY_RIGHTSHIFT},
    {XK_Control_L, "Ctrl", KEY_LEFTCTRL}, {XK_Control_R, "Ctrl", KEY_RIGHTCTRL},
    {XK_Alt_L, "Alt", KEY_LEFTALT}, {XK_Meta_L, "Alt", KEY_LEFTALT}, {XK_Alt_R, "Alt", KEY_RIGHTALT},
    {XK_ISO_Level3_Shift, "AltGr", KEY_RIGHTALT}, {XK_Super_L, "Win", KEY_LEFTMETA}, {XK_Super_R, "Win", KEY_RIGHTMETA},
    {XK_minus, "Minus", KEY_MINUS}, {XK_equal, "Equals", KEY_EQUAL}, {XK_bracketleft, "LeftBracket", KEY_LEFTBRACE},
    {XK_bracketright, "RightBracket", KEY_RIGHTBRACE}, {XK_semicolon, "Semicolon", KEY_SEMICOLON},
    {XK_apostrophe, "Quote", KEY_APOSTROPHE}, {XK_comma, "Comma", KEY_COMMA}, {XK_period, "Period", KEY_DOT},
    {XK_slash, "Slash", KEY_SLASH}, {XK_backslash, "Backslash", KEY_BACKSLASH}, {XK_grave, "Backquote", KEY_GRAVE},
    {XK_less, "IntlBackslash", KEY_102ND},
    {XK_KP_0, "Num0", KEY_KP0}, {XK_KP_1, "Num1", KEY_KP1}, {XK_KP_2, "Num2", KEY_KP2}, {XK_KP_3, "Num3", KEY_KP3},
    {XK_KP_4, "Num4", KEY_KP4}, {XK_KP_5, "Num5", KEY_KP5}, {XK_KP_6, "Num6", KEY_KP6}, {XK_KP_7, "Num7", KEY_KP7},
    {XK_KP_8, "Num8", KEY_KP8}, {XK_KP_9, "Num9", KEY_KP9}, {XK_KP_Add, "NumAdd", KEY_KPPLUS},
    {XK_KP_Subtract, "NumSubtract", KEY_KPMINUS}, {XK_KP_Multiply, "NumMultiply", KEY_KPASTERISK},
    {XK_KP_Divide, "NumDivide", KEY_KPSLASH}, {XK_KP_Decimal, "NumDecimal", KEY_KPDOT},
    {XF86XK_AudioMute, "VolumeMute", KEY_MUTE}, {XF86XK_AudioLowerVolume, "VolumeDown", KEY_VOLUMEDOWN},
    {XF86XK_AudioRaiseVolume, "VolumeUp", KEY_VOLUMEUP}, {XF86XK_AudioPlay, "MediaPlayPause", KEY_PLAYPAUSE},
    {XF86XK_AudioNext, "MediaNext", KEY_NEXTSONG}, {XF86XK_AudioPrev, "MediaPrevious", KEY_PREVIOUSSONG},
    {XF86XK_AudioStop, "MediaStop", KEY_STOPCD}, {XF86XK_Back, "BrowserBack", KEY_BACK}, {XF86XK_Forward, "BrowserForward", KEY_FORWARD},
};
constexpr int kLetters[] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                            KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
constexpr int kDigits[] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
constexpr int kF[] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
                      KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18, KEY_F19, KEY_F20, KEY_F21, KEY_F22, KEY_F23, KEY_F24};

}  // namespace

QString nameFromKeysym(unsigned long s)
{
    if (s >= XK_a && s <= XK_z) return QString(QChar(char('A' + (s - XK_a))));
    if (s >= XK_A && s <= XK_Z) return QString(QChar(char('A' + (s - XK_A))));
    if (s >= XK_0 && s <= XK_9) return QString(QChar(char('0' + (s - XK_0))));
    if (s >= XK_F1 && s <= XK_F24) return QStringLiteral("F%1").arg(s - XK_F1 + 1);
    for (const K& k : kKeys)
        if (k.sym == s) return QString::fromLatin1(k.name);
    return s ? QStringLiteral("X11_0x%1").arg(s, 0, 16) : QString();
}

unsigned long keysymFromName(const QString& n)
{
    if (n.size() == 1) {
        const QChar c = n.at(0).toUpper();
        if (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) return XK_a + (c.unicode() - 'A');
        if (c >= QLatin1Char('0') && c <= QLatin1Char('9')) return XK_0 + (c.unicode() - '0');
    }
    if (n.size() >= 2 && n.at(0) == QLatin1Char('F')) {
        bool ok = false;
        const int f = n.mid(1).toInt(&ok);
        if (ok && f >= 1 && f <= 24) return XK_F1 + f - 1;
    }
    if (n.startsWith(QLatin1String("X11_0x"))) return n.mid(6).toULong(nullptr, 16);
    for (const K& k : kKeys)
        if (n.compare(QLatin1String(k.name), Qt::CaseInsensitive) == 0) return k.sym;
    return 0;
}

QString modifierFromKeysym(unsigned long s)
{
    switch (s) {
    case XK_Shift_L: case XK_Shift_R: return QStringLiteral("Shift");
    case XK_Control_L: case XK_Control_R: return QStringLiteral("Ctrl");
    case XK_Alt_L: case XK_Alt_R: case XK_Meta_L: case XK_Meta_R: return QStringLiteral("Alt");
    case XK_ISO_Level3_Shift: return QStringLiteral("AltGr");
    case XK_Super_L: case XK_Super_R: return QStringLiteral("Win");
    default: return {};
    }
}

unsigned long keysymFromChar(uint cp)
{
    if ((cp >= 0x20 && cp <= 0x7e) || (cp >= 0xa0 && cp <= 0xff)) return cp;   // Latin-1 keysyms equal the code point
    if (cp == 0x20ac) return XK_EuroSign;
    return 0x01000000 | cp;                                                     // Unicode keysyms
}

uint charFromKeysym(unsigned long s)
{
    if ((s >= 0x20 && s <= 0x7e) || (s >= 0xa0 && s <= 0xff)) return uint(s);
    if (s == XK_EuroSign) return 0x20ac;
    if ((s & 0xff000000) == 0x01000000) return uint(s & 0x00ffffff);
    if (s >= XK_KP_0 && s <= XK_KP_9) return uint('0' + (s - XK_KP_0));
    return 0;
}

bool keysymFromQt(const QKeyCombination& kc, unsigned long* keysym, unsigned* mods)
{
    const Qt::Key k = kc.key();
    unsigned long s = 0;
    if (k >= Qt::Key_A && k <= Qt::Key_Z) s = XK_a + (k - Qt::Key_A);
    else if (k >= Qt::Key_0 && k <= Qt::Key_9) s = XK_0 + (k - Qt::Key_0);
    else if (k >= Qt::Key_F1 && k <= Qt::Key_F24) s = XK_F1 + (k - Qt::Key_F1);
    else {
        switch (k) {
        case Qt::Key_Escape: s = XK_Escape; break;
        case Qt::Key_Space: s = XK_space; break;
        case Qt::Key_Tab: s = XK_Tab; break;
        case Qt::Key_Return: case Qt::Key_Enter: s = XK_Return; break;
        case Qt::Key_Backspace: s = XK_BackSpace; break;
        case Qt::Key_Insert: s = XK_Insert; break;
        case Qt::Key_Delete: s = XK_Delete; break;
        case Qt::Key_Home: s = XK_Home; break;
        case Qt::Key_End: s = XK_End; break;
        case Qt::Key_PageUp: s = XK_Prior; break;
        case Qt::Key_PageDown: s = XK_Next; break;
        case Qt::Key_Left: s = XK_Left; break;
        case Qt::Key_Right: s = XK_Right; break;
        case Qt::Key_Up: s = XK_Up; break;
        case Qt::Key_Down: s = XK_Down; break;
        case Qt::Key_Pause: s = XK_Pause; break;
        case Qt::Key_Print: s = XK_Print; break;
        case Qt::Key_ScrollLock: s = XK_Scroll_Lock; break;
        default: return false;
        }
    }
    unsigned m = 0;
    const Qt::KeyboardModifiers km = kc.keyboardModifiers();
    if (km & Qt::ControlModifier) m |= ControlMask;
    if (km & Qt::ShiftModifier) m |= ShiftMask;
    if (km & Qt::AltModifier) m |= Mod1Mask;
    if (km & Qt::MetaModifier) m |= Mod4Mask;
    *keysym = s;
    *mods = m;
    return true;
}

int evdevFromName(const QString& n)
{
    if (n.size() == 1) {
        const QChar c = n.at(0).toUpper();
        if (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) return kLetters[c.unicode() - 'A'];
        if (c >= QLatin1Char('0') && c <= QLatin1Char('9')) return kDigits[c.unicode() - '0'];
    }
    if (n.size() >= 2 && n.at(0) == QLatin1Char('F')) {
        bool ok = false;
        const int f = n.mid(1).toInt(&ok);
        if (ok && f >= 1 && f <= 24) return kF[f - 1];
    }
    for (const K& k : kKeys)
        if (n.compare(QLatin1String(k.name), Qt::CaseInsensitive) == 0) return k.evdev;
    return 0;
}

bool evdevFromAscii(char c, int* code, bool* shift)
{
    *shift = false;
    if (c >= 'a' && c <= 'z') { *code = kLetters[c - 'a']; return true; }
    if (c >= 'A' && c <= 'Z') { *code = kLetters[c - 'A']; *shift = true; return true; }
    if (c >= '0' && c <= '9') { *code = kDigits[c - '0']; return true; }
    static const struct { char ch; int code; bool shift; } kSym[] = {
        {' ', KEY_SPACE, false}, {'\n', KEY_ENTER, false}, {'\t', KEY_TAB, false},
        {'-', KEY_MINUS, false}, {'_', KEY_MINUS, true}, {'=', KEY_EQUAL, false}, {'+', KEY_EQUAL, true},
        {'[', KEY_LEFTBRACE, false}, {'{', KEY_LEFTBRACE, true}, {']', KEY_RIGHTBRACE, false}, {'}', KEY_RIGHTBRACE, true},
        {';', KEY_SEMICOLON, false}, {':', KEY_SEMICOLON, true}, {'\'', KEY_APOSTROPHE, false}, {'"', KEY_APOSTROPHE, true},
        {',', KEY_COMMA, false}, {'<', KEY_COMMA, true}, {'.', KEY_DOT, false}, {'>', KEY_DOT, true},
        {'/', KEY_SLASH, false}, {'?', KEY_SLASH, true}, {'\\', KEY_BACKSLASH, false}, {'|', KEY_BACKSLASH, true},
        {'`', KEY_GRAVE, false}, {'~', KEY_GRAVE, true}, {'!', KEY_1, true}, {'@', KEY_2, true}, {'#', KEY_3, true},
        {'$', KEY_4, true}, {'%', KEY_5, true}, {'^', KEY_6, true}, {'&', KEY_7, true}, {'*', KEY_8, true},
        {'(', KEY_9, true}, {')', KEY_0, true},
    };
    for (const auto& s : kSym)
        if (s.ch == c) { *code = s.code; *shift = s.shift; return true; }
    return false;
}

}  // namespace platform::lnx
