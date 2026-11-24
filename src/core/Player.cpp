#include "Player.h"

#include "ImageFind.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace core {

namespace {

constexpr int kClickHoldMs = 40;     // button held this long for a click
constexpr int kDoubleGapMs = 70;     // between the two clicks of a double-click
constexpr int kKeyHoldMs = 30;
constexpr int kWheelGapMs = 40;
constexpr int kPollMs = 40;          // waits that watch the screen or keyboard
constexpr int kWindowFallbackMs = 1500;
constexpr int kMaxMacroDepth = 5;    // "Run macro" inside "Run macro"...: stops a macro that runs itself

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

void Player::answerAsk(const QString& text, bool ok)
{
    {
        QMutexLocker l(&m_mx);
        m_answer = text;
    }
    m_askOk = ok;
    m_askOpen = false;
}

void Player::answerAI(const QString& text, bool ok)
{
    {
        QMutexLocker l(&m_mx);
        m_aiAnswer = text;
    }
    m_aiOk = ok;
    m_aiOpen = false;
}

void Player::setClipboardCache(const QString& text)
{
    QMutexLocker l(&m_mx);
    m_clipboard = text;
}

QHash<QString, Value> Player::variables() const
{
    QMutexLocker l(&m_mx);
    return m_running ? QHash<QString, Value>() : m_scope.vars;
}

Player::Blocks Player::blocks(const QList<Action>& acts, int first, int last)
{
    Blocks b;
    QList<int> ifs, groups;
    QHash<int, int> elseOf;   // If -> its Else
    auto stepName = [](int i) { return QStringLiteral("Step %1").arg(i + 1); };
    for (int i = first; i <= last && i < acts.size(); ++i) {
        switch (acts.at(i).type) {
        case ActionType::If: ifs << i; break;
        case ActionType::Else:
            if (ifs.isEmpty()) { b.problems << QStringLiteral("%1: Else without an If before it.").arg(stepName(i)); break; }
            if (elseOf.contains(ifs.last())) { b.problems << QStringLiteral("%1: a second Else for the same If.").arg(stepName(i)); break; }
            elseOf.insert(ifs.last(), i);
            b.next.insert(ifs.last(), i);
            break;
        case ActionType::EndIf: {
            if (ifs.isEmpty()) { b.problems << QStringLiteral("%1: End If without an If before it.").arg(stepName(i)); break; }
            const int top = ifs.takeLast();
            if (!b.next.contains(top)) b.next.insert(top, i);
            b.endOf.insert(top, i);
            if (elseOf.contains(top)) b.next.insert(elseOf.value(top), i);
            break;
        }
        case ActionType::Group: groups << i; break;
        case ActionType::EndGroup:
            if (groups.isEmpty()) { b.problems << QStringLiteral("%1: End Group without a Group before it.").arg(stepName(i)); break; }
            b.next.insert(groups.takeLast(), i);
            break;
        default: break;
        }
    }
    const int end = last + 1;
    for (int top : std::as_const(ifs)) {   // an If that's never closed runs to the end of the list
        b.problems << QStringLiteral("%1: If without an End If after it.").arg(stepName(top));
        if (!b.next.contains(top)) b.next.insert(top, end);
        b.endOf.insert(top, end);
        if (elseOf.contains(top)) b.next.insert(elseOf.value(top), end);
    }
    for (int g : std::as_const(groups)) b.next.insert(g, end);   // a group without an end: to the end of the list
    return b;
}

QString Player::sub(const QString& text)
{
    if (!text.contains(QLatin1Char('{'))) return text;
    QStringList errors;
    const QString out = expr::substitute(text, m_scope, &errors);
    for (const QString& e : std::as_const(errors)) emit message(QStringLiteral("In “%1”: %2").arg(text, e), true);
    return out;
}

bool Player::condition(const QString& src, const QString& what)
{
    Value v;
    QString e;
    if (!expr::evaluate(src, m_scope, &v, &e)) {
        emit message(QStringLiteral("%1 “%2”: %3 Treated as false.").arg(what, src, e), true);
        return false;
    }
    emit detail(QStringLiteral("%1 %2: %3").arg(what, src, v.truthy() ? QStringLiteral("true") : QStringLiteral("false")));
    return v.truthy();
}

void Player::setVar(const QString& name, const Value& v)
{
    {
        QMutexLocker l(&m_mx);
        m_scope.vars.insert(name.toLower(), v);
    }
    emit detail(QStringLiteral("%1 = %2").arg(name, v.kind == Value::Text ? QStringLiteral("“%1”").arg(v.str) : v.toText()));
}

bool Player::findImage(const Action& a)
{
    QImage needle;
    needle.loadFromData(a.image);
    if (needle.isNull()) {
        emit message(QStringLiteral("%1: the step has no picture to look for; skipped.").arg(a.typeName()), true);
        setVar(QStringLiteral("found"), Value::number(0));
        return true;
    }
    const QRect area = a.area.isEmpty() ? m_inj->screenBounds() : a.area;
    QElapsedTimer t;
    t.start();
    while (true) {
        const QImage shot = m_inj->grabScreen(area);
        if (shot.isNull()) {
            emit message(QStringLiteral("%1: pictures of the screen can't be taken here; skipped.").arg(a.typeName()), true);
            setVar(QStringLiteral("found"), Value::number(0));
            return true;
        }
        const ImageMatch m = core::findImage(shot, needle, a.tolerance);
        if (a.waitGone && !m.found) { setVar(QStringLiteral("found"), Value::number(0)); return true; }
        if (!a.waitGone && m.found) {
            const QPoint at = area.topLeft() + m.topLeft + QPoint(needle.width() / 2, needle.height() / 2) + a.offset;
            setVar(QStringLiteral("found"), Value::number(1));
            setVar(QStringLiteral("found_x"), Value::number(at.x()));
            setVar(QStringLiteral("found_y"), Value::number(at.y()));
            emit detail(QStringLiteral("Picture found at %1, %2 px (difference %3 %).").arg(at.x()).arg(at.y()).arg(m.score / 2.55, 0, 'f', 1));
            if (a.findDo == FindDo::Nothing) return true;
            if (m_inj->cursor() != at) m_inj->moveTo(at);
            if (a.findDo == FindDo::Move) return true;
            const MouseButton b = a.findDo == FindDo::RightClick ? MouseButton::Right : MouseButton::Left;
            const int clicks = a.findDo == FindDo::DoubleClick ? 2 : 1;
            for (int i = 0; i < clicks; ++i) {
                if (i > 0 && !waitMs(kDoubleGapMs)) return false;
                m_inj->button(b, true);
                if (!waitMs(kClickHoldMs)) { m_inj->button(b, false); return false; }
                m_inj->button(b, false);
            }
            return true;
        }
        if (t.elapsed() >= a.ms) break;
        if (!waitMs(qMin<qint64>(200, qMax<qint64>(1, a.ms - t.elapsed())))) return false;
    }
    const QString secs = QString::number(a.ms / 1000.0, 'f', 1);
    if (a.waitGone) {
        emit message(QStringLiteral("%1: the picture was still there after %2 s; continuing.").arg(a.typeName(), secs), true);
        return true;
    }
    setVar(QStringLiteral("found"), Value::number(0));
    if (a.stopIfMissing) {
        m_stopWhy = a.ms > 0 ? QStringLiteral("The picture of the %1 step wasn't found within %2 s.").arg(a.typeName(), secs)
                             : QStringLiteral("The picture of the %1 step wasn't on the screen.").arg(a.typeName());
        return false;
    }
    emit message(QStringLiteral("%1: the picture wasn't found; continuing (found = 0).").arg(a.typeName()), true);
    return true;
}

bool Player::saveScreenshot(const Action& a)
{
    const QRect area = a.area.isEmpty() ? m_inj->screenBounds() : a.area;
    const QImage shot = m_inj->grabScreen(area);
    if (shot.isNull()) {
        emit message(QStringLiteral("Save Screenshot: pictures of the screen can't be taken here; skipped."), true);
        return true;
    }
    QString path = sub(a.file).trimmed();
    if (path.isEmpty()) path = QStringLiteral("Screenshot %1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss")));
    if (QFileInfo(path).isRelative())
        path = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).absoluteFilePath(path);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1String("png") && suffix != QLatin1String("bmp")) path += QStringLiteral(".png");
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!shot.save(path)) {
        emit message(QStringLiteral("Save Screenshot: couldn't save “%1”.").arg(QDir::toNativeSeparators(path)), true);
        return true;
    }
    emit detail(QStringLiteral("Screenshot saved: %1").arg(QDir::toNativeSeparators(path)));
    setVar(QStringLiteral("screenshot"), Value::text(QDir::toNativeSeparators(path)));
    return true;
}

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
            m_pausedNs += p.nsecsElapsed();
            continue;
        }
        const double left = ms - double(t.nsecsElapsed()) / 1e6 + double(pausedFor);
        // Keys held down repeat like on a real keyboard (Windows doesn't do this for simulated keys).
        double nextRepeat = 1e18;
        if (!m_heldKeys.isEmpty()) {
            const double now = double(m_clock.nsecsElapsed()) / 1e6;
            for (auto it = m_heldKeys.begin(); it != m_heldKeys.end(); ++it) {
                if (now >= it.value()) {
                    m_inj->key(it.key(), true);
                    it.value() += m_repeatInterval;                              // a steady rhythm: lateness doesn't add up
                    if (it.value() < now) it.value() = now + m_repeatInterval;   // but never a burst after a long pause
                }
                nextRepeat = qMin(nextRepeat, it.value() - now);
            }
        }
        if (left <= 0) return true;
        if (nextRepeat < left && nextRepeat > 0) { QThread::usleep(qint64(nextRepeat * 1000)); continue; }
        if (left > 2) QThread::usleep(qMin<qint64>(qint64((left - 1) * 1000), 10000));
        else QThread::yieldCurrentThread();
    }
}

double Player::activeMs(const QElapsedTimer& t, qint64 pausedAtStart) const
{
    return double(t.nsecsElapsed() - (m_pausedNs - pausedAtStart)) / 1e6;
}

bool Player::waitPlanned(double ms)
{
    // Time lost earlier (input that took longer than recorded) comes off planned pauses, so it never adds up
    const double cut = qMin(ms, m_late);
    m_late -= cut;
    return waitMs(ms - cut);
}

QPoint Player::target(const Action& a)
{
    return a.relativeToWindow ? a.pos + m_inj->activeWindowOrigin() : a.pos;
}

bool Player::playAction(const Action& a, int index)
{
    Q_UNUSED(index)
    const double sp = m_o.speed;
    if (m_o.exactTiming && a.delayMs > 0 && !waitPlanned(a.delayMs / sp)) return false;

    auto moveInstant = [&](QPoint p) {
        if (m_inj->cursor() != p) m_inj->moveTo(p);
    };
    // How long the input itself takes is measured: sending a click or a key can cost real time (Windows takes
    // up to a timer tick per event when other programs watch the input), and over a macro that added up
    QElapsedTimer stepClock;
    stepClock.start();
    const qint64 stepPaused = m_pausedNs;

    switch (a.type) {
    case ActionType::MouseMove: {
        const QPoint to = target(a);
        const QPoint from = m_inj->cursor();
        QVector<PathPoint> recorded = m_o.path == PathStyle::AsRecorded ? a.path : QVector<PathPoint>();
        if (a.relativeToWindow && !recorded.isEmpty()) {   // the path was recorded relative to the window too
            const QPoint off = to - a.pos;
            for (PathPoint& p : recorded) { p.x += off.x(); p.y += off.y(); }
        }
        const auto pts = makePath(m_o.path, from, to, int(a.ms / sp), recorded, quint32(index * 7919 + 1));
        QElapsedTimer t;
        t.start();
        const qint64 paused0 = m_pausedNs;
        const double scale = (m_o.path == PathStyle::AsRecorded && a.path.size() >= 2) ? 1.0 / sp : 1.0;
        // Each point at its time on the real clock: moving the pointer takes a little time itself (more when another
        // program watches the mouse), and counting only the planned waits let that time add up over a macro.
        for (const PathPoint& p : pts) {
            const double due = p.t * scale, now = activeMs(t, paused0);
            if (due > now && !waitMs(due - now)) return false;
            m_inj->moveTo(QPoint(p.x, p.y));
        }
        // The move's recorded time includes any rest at the end (pointer arrived, then a pause before the click).
        const double total = a.ms / sp, now = activeMs(t, paused0);
        if (total > now) { if (!waitMs(total - now)) return false; }
        else m_late += now - total;
        break;
    }
    case ActionType::MouseClick:
        moveInstant(target(a));
        for (int i = 0; i < a.clicks; ++i) {
            if (i > 0 && !waitMs(a.gapMs > 0 ? a.gapMs / sp : kDoubleGapMs)) return false;
            m_inj->button(a.button, true);
            if (!waitMs(a.holdMs > 0 ? qMax(8.0, a.holdMs / sp) : kClickHoldMs)) { m_inj->button(a.button, false); return false; }
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
            if (i > 0 && !waitMs((a.gapMs > 0 ? a.gapMs : kWheelGapMs) / sp)) return false;
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
        static const QStringList kModifiers = {QStringLiteral("Shift"), QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("Win")};
        if (a.type == ActionType::KeyDown && ok && !kModifiers.contains(a.key))
            m_heldKeys.insert(a.key, double(m_clock.nsecsElapsed()) / 1e6 + m_repeatDelay);
        if (up) m_heldKeys.remove(a.key);
        if (down && up && !waitMs(a.holdMs > 0 ? qMax(8.0, a.holdMs / sp) : kKeyHoldMs)) { m_inj->key(a.key, false); return false; }
        if (up) ok = m_inj->key(a.key, false) && ok;
        if (up) for (auto it = a.mods.crbegin(); it != a.mods.crend(); ++it) m_inj->key(*it, false);
        if (!ok) emit message(QStringLiteral("Unknown key “%1” was skipped.").arg(a.key), true);
        break;
    }
    case ActionType::Text: {
        const QString text = sub(a.text);   // with variables filled in, the recorded rhythm no longer fits
        if (m_o.typingAsRecorded && a.strokes.size() > 0 && text == a.text) {
            QElapsedTimer t;
            t.start();
            const qint64 paused0 = m_pausedNs;
            for (const Stroke& s : a.strokes) {   // on the real clock, like a mouse path
                const double due = s.t / sp, now = activeMs(t, paused0);
                if (due > now && !waitMs(due - now)) return false;
                m_inj->typeText(s.ch);
            }
            const double late = activeMs(t, paused0) - a.strokes.last().t / sp;
            if (late > 0) m_late += late;
        } else {
            for (int i = 0; i < text.size(); ++i) {
                QString ch = text.mid(i, 1);
                if (text.at(i).isHighSurrogate() && i + 1 < text.size()) ch = text.mid(i++, 2);
                m_inj->typeText(ch);
                if (!waitMs(2)) return false;
            }
        }
        break;
    }
    case ActionType::Wait: {
        int ms = a.ms;
        if (a.maxMs > a.ms) ms = int(m_scope.random ? m_scope.random(a.ms, a.maxMs) : a.ms + QRandomGenerator::global()->bounded(a.maxMs - a.ms + 1));
        if (!waitPlanned(ms / sp)) return false;
        break;
    }
    case ActionType::WindowFocus: {
        QElapsedTimer t;
        t.start();
        bool loose = false;
        const QString exe = sub(a.exe), title = sub(a.title);
        while (true) {
            if (m_inj->focusWindow(exe, title, loose ? TitleMatch::Any : a.titleMatch)) {
                if (loose) emit message(QStringLiteral("No window titled “%1”; used another %2 window.").arg(title, exe), true);
                break;
            }
            if (!loose && a.titleMatch != TitleMatch::Any && t.elapsed() >= kWindowFallbackMs && !exe.isEmpty()) loose = true;
            if (a.ms > 0 && t.elapsed() >= a.ms) {
                m_stopWhy = QStringLiteral("Couldn't find the window “%1” (%2) within %3 s.")
                                .arg(title, exe).arg(a.ms / 1000.0, 0, 'f', 1);
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
        const QString program = sub(a.program);
        if (!QProcess::startDetached(program, QProcess::splitCommand(sub(a.arguments))))
            emit message(QStringLiteral("Couldn't start “%1”.").arg(program), true);
        break;
    }
    case ActionType::Notify: emit notify(sub(a.message)); break;
    case ActionType::MessageBox:
        m_boxOpen = true;
        emit messageBox(sub(a.message));
        while (m_boxOpen)
            if (!waitMs(20)) { m_boxOpen = false; return false; }
        break;
    case ActionType::AskText: {
        m_askOk = false;
        m_askOpen = true;
        emit askText(sub(a.message), sub(a.text));
        while (m_askOpen)
            if (!waitMs(20)) { m_askOpen = false; return false; }
        if (!m_askOk) {
            m_stopWhy = QStringLiteral("The question “%1” was cancelled.").arg(sub(a.message));
            return false;
        }
        QString answer;
        {
            QMutexLocker l(&m_mx);
            answer = m_answer;
        }
        setVar(a.var, Value::text(answer));
        break;
    }
    case ActionType::AskAI: {
        const QString prompt = sub(a.message);
        m_aiOk = false;
        m_aiOpen = true;
        emit askAI(prompt, a.ms > 0 ? a.ms : 60000);
        while (m_aiOpen)
            if (!waitMs(20)) { m_aiOpen = false; return false; }
        QString answer;
        {
            QMutexLocker l(&m_mx);
            answer = m_aiAnswer;
        }
        if (!m_aiOk) {
            m_stopWhy = QStringLiteral("Ask AI: %1").arg(answer);
            return false;
        }
        setVar(a.var, Value::text(answer));
        break;
    }
    case ActionType::SetVariable: {
        Value v;
        QString e;
        if (!expr::evaluate(a.expr, m_scope, &v, &e)) {
            emit message(QStringLiteral("Set Variable %1 = %2: %3 The variable wasn't changed.").arg(a.var, a.expr, e), true);
            break;
        }
        setVar(a.var, v);
        break;
    }
    case ActionType::SetClipboard: {
        const QString t = sub(a.text);
        setClipboardCache(t);
        emit clipboard(t);
        break;
    }
    case ActionType::WindowMove: {
        QElapsedTimer t;
        t.start();
        const QString exe = sub(a.exe), title = sub(a.title);
        while (!m_inj->moveWindow(exe, title, a.titleMatch, a.windowState, a.rect)) {
            if (t.elapsed() >= a.ms) {
                emit message(QStringLiteral("Move / Resize Window: no window matched %1%2; continuing.")
                                 .arg(exe.isEmpty() ? QStringLiteral("(any program)") : exe,
                                      title.isEmpty() || a.titleMatch == TitleMatch::Any ? QString() : QStringLiteral(" · “%1”").arg(title)), true);
                break;
            }
            if (!waitMs(kPollMs * 2)) return false;
        }
        if (!waitMs(60)) return false;   // let the window settle before input reaches it
        break;
    }
    case ActionType::FindImage: return findImage(a);
    case ActionType::Screenshot: return saveScreenshot(a);
    case ActionType::If:
    case ActionType::Else:
    case ActionType::EndIf:
    case ActionType::Group:
    case ActionType::EndGroup: break;   // handled by playList()
    case ActionType::Beep: emit beep(); break;
    case ActionType::WaitFile: {
        QElapsedTimer t;
        t.start();
        const QString file = sub(a.file);
        const QFileInfo start(file);
        const bool existed = start.exists();
        const QDateTime startTime = existed ? start.lastModified() : QDateTime();
        const qint64 startSize = existed ? start.size() : -1;
        auto met = [&] {
            const QFileInfo f(file);
            switch (a.fileWhen) {
            case FileCondition::Exists: return f.exists();
            case FileCondition::Gone: return !f.exists();
            case FileCondition::Changed:
                return f.exists() != existed || (f.exists() && (f.lastModified() != startTime || f.size() != startSize));
            }
            return true;
        };
        while (!met()) {
            if (a.ms > 0 && t.elapsed() >= a.ms) {
                const QString what = a.fileWhen == FileCondition::Exists ? QStringLiteral("appear")
                                     : a.fileWhen == FileCondition::Gone ? QStringLiteral("go away") : QStringLiteral("change");
                emit message(QStringLiteral("“%1” didn't %2 within %3 s; continuing.").arg(file, what).arg(a.ms / 1000.0, 0, 'f', 1), true);
                break;
            }
            if (!waitMs(100)) return false;
        }
        break;
    }
    case ActionType::RunMacro: {
        if (m_depth >= kMaxMacroDepth) {
            emit message(QStringLiteral("“Run macro” steps are nested more than %1 deep (a macro that runs itself?); skipped.")
                             .arg(kMaxMacroDepth), true);
            break;
        }
        Macro inner;
        QString err;
        const QString file = sub(a.file);
        if (!Macro::load(file, &inner, &err)) {
            emit message(QStringLiteral("Couldn't run the macro “%1”: %2").arg(file, err), true);
            break;
        }
        if (!err.isEmpty()) emit message(err, true);
        ++m_depth;
        const bool ok = playList(inner.actions, 0, int(inner.actions.size()) - 1, 1, 1, m_depth);
        --m_depth;
        if (!ok) return false;
        break;
    }
    case ActionType::Repeat:
    case ActionType::Goto: break;  // handled by playList()
    }
    // Clicks, keys and scrolling: the time beyond what they wait for themselves comes off the next planned pauses
    double planned = -1;
    switch (a.type) {
    case ActionType::MouseClick: {
        const double hold = a.holdMs > 0 ? qMax(8.0, a.holdMs / sp) : kClickHoldMs;
        const double gap = a.gapMs > 0 ? a.gapMs / sp : kDoubleGapMs;
        planned = a.clicks * hold + qMax(0, a.clicks - 1) * gap;
        break;
    }
    case ActionType::KeyPress: planned = a.holdMs > 0 ? qMax(8.0, a.holdMs / sp) : kKeyHoldMs; break;
    case ActionType::Wheel: planned = qMax(0, qAbs(a.notches) - 1) * (a.gapMs > 0 ? a.gapMs : kWheelGapMs) / sp; break;
    case ActionType::MouseDown: case ActionType::MouseUp: case ActionType::KeyDown: case ActionType::KeyUp: planned = 0; break;
    default: break;
    }
    if (planned >= 0) {
        const double excess = activeMs(stepClock, stepPaused) - planned;
        if (excess > 0) m_late += excess;
    }
    return true;
}

bool Player::playList(const QList<Action>& acts, int first, int last, int round, int rounds, int depth)
{
    QHash<QString, int> labels;
    for (int i = first; i <= last; ++i)
        if (!acts[i].label.isEmpty() && !labels.contains(acts[i].label)) labels.insert(acts[i].label, i);
    QHash<int, int> repeatsDone;          // Repeat action index -> jumps made this round
    QHash<int, qint64> repeatStarted;     // Repeat action index -> when its timed repeat began
    QElapsedTimer roundTimer;
    roundTimer.start();
    const Blocks b = blocks(acts, first, last);
    if (round == 1)   // a list's structure problems: once per playback, not once per round
        for (const QString& p : b.problems) emit message(p, true);
    int pc = first;
    while (pc <= last) {
        if (m_stop) return false;
        const Action& a = acts[pc];
        if (!a.enabled) {
            // A disabled If or Group skips everything inside it; a disabled Else or End If still ends its part.
            if (a.type == ActionType::If) { pc = b.endOf.value(pc, last) + 1; continue; }
            if (a.type == ActionType::Group) { pc = b.next.value(pc, last) + 1; continue; }
            if (a.type != ActionType::Else && a.type != ActionType::EndIf) { ++pc; continue; }
        }
        if (depth == 0) emit step(pc, round, rounds);   // steps of a macro run from this one aren't rows in the list
        if (a.type == ActionType::If) {
            pc = condition(a.expr, QStringLiteral("If")) ? pc + 1 : b.next.value(pc, last) + 1;   // false: the Else part
            if (!waitMs(0)) return false;   // honors pause; never spins without a stop check
            continue;
        }
        if (a.type == ActionType::Else) {   // reached at the end of the If part: skip the Else part
            pc = b.next.contains(pc) ? b.next.value(pc) + 1 : pc + 1;   // an Else without an If: nothing to skip
            continue;
        }
        if (a.type == ActionType::Goto && !a.expr.trimmed().isEmpty() && !condition(a.expr, QStringLiteral("Go To if"))) {
            ++pc;
            continue;
        }
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
            if (jump && !waitMs(1)) return false;  // never spin without yielding
            pc = jump ? to : pc + 1;
            continue;
        }
        if (!playAction(a, pc)) return false;
        switch (a.type) {   // steps that wait for something outside: their end is a fresh start for the timing
        case ActionType::WindowFocus: case ActionType::WaitPixel: case ActionType::WaitKey: case ActionType::WaitFile:
        case ActionType::FindImage: case ActionType::AskText: case ActionType::MessageBox: case ActionType::AskAI: m_late = 0; break;
        default: break;
        }
        const QString problem = m_inj->takeProblem();
        if (!problem.isEmpty() && !m_problemShown) { m_problemShown = true; emit message(problem, true); }
        ++pc;
    }
    return true;
}

void Player::run()
{
    m_inj->setPreciseTiming(true);
    m_problemShown = false;
    m_late = 0;
    m_pausedNs = 0;
    m_heldKeys.clear();
    m_clock.start();
    m_inj->keyRepeat(&m_repeatDelay, &m_repeatInterval);
    m_repeatDelay = qBound(100, m_repeatDelay, 2000);
    m_repeatInterval = qBound(10, m_repeatInterval, 1000);
    m_inj->takeProblem();   // forget anything from before this playback
    const QPoint startCursor = m_inj->cursor();
    const auto& acts = m_macro.actions;
    const int first = qBound(0, m_o.first, int(acts.size()));
    const int last = m_o.last < 0 ? int(acts.size()) - 1 : qMin(m_o.last, int(acts.size()) - 1);

    m_depth = 0;
    {
        QMutexLocker l(&m_mx);
        m_scope.vars.clear();   // every playback starts without variables
    }
    m_scope.clipboard = [this] { QMutexLocker l(&m_mx); return m_clipboard; };
    QElapsedTimer total;
    total.start();
    const int rounds = m_o.repeatMode == RepeatMode::Times ? qMax(1, m_o.repeatCount) : 0;
    bool stopped = false;
    for (int round = 1; !stopped; ++round) {
        if (m_o.repeatMode == RepeatMode::Times && round > rounds) break;
        if (m_o.repeatMode == RepeatMode::Seconds && round > 1 && total.elapsed() >= qint64(m_o.repeatCount) * 1000) break;
        {
            QMutexLocker l(&m_mx);
            m_scope.vars.insert(QStringLiteral("round"), Value::number(round));   // which time through the macro: 1, 2, ...
        }
        if (!playList(acts, first, last, round, rounds, 0)) stopped = true;
    }
    // Never leave buttons or modifier keys held down.
    for (int b : std::as_const(m_held)) m_inj->button(MouseButton(b), false);
    m_held.clear();
    for (auto it = m_heldKeys.cbegin(); it != m_heldKeys.cend(); ++it) m_inj->key(it.key(), false);   // never leave a key stuck
    m_heldKeys.clear();
    for (const QString& m : {QStringLiteral("Shift"), QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("Win")})
        if (m_inj->isKeyDown(m)) m_inj->key(m, false);
    if (m_o.restoreMouse && !m_stopWhy.size()) m_inj->moveTo(startCursor);
    m_inj->setPreciseTiming(false);
    const QString why = m_stopWhy;
    m_running = false;
    emit finished(stopped, why);
}

}  // namespace core
