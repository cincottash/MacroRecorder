// One step of a macro, and the macro itself (a list of steps plus file I/O).
#pragma once

#include <QJsonObject>
#include <QList>
#include <QByteArray>
#include <QPoint>
#include <QRect>
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
    WaitFile,     // wait until a file exists / is gone / changes
    MessageBox,   // show a message and wait until OK is pressed
    Beep,         // play the system sound
    RunMacro,     // play another macro file at this point
    SetVariable,  // var = expr (see Expr.h)
    If,           // expr: the steps up to the matching Else / End If play only when it's true
    Else,
    EndIf,
    AskText,      // ask the user for text while playing (message = question, text = default answer), into var
    Group,        // a named, foldable block of steps up to the matching End Group (label = its name); disabled: skipped
    EndGroup,
    WindowMove,   // move / resize / maximize / minimize a program's window (exe + title, rect, windowState)
    FindImage,    // look for a picture on the screen (image), optionally wait for it and click it
    Screenshot,   // save a picture of the screen (or a part of it) to a file
    SetClipboard, // put text on the clipboard
    AskAI,        // send a prompt (message) to the AI server set in the settings; the answer goes into var
    FindText,     // read the text in an area (into var), or look for `text` there: wait for it, point at it, click it
};
constexpr ActionType kLastActionType = ActionType::FindText;

enum class MouseButton { Left, Right, Middle, X1, X2 };
enum class TitleMatch { Exact, StartsWith, Contains, Any };
enum class RepeatMode { Times, Seconds, Forever };
enum class FileCondition { Exists, Gone, Changed };
enum class WindowState { Normal, Maximized, Minimized, Restore };   // Restore: un-maximize / un-minimize only
enum class FindDo { Nothing, Move, Click, DoubleClick, RightClick };

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
    int holdMs = 0;    // how long a click's button or a key was held (0 = a short default)
    int gapMs = 0;     // between the clicks of a double-click, or between scroll notches (0 = a short default)

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

    // File (wait for file, run macro)
    QString file;
    FileCondition fileWhen = FileCondition::Exists;

    // Variables (Set Variable, If, Go To's condition, Ask for Text)
    QString var;                // variable name
    QString expr;               // Set Variable: the value; If / Go To: the condition (Go To: empty = always)
    int maxMs = 0;              // Wait: more than ms = a random wait from ms to maxMs

    // Groups
    bool collapsed = false;     // Group: its steps are folded away in the list

    // Window (Move / Resize Window)
    QRect rect;                 // position and size (normal state)
    WindowState windowState = WindowState::Normal;

    // Find image / screenshot
    QByteArray image;           // PNG bytes of the picture to find
    QRect area;                 // where to look / what to capture; empty = every screen
    FindDo findDo = FindDo::Nothing;
    QPoint offset;              // the click or move lands this far from the picture's center
    bool waitGone = false;      // Find Image: wait until the picture is gone instead
    bool stopIfMissing = false; // Find Image: not found in time stops playback (else: continues; found = 0)

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
