#include "Recorder.h"

#include <QtMath>

namespace core {

namespace {

constexpr int kClickSlopPx = 4;        // a press and release closer than this is a click, farther is a drag
constexpr int kWheelMergeMs = 400;     // wheel notches closer than this merge into one action
constexpr int kHoldMs = 600;           // a key held at least this long is recorded as down + up

int dist(QPoint a, QPoint b) { return qRound(qSqrt(qreal((a.x() - b.x()) * (a.x() - b.x()) + (a.y() - b.y()) * (a.y() - b.y())))); }

bool isTextBreakingKey(const QString& k)
{
    return k == QLatin1String("Enter") || k == QLatin1String("Tab") || k == QLatin1String("Backspace")
           || k == QLatin1String("Escape") || k == QLatin1String("Delete");
}

}  // namespace

void Recorder::start(const RecordOptions& o, qint64 t)
{
    m_o = o;
    m_active = true;
    m_lastEnd = t;
    m_moving = m_typing = m_hasLast = false;
    m_path.clear();
    m_down.clear();
    m_mods.clear();
    m_keys.clear();
    m_modUsed = false;
    m_window = 0;
    m_lastClickT = m_lastWheelT = 0;
    m_wheelRemainder = 0;
}

void Recorder::pauseBefore(Action& a, qint64 start)
{
    const qint64 gap = qMax<qint64>(0, start - m_lastEnd);
    if (gap >= m_o.shortestPauseMs) {
        Action w;
        w.type = ActionType::Wait;
        w.ms = int(gap);
        m_last = w;
        m_hasLast = true;
        emit actionAdded(w);
        a.delayMs = 0;
    } else {
        a.delayMs = int(gap);
    }
}

void Recorder::emitAction(Action a, qint64 start, qint64 end)
{
    pauseBefore(a, start);
    m_lastEnd = end;
    m_last = a;
    m_hasLast = true;
    emit actionAdded(a);
}

void Recorder::flushMove()
{
    if (!m_moving) return;
    m_moving = false;
    if (m_path.isEmpty() || m_path.last().x != m_moveFrom.x() || m_path.last().y != m_moveFrom.y()) {
        if (m_path.isEmpty()) return;
    }
    Action a;
    a.type = ActionType::MouseMove;
    a.from = m_moveFrom;
    a.pos = QPoint(m_path.last().x, m_path.last().y);
    if (a.pos == a.from) { m_path.clear(); return; }
    a.ms = int(m_moveLast - m_moveStart);
    a.path = m_path;
    m_path.clear();
    emitAction(a, m_moveStart, m_moveLast);
}

void Recorder::flushText()
{
    if (!m_typing) return;
    m_typing = false;
    emitAction(m_text, m_textStart, m_textLast);
}

void Recorder::checkWindow(const RawEvent& e)
{
    if (e.windowId == 0 || e.windowId == m_window) return;
    const bool first = m_window == 0;
    m_window = e.windowId;
    if (e.exe.isEmpty()) return;
    flushAll();
    Action a;
    a.type = ActionType::WindowFocus;
    a.exe = e.exe;
    a.title = e.title;
    a.titleMatch = TitleMatch::Exact;
    a.ms = 10000;  // wait up to 10 s for the window when playing
    if (first) {   // the window that was active when recording started: no pause before it
        m_last = a;
        m_hasLast = true;
        emit actionAdded(a);
    } else {
        emitAction(a, e.t, e.t);
    }
}

void Recorder::feed(const RawEvent& e)
{
    if (!m_active) return;
    switch (e.kind) {
    case RawEvent::Move: {
        // A button held while moving is a drag: emit the press before the path.
        for (auto it = m_down.begin(); it != m_down.end(); ++it) {
            if (!it->emitted && dist(e.pos, it->pos) > kClickSlopPx) {
                flushText();
                Action d;
                d.type = ActionType::MouseDown;
                d.button = MouseButton(it.key());
                d.pos = it->pos;
                emitAction(d, it->t, it->t);
                it->emitted = true;
            }
        }
        if (!m_o.mouseMoves) { m_cursor = e.pos; break; }
        if (!m_moving) {
            if (dist(e.pos, m_cursor) < m_o.minMovePx && !m_cursor.isNull()) break;
            flushText();
            m_moving = true;
            m_moveFrom = m_cursor.isNull() ? e.pos : m_cursor;
            m_moveStart = e.t;
            m_path = {{m_moveFrom.x(), m_moveFrom.y(), 0}};
        }
        const PathPoint& last = m_path.last();
        if (dist(e.pos, QPoint(last.x, last.y)) >= m_o.minMovePx) m_path.append({e.pos.x(), e.pos.y(), int(e.t - m_moveStart)});
        m_moveLast = e.t;
        m_cursor = e.pos;
        break;
    }
    case RawEvent::Down: {
        checkWindow(e);
        flushText();
        if (m_moving) {  // make sure the path ends exactly where the button goes down
            if (m_path.last().x != e.pos.x() || m_path.last().y != e.pos.y()) m_path.append({e.pos.x(), e.pos.y(), int(e.t - m_moveStart)});
            m_moveLast = e.t;
            flushMove();
        }
        // Modifiers held while clicking (Ctrl+click): press them first.
        for (const QString& m : std::as_const(m_mods)) {
            KeyHeld& k = m_keys[m];
            if (!k.emitted) {
                Action d;
                d.type = ActionType::KeyDown;
                d.key = m;
                emitAction(d, e.t, e.t);
                k.emitted = true;
                m_modUsed = true;
            }
        }
        m_down.insert(int(e.button), {e.pos, e.t, false});
        m_cursor = e.pos;
        break;
    }
    case RawEvent::Up: {
        auto it = m_down.find(int(e.button));
        if (it == m_down.end()) break;  // pressed before recording started
        const Held h = *it;
        m_down.erase(it);
        if (h.emitted) {  // end of a drag
            if (m_moving) {
                if (m_path.last().x != e.pos.x() || m_path.last().y != e.pos.y()) m_path.append({e.pos.x(), e.pos.y(), int(e.t - m_moveStart)});
                m_moveLast = e.t;
                flushMove();
            }
            Action u;
            u.type = ActionType::MouseUp;
            u.button = e.button;
            u.pos = e.pos;
            emitAction(u, e.t, e.t);
        } else if (m_hasLast && m_last.type == ActionType::MouseClick && m_last.clicks == 1 && m_last.button == e.button
                   && h.t - m_lastClickT <= m_o.doubleClickMs && dist(m_last.pos, h.pos) <= kClickSlopPx) {
            m_last.clicks = 2;
            m_lastEnd = e.t;
            emit lastActionChanged(m_last);
        } else {
            Action c;
            c.type = ActionType::MouseClick;
            c.button = e.button;
            c.pos = h.pos;
            emitAction(c, h.t, e.t);
            m_lastClickT = e.t;
        }
        m_cursor = e.pos;
        break;
    }
    case RawEvent::Wheel: {
        flushAll();
        m_wheelRemainder += e.wheelDelta;
        int notches = m_wheelRemainder / 120;
        if (notches == 0) break;  // high-resolution wheels send small steps; wait for a whole notch
        m_wheelRemainder -= notches * 120;
        if (m_hasLast && m_last.type == ActionType::Wheel && m_last.horizontal == e.horizontal
            && (m_last.notches > 0) == (notches > 0) && e.t - m_lastWheelT <= kWheelMergeMs && m_last.pos == e.pos) {
            m_last.notches += notches;
            m_lastEnd = e.t;
            emit lastActionChanged(m_last);
        } else {
            Action w;
            w.type = ActionType::Wheel;
            w.pos = e.pos;
            w.notches = notches;
            w.horizontal = e.horizontal;
            emitAction(w, e.t, e.t);
        }
        m_lastWheelT = e.t;
        break;
    }
    case RawEvent::KeyDown: {
        if (!m_o.keyboard) break;
        if (e.isModifier) {
            if (!m_mods.contains(e.key)) {
                m_mods.insert(e.key);
                m_keys.insert(e.key, {e.t, false});
                if (m_mods.size() == 1) m_modUsed = false;
            }
            break;
        }
        if (m_keys.contains(e.key)) break;  // auto-repeat while held
        checkWindow(e);
        const bool ctrl = m_mods.contains(QStringLiteral("Ctrl")), alt = m_mods.contains(QStringLiteral("Alt"));
        const bool win = m_mods.contains(QStringLiteral("Win"));
        const bool altGr = ctrl && alt;  // AltGr produces characters on many layouts
        const bool typed = !e.ch.isEmpty() && (altGr || (!ctrl && !alt)) && !win && !isTextBreakingKey(e.key);
        if (typed && m_o.mergeTyping) {
            flushMove();
            if (!m_typing) {
                m_typing = true;
                m_text = Action();
                m_text.type = ActionType::Text;
                m_textStart = e.t;
            }
            m_text.text += e.ch;
            m_text.strokes.append({e.ch, int(e.t - m_textStart)});
            m_textLast = e.t;
            m_keys.insert(e.key, {e.t, false});  // part of the text: never converted to down/up
            break;
        }
        flushAll();
        Action k;
        k.type = ActionType::KeyPress;
        k.key = e.key;
        for (const QString& m : {QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("Shift"), QStringLiteral("Win")})
            if (m_mods.contains(m) && !m_keys.value(m).emitted) k.mods << m;
        if (!k.mods.isEmpty()) m_modUsed = true;
        emitAction(k, e.t, e.t);
        m_keys.insert(e.key, {e.t, true});
        break;
    }
    case RawEvent::KeyUp: {
        if (!m_o.keyboard) break;
        if (e.isModifier) {
            if (!m_mods.contains(e.key)) break;
            const KeyHeld k = m_keys.take(e.key);
            m_mods.remove(e.key);
            if (k.emitted) {
                Action u;
                u.type = ActionType::KeyUp;
                u.key = e.key;
                flushAll();
                emitAction(u, e.t, e.t);
            } else if (!m_modUsed && m_mods.isEmpty() && !m_typing) {
                Action p;  // a modifier pressed on its own
                p.type = ActionType::KeyPress;
                p.key = e.key;
                flushAll();
                emitAction(p, k.t, e.t);
            }
            break;
        }
        const KeyHeld k = m_keys.take(e.key);
        if (k.emitted && e.t - k.t >= kHoldMs && m_hasLast && m_last.type == ActionType::KeyPress && m_last.key == e.key) {
            m_last.type = ActionType::KeyDown;  // held: record as down ... up
            emit lastActionChanged(m_last);
            Action u;
            u.type = ActionType::KeyUp;
            u.key = e.key;
            emitAction(u, e.t, e.t);
        }
        break;
    }
    }
}

void Recorder::finish()
{
    if (!m_active) return;
    flushAll();
    m_active = false;
}

}  // namespace core
