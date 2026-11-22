// Plays a macro on a background thread through an Injector (the platform's way of moving the mouse and typing).
#pragma once

#include "Input.h"
#include "Paths.h"

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QThread>

#include <atomic>

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

signals:
    void step(int index, int round, int rounds);       // rounds = 0 when repeating by time or forever
    void message(const QString& text, bool warning);
    void notify(const QString& text);
    void messageBox(const QString& text);              // show it, then call answerMessageBox()
    void beep();
    void finished(bool stopped, const QString& why);

private:
    void run();
    bool waitMs(double ms);              // sleeps, honoring pause and stop; false if stopped
    bool playAction(const Action& a, int index);
    // Plays acts[first..last] once; false if stopped. depth > 0: inside a "Run macro" step.
    bool playList(const QList<Action>& acts, int first, int last, int round, int rounds, int depth);
    QPoint target(const Action& a);

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
};

}  // namespace core
