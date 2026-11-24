// Plays a macro on a background thread through an Injector (the platform's way of moving the mouse and typing).
#pragma once

#include "Expr.h"
#include "Input.h"
#include "Paths.h"

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QThread>

#include <atomic>
#include <functional>

namespace core {

struct PlayOptions {
    double speed = 1.0;               // 2.0 = twice as fast
    RepeatMode repeatMode = RepeatMode::Times;
    int repeatCount = 1;              // Times: how many times; Seconds: how long
    PathStyle path = PathStyle::AsRecorded;
    bool typingAsRecorded = true;     // false: type text instantly
    bool exactTiming = true;          // keep the short pauses that weren't turned into Wait actions
    bool restoreMouse = true;
    int first = 0, last = -1;         // range of actions to play (last = -1: to the end)
};

class Player : public QObject {
    Q_OBJECT
public:
    explicit Player(Injector* inj, QObject* parent = nullptr);
    ~Player() override;

    bool start(const Macro& m, const PlayOptions& o);  // false if already playing
    void stop();
    void setPaused(bool p) { m_paused = p; }
    bool paused() const { return m_paused; }
    bool running() const { return m_running; }
    void answerMessageBox() { m_boxOpen = false; }   // the user pressed OK on a Message box step
    // The user answered an Ask for Text step (ok false: cancelled, which stops playback).
    void answerAsk(const QString& text, bool ok);
    // The AI server answered an Ask AI step (ok false: `text` says what went wrong; playback stops).
    void answerAI(const QString& text, bool ok);
    // The clipboard's text, kept up to date by the window (the player's thread can't read the clipboard itself).
    void setClipboardCache(const QString& text);
    // The variables when playback ended (for the window's log and for tests). Read after finished().
    QHash<QString, Value> variables() const;
    // Tests: random numbers from this function instead of the system's.
    void setRandomForTests(std::function<qint64(qint64, qint64)> f) { m_scope.random = std::move(f); }

    // Block structure of a list of steps: for each If its Else (or End If), for each Else its End If, for each
    // Group its End Group. A missing end is the end of the list (last + 1). Public for the list's indentation.
    struct Blocks {
        QHash<int, int> next;    // If -> its Else or End If; Else -> its End If; Group -> its End Group
        QHash<int, int> endOf;   // If -> its End If
        QStringList problems;    // "Else without If", ...
    };
    static Blocks blocks(const QList<Action>& acts, int first, int last);

signals:
    void step(int index, int round, int rounds);       // rounds = 0 when repeating by time or forever
    void message(const QString& text, bool warning);
    void detail(const QString& text);                  // for the detailed log: variables set, conditions checked
    void notify(const QString& text);
    void messageBox(const QString& text);              // show it, then call answerMessageBox()
    void askText(const QString& question, const QString& defaultText);   // show it, then call answerAsk()
    void askAI(const QString& prompt, int timeoutMs);  // send it to the AI server, then call answerAI()
    void clipboard(const QString& text);               // put this text on the clipboard
    void beep();
    void finished(bool stopped, const QString& why);

private:
    void run();
    bool waitMs(double ms);              // sleeps, honoring pause and stop; false if stopped
    bool waitPlanned(double ms);         // a planned pause, shortened by any time lost earlier (m_late)
    double activeMs(const QElapsedTimer& t, qint64 pausedAtStart) const;   // t's time minus time spent paused since
    bool playAction(const Action& a, int index);
    // Plays acts[first..last] once; false if stopped. depth > 0: inside a "Run macro" step.
    bool playList(const QList<Action>& acts, int first, int last, int round, int rounds, int depth);
    QPoint target(const Action& a);
    QString sub(const QString& text);              // {name} and {=expression}, problems reported once per step
    bool condition(const QString& expr, const QString& what);   // false on errors (reported)
    bool findImage(const Action& a);
    bool saveScreenshot(const Action& a);
    void setVar(const QString& name, const Value& v);

    Injector* m_inj;
    Macro m_macro;
    PlayOptions m_o;
    QThread* m_thread = nullptr;
    std::atomic<bool> m_stop{false}, m_paused{false}, m_running{false}, m_boxOpen{false};
    int m_depth = 0;
    QString m_stopWhy;
    QSet<int> m_held;
    QHash<QString, double> m_heldKeys;   // keys held by KeyDown -> when (ms on m_clock) to repeat them next
    QElapsedTimer m_clock;
    int m_repeatDelay = 500, m_repeatInterval = 33;
    bool m_problemShown = false;   // mouse buttons pressed by MouseDown and not yet released
    double m_late = 0;             // ms behind the recording's timing (slow input); taken out of the next planned pauses
    qint64 m_pausedNs = 0;         // time spent paused during this playback
    Scope m_scope;
    mutable QMutex m_mx;           // m_clipboard, m_answer, the variables after playback
    QString m_clipboard, m_answer;
    std::atomic<bool> m_askOpen{false}, m_askOk{false}, m_aiOpen{false}, m_aiOk{false};
    QString m_aiAnswer;            // guarded by m_mx
};

}  // namespace core
