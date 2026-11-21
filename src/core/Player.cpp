#include "Player.h"

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QProcess>

namespace core {

namespace {

constexpr int kClickHoldMs = 40;     // button held this long for a click
constexpr int kDoubleGapMs = 70;     // between the two clicks of a double-click
constexpr int kKeyHoldMs = 30;
constexpr int kWheelGapMs = 40;
constexpr int kPollMs = 40;          // waits that watch the screen or keyboard
constexpr int kWindowFallbackMs = 1500;

bool colorClose(const QString& a, const QString& b, int tolerancePct)
{
    const QColor x(a), y(b);
    if (!x.isValid() || !y.isValid()) return false;
    const int limit = 255 * tolerancePct / 100;
    return qAbs(x.red() - y.red()) <= limit && qAbs(x.green() - y.green()) <= limit && qAbs(x.blue() - y.blue()) <= limit;
}

}  // namespace

Player::Player(Injector* inj, QObject* parent) : QObject(parent), m_inj(inj) {}

Player::~Player()
{
    stop();
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

bool Player::start(const Macro& m, const PlayOptions& o)
{
    if (m_running) return false;
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    m_macro = m;
    m_o = o;
    if (m_o.speed <= 0) m_o.speed = 1;
    m_stop = false;
    m_paused = false;
    m_running = true;
    m_stopWhy.clear();
    m_thread = QThread::create([this] { run(); });
    m_thread->start(QThread::TimeCriticalPriority);
    return true;
}

void Player::stop() { m_stop = true; }

bool Player::waitMs(double ms)
{
    QElapsedTimer t;
    t.start();
    qint64 pausedFor = 0;
    while (true) {
        if (m_stop) return false;
        if (m_paused) {
            QElapsedTimer p;
            p.start();
            while (m_paused && !m_stop) QThread::msleep(10);
            pausedFor += p.elapsed();
            continue;
        }
        const double left = ms - double(t.nsecsElapsed()) / 1e6 + double(pausedFor);
        if (left <= 0) return true;
        if (left > 2) QThread::usleep(qMin<qint64>(qint64((left - 1) * 1000), 10000));
        else QThread::yieldCurrentThread();
    }
}

QPoint Player::target(const Action& a)
{
    return a.relativeToWindow ? a.pos + m_inj->activeWindowOrigin() : a.pos;
}

bool Player::playAction(const Action& a, int index)
{
    Q_UNUSED(index)
    const double sp = m_o.speed;
    if (m_o.exactTiming && a.delayMs > 0 && !waitMs(a.delayMs / sp)) return false;

    auto moveInstant = [&](QPoint p) {
        if (m_inj->cursor() != p) m_inj->moveTo(p);
    };

    switch (a.type) {
    case ActionType::MouseMove: {
        const QPoint to = target(a);
        const QPoint from = m_inj->cursor();
        const auto pts = makePath(m_o.path, from, to, int(a.ms / sp),
                                  m_o.path == PathStyle::AsRecorded ? a.path : QVector<PathPoint>(), quint32(index * 7919 + 1));
        QElapsedTimer t;
        t.start();
        const double scale = (m_o.path == PathStyle::AsRecorded && a.path.size() >= 2) ? 1.0 / sp : 1.0;
        double elapsed = 0;
        for (const PathPoint& p : pts) {
            const double due = p.t * scale;
            if (due > elapsed && !waitMs(due - elapsed)) return false;
            elapsed = qMax(elapsed, due);
            m_inj->moveTo(QPoint(p.x, p.y));
        }
        break;
    }
    case ActionType::MouseClick:
        moveInstant(target(a));
        for (int i = 0; i < a.clicks; ++i) {
            if (i > 0 && !waitMs(kDoubleGapMs)) return false;
            m_inj->button(a.button, true);
            if (!waitMs(kClickHoldMs)) { m_inj->button(a.button, false); return false; }
            m_inj->button(a.button, false);
        }
        break;
    case ActionType::MouseDown:
        moveInstant(target(a));
        m_inj->button(a.button, true);
        m_held.insert(int(a.button));
        break;
    case ActionType::MouseUp:
        moveInstant(target(a));
        m_inj->button(a.button, false);
        m_held.remove(int(a.button));
        break;
    case ActionType::Wheel:
        moveInstant(target(a));
        for (int i = 0; i < qAbs(a.notches); ++i) {
            if (i > 0 && !waitMs(kWheelGapMs / sp)) return false;
            m_inj->wheel(a.notches > 0 ? 1 : -1, a.horizontal);
        }
        break;
    case ActionType::KeyPress:
    case ActionType::KeyDown:
    case ActionType::KeyUp: {
        const bool down = a.type != ActionType::KeyUp, up = a.type != ActionType::KeyDown;
        if (down) for (const QString& m : a.mods) m_inj->key(m, true);
        bool ok = true;
        if (down) ok = m_inj->key(a.key, true);
        if (down && up && !waitMs(kKeyHoldMs)) { m_inj->key(a.key, false); return false; }
        if (up) ok = m_inj->key(a.key, false) && ok;
        if (up) for (auto it = a.mods.crbegin(); it != a.mods.crend(); ++it) m_inj->key(*it, false);
        if (!ok) emit message(QStringLiteral("Unknown key “%1” was skipped.").arg(a.key), true);
        break;
    }
    case ActionType::Text: {
        if (m_o.typingAsRecorded && a.strokes.size() > 0) {
            double elapsed = 0;
            for (const Stroke& s : a.strokes) {
                const double due = s.t / sp;
                if (due > elapsed && !waitMs(due - elapsed)) return false;
                elapsed = qMax(elapsed, due);
                m_inj->typeText(s.ch);
            }
        } else {
            for (int i = 0; i < a.text.size(); ++i) {
                QString ch = a.text.mid(i, 1);
                if (a.text.at(i).isHighSurrogate() && i + 1 < a.text.size()) ch = a.text.mid(i++, 2);
                m_inj->typeText(ch);
                if (!waitMs(2)) return false;
            }
        }
        break;
    }
    case ActionType::Wait:
        if (!waitMs(a.ms / sp)) return false;
        break;
    case ActionType::WindowFocus: {
        QElapsedTimer t;
        t.start();
        bool loose = false;
        while (true) {
            if (m_inj->focusWindow(a.exe, a.title, loose ? TitleMatch::Any : a.titleMatch)) {
                if (loose) emit message(QStringLiteral("No window titled “%1”; used another %2 window.").arg(a.title, a.exe), true);
                break;
            }
            if (!loose && a.titleMatch != TitleMatch::Any && t.elapsed() >= kWindowFallbackMs && !a.exe.isEmpty()) loose = true;
            if (a.ms > 0 && t.elapsed() >= a.ms) {
                m_stopWhy = QStringLiteral("Couldn't find the window “%1” (%2) within %3 s.")
                                .arg(a.title, a.exe).arg(a.ms / 1000.0, 0, 'f', 1);
                return false;
            }
            if (!waitMs(kPollMs * 2)) return false;
        }
        if (!waitMs(60)) return false;  // let the window settle before input reaches it
        break;
    }
    case ActionType::WaitPixel: {
        QElapsedTimer t;
        t.start();
        const QPoint p = target(a);
        while (!colorClose(m_inj->pixelColor(p), a.color, a.tolerance)) {
            if (a.ms > 0 && t.elapsed() >= a.ms) {
                emit message(QStringLiteral("Pixel %1, %2 px didn't turn %3 within %4 s; continuing.")
                                 .arg(p.x()).arg(p.y()).arg(a.color).arg(a.ms / 1000.0, 0, 'f', 1), true);
                break;
            }
            if (!waitMs(kPollMs)) return false;
        }
        break;
    }
    case ActionType::WaitKey: {
        QElapsedTimer t;
        t.start();
        while (!m_inj->isKeyDown(a.key)) {
            if (a.ms > 0 && t.elapsed() >= a.ms) {
                emit message(QStringLiteral("“%1” wasn't pressed within %2 s; continuing.").arg(a.key).arg(a.ms / 1000.0, 0, 'f', 1), true);
                break;
            }
            if (!waitMs(15)) return false;
        }
        while (m_inj->isKeyDown(a.key))  // wait for the release, so the key isn't seen twice
            if (!waitMs(15)) return false;
        break;
    }
    case ActionType::RunProgram: {
        if (!QProcess::startDetached(a.program, QProcess::splitCommand(a.arguments)))
            emit message(QStringLiteral("Couldn't start “%1”.").arg(a.program), true);
        break;
    }
    case ActionType::Notify: emit notify(a.message); break;
    case ActionType::Repeat:
    case ActionType::Goto: break;  // handled by run()
    }
    return true;
}

void Player::run()
{
    m_inj->setPreciseTiming(true);
    const QPoint startCursor = m_inj->cursor();
    const auto& acts = m_macro.actions;
    const int first = qBound(0, m_o.first, int(acts.size()));
    const int last = m_o.last < 0 ? int(acts.size()) - 1 : qMin(m_o.last, int(acts.size()) - 1);

    QHash<QString, int> labels;
    for (int i = first; i <= last; ++i)
        if (!acts[i].label.isEmpty() && !labels.contains(acts[i].label)) labels.insert(acts[i].label, i);

    QElapsedTimer total;
    total.start();
    const int rounds = m_o.repeatMode == RepeatMode::Times ? qMax(1, m_o.repeatCount) : 0;
    bool stopped = false;
    for (int round = 1; !stopped; ++round) {
        if (m_o.repeatMode == RepeatMode::Times && round > rounds) break;
        if (m_o.repeatMode == RepeatMode::Seconds && round > 1 && total.elapsed() >= qint64(m_o.repeatCount) * 1000) break;
        QHash<int, int> repeatsDone;          // Repeat action index -> jumps made this round
        QHash<int, qint64> repeatStarted;     // Repeat action index -> when its timed repeat began
        QElapsedTimer roundTimer;
        roundTimer.start();
        int pc = first;
        while (pc <= last) {
            if (m_stop) { stopped = true; break; }
            const Action& a = acts[pc];
            if (!a.enabled) { ++pc; continue; }
            emit step(pc, round, rounds);
            if (a.type == ActionType::Goto || a.type == ActionType::Repeat) {
                const int to = labels.value(a.target, -1);
                if (to < 0) {
                    emit message(QStringLiteral("No action has the label “%1”; skipping the %2.")
                                     .arg(a.target, a.type == ActionType::Goto ? QStringLiteral("go-to") : QStringLiteral("repeat")), true);
                    ++pc;
                    continue;
                }
                bool jump = true;
                if (a.type == ActionType::Repeat) {
                    if (a.repeatMode == RepeatMode::Times) {
                        const int done = repeatsDone.value(pc);
                        jump = done + 1 < a.count;  // "3 times" = the section plays 3 times in total
                        if (jump) repeatsDone[pc] = done + 1; else repeatsDone.remove(pc);
                    } else if (a.repeatMode == RepeatMode::Seconds) {
                        if (!repeatStarted.contains(pc)) repeatStarted[pc] = roundTimer.elapsed();
                        jump = roundTimer.elapsed() - repeatStarted[pc] < qint64(a.count) * 1000;
                        if (!jump) repeatStarted.remove(pc);
                    }
                }
                if (jump && !waitMs(1)) { stopped = true; break; }  // never spin without yielding
                pc = jump ? to : pc + 1;
                continue;
            }
            if (!playAction(a, pc)) { stopped = true; break; }
            ++pc;
        }
    }
    // Never leave buttons or modifier keys held down.
    for (int b : std::as_const(m_held)) m_inj->button(MouseButton(b), false);
    m_held.clear();
    for (const QString& m : {QStringLiteral("Shift"), QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("Win")})
        if (m_inj->isKeyDown(m)) m_inj->key(m, false);
    if (m_o.restoreMouse && !m_stopWhy.size()) m_inj->moveTo(startCursor);
    m_inj->setPreciseTiming(false);
    const QString why = m_stopWhy;
    m_running = false;
    emit finished(stopped, why);
}

}  // namespace core
