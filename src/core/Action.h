// One step of a macro, and the macro itself (a list of steps plus file I/O).
#pragma once

#include <QJsonObject>
#include <QList>
#include <QPoint>
#include <QString>
#include <QVector>

namespace core {

enum class ActionType {
    MouseMove,    // move along a path (from -> pos)
    MouseClick,   // click / double click (count) at pos
    MouseDown,    // press and hold (drags)
    MouseUp,
    Wheel,        // scroll: notches (+ up / - down), horizontal flag
    KeyPress,     // key (with modifiers) pressed and released
    KeyDown,      // key held (games, shortcuts held while clicking)
    KeyUp,
    Text,         // typed text
    Wait,         // pause, ms
    WindowFocus,  // bring a program's window to the front (exe + title), optionally wait for it
    WaitPixel,    // wait until a screen pixel has a color
    WaitKey,      // wait until a key is pressed
    Repeat,       // jump back to a label N times / for N seconds / forever, then continue
    Goto,         // jump to a label
    RunProgram,   // start a program with arguments
    Notify,       // show a desktop notification
};

enum class MouseButton { Left, Right, Middle, X1, X2 };
enum class TitleMatch { Exact, StartsWith, Contains, Any };
enum class RepeatMode { Times, Seconds, Forever };

struct PathPoint {
    int x = 0, y = 0;
    int t = 0;  // ms since the move started
};

struct Stroke {   // one typed character and when it was typed, for "typing as recorded"
    QString ch;
    int t = 0;    // ms since the text action started
};

struct Action {
    ActionType type = ActionType::Wait;
    bool enabled = true;
    QString label;     // jump target name (Repeat / Goto)
    QString comment;
    int delayMs = 0;   // pause before this action that was shorter than the "shortest pause kept" (exact timing)

    // Mouse
    QPoint pos;                 // target (click / move end / down / up / wheel / pixel)
    QPoint from;                // move start
    QVector<PathPoint> path;    // recorded path (move)
    MouseButton button = MouseButton::Left;
    int clicks = 1;             // 2 = double click
    int notches = 0;            // wheel
    bool horizontal = false;
    bool relativeToWindow = false;  // pos is relative to the active window's top-left

    // Keyboard
    QString key;                // portable key name, e.g. "Enter", "A", "F5"
    QStringList mods;           // "Ctrl", "Shift", "Alt", "Win"
    QString text;               // Text action
    QVector<Stroke> strokes;    // Text timing

    // Wait / timeouts / durations (ms)
    int ms = 0;

    // Window
    QString exe;                // e.g. "notepad.exe"
    QString title;
    TitleMatch titleMatch = TitleMatch::Exact;

    // Pixel
    QString color;              // "#RRGGBB"
    int tolerance = 0;          // percent

    // Flow
    QString target;             // label to jump to
    RepeatMode repeatMode = RepeatMode::Times;
    int count = 1;

    // Program / notification
    QString program;
    QString arguments;
    QString message;

    QString typeName() const;   // "Left click", "Mouse move", ...
    QString valueText() const;  // "1600, 600 px", "932 ms", ...
    int durationMs() const;     // time this action itself takes when played as recorded

    QJsonObject toJson() const;
    static Action fromJson(const QJsonObject& o, bool* ok = nullptr);
};

QString buttonName(MouseButton b);
QString typeId(ActionType t);              // stable id used in files, e.g. "mouse_click"
bool typeFromId(const QString& id, ActionType* out);

struct Macro {
    QList<Action> actions;
    int totalMs() const;                    // played as recorded, at 100%
    bool save(const QString& path, QString* error) const;
    static bool load(const QString& path, Macro* out, QString* error);
};

}  // namespace core
