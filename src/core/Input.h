// What the operating-system layer delivers while recording (RawEvent), and what playback needs from it (Injector).
// Each platform (Windows, X11, Wayland) implements these; the recording and playback logic never touches the OS.
#pragma once

#include "Action.h"

#include <QMetaType>
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
};

}  // namespace core

Q_DECLARE_METATYPE(core::RawEvent)
