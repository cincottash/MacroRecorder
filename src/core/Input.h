// What the operating-system layer delivers while recording (RawEvent), and what playback needs from it (Injector).
// Each platform (Windows, X11, Wayland) implements these; the recording and playback logic never touches the OS.
#pragma once

#include "Action.h"

#include <QImage>
#include <QMetaType>

#include <climits>
#include <QPoint>
#include <QString>

namespace core {

struct RawEvent {
    enum Kind { Move, Down, Up, Wheel, KeyDown, KeyUp } kind = Move;
    qint64 t = 0;              // ms, monotonic clock
    QPoint pos;                // screen pixels (mouse events; current cursor for key events)
    MouseButton button = MouseButton::Left;
    int wheelDelta = 0;        // 120 per notch; + up/right
    bool horizontal = false;
    QString key;               // portable key name ("A", "Enter", "Shift", ...)
    QString ch;                // text the key produced with the current layout and modifiers; empty if none
    bool isModifier = false;
    quint64 windowId = 0;      // foreground window (Down / KeyDown only)
    QString exe;               // its program file name
    QString title;             // its title
    // Top-left of the window this event belongs to (the window under the pointer for button presses and
    // releases, the active window otherwise), for "positions relative to the window". Unset: not known.
    QPoint windowOrigin{INT_MIN, INT_MIN};
    // Button presses while recording smart clicks: a small picture of the screen around the pointer, taken before
    // the click changed anything, and where the pointer is in it
    QImage around;
    QPoint aroundAt;
};

// A word read from a picture of the screen (Find Text steps): its text, where it is in the picture, its line.
struct TextWord {
    QString text;
    QRect box;
    int line = 0;
};

class Injector {
public:
    virtual ~Injector() = default;
    virtual QPoint cursor() = 0;
    virtual void moveTo(QPoint p) = 0;                    // exact pixel
    virtual void button(MouseButton b, bool down) = 0;
    virtual void wheel(int notches, bool horizontal) = 0;
    virtual bool key(const QString& name, bool down) = 0; // false if the key name is unknown here
    virtual void typeText(const QString& ch) = 0;         // one character, layout-independent
    virtual bool focusWindow(const QString& exe, const QString& title, TitleMatch match) = 0;
    virtual QPoint activeWindowOrigin() = 0;
    virtual QString pixelColor(QPoint p) = 0;             // "#RRGGBB"
    virtual bool isKeyDown(const QString& name) = 0;
    virtual void setPreciseTiming(bool on) { Q_UNUSED(on) }  // e.g. 1 ms timer resolution on Windows during playback
    // A problem the system reported since the last call (e.g. input blocked), or empty. Reported once per playback.
    virtual QString takeProblem() { return {}; }
    // A real keyboard repeats a held key: first after delayMs, then every intervalMs (the user's system settings).
    virtual void keyRepeat(int* delayMs, int* intervalMs) { *delayMs = 500; *intervalMs = 33; }
    // Windows: send keys as hardware scan codes only (some games read nothing else). Ignored elsewhere.
    virtual void setScanCodesOnly(bool on) { Q_UNUSED(on) }

    // Move / Resize Window: the first window that matches (empty exe and TitleMatch::Any: the active window).
    // `r` is used for WindowState::Normal (screen pixels, the window's outer frame). False: no such window.
    virtual bool moveWindow(const QString& exe, const QString& title, TitleMatch match, WindowState state, const QRect& r)
    {
        Q_UNUSED(exe) Q_UNUSED(title) Q_UNUSED(match) Q_UNUSED(state) Q_UNUSED(r)
        return false;
    }
    // The top-level window at a screen point: its program, title and frame. False: none (or not supported here).
    virtual bool windowAt(QPoint p, QString* exe, QString* title, QRect* frame)
    {
        Q_UNUSED(p) Q_UNUSED(exe) Q_UNUSED(title) Q_UNUSED(frame)
        return false;
    }
    virtual QRect screenBounds() { return {}; }                  // every screen together, in screen pixels
    virtual QImage grabScreen(const QRect& r) { Q_UNUSED(r) return {}; }   // screen pixels; null: not supported here
    // Reads the words in a picture (the system's text recognition). False with the reason if it can't.
    virtual bool readText(const QImage& img, QVector<TextWord>* words, QString* error)
    {
        Q_UNUSED(img) Q_UNUSED(words)
        if (error) *error = QStringLiteral("Reading text from the screen isn't available on this system.");
        return false;
    }
};

}  // namespace core

Q_DECLARE_METATYPE(core::RawEvent)
