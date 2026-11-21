// Turns the stream of raw input events into macro actions while recording: merges mouse moves into paths,
// typing into text, detects clicks / double-clicks / drags, inserts waits and window changes.
#pragma once

#include "Input.h"

#include <QObject>
#include <QHash>
#include <QSet>

namespace core {

struct RecordOptions {
    bool mouseMoves = true;
    int minMovePx = 5;          // ignore movements smaller than this (path points)
    bool keyboard = true;
    bool mergeTyping = true;    // typed characters -> one Text action
    int shortestPauseMs = 500;  // pauses at least this long become Wait actions; shorter ones are kept as delays
    int doubleClickMs = 500;
};

class Recorder : public QObject {
    Q_OBJECT
public:
    explicit Recorder(QObject* parent = nullptr) : QObject(parent) {}
    void start(const RecordOptions& o, qint64 t);
    void feed(const RawEvent& e);
    void finish();            // flush anything pending
    bool active() const { return m_active; }

signals:
    void actionAdded(const core::Action& a);
    void lastActionChanged(const core::Action& a);   // e.g. a click that became a double-click

private:
    void emitAction(Action a, qint64 start, qint64 end);
    void flushMove();
    void flushText();
    void flushAll() { flushMove(); flushText(); }
    void checkWindow(const RawEvent& e);
    void pauseBefore(Action& a, qint64 start);

    RecordOptions m_o;
    bool m_active = false;
    qint64 m_lastEnd = 0;         // when the previous action finished
    QPoint m_cursor;              // last known cursor position

    // pending mouse move
    bool m_moving = false;
    qint64 m_moveStart = 0, m_moveLast = 0;
    QPoint m_moveFrom;
    QVector<PathPoint> m_path;

    // pending text
    bool m_typing = false;
    Action m_text;
    qint64 m_textStart = 0, m_textLast = 0;

    // buttons held: button -> (press position, time, already emitted as MouseDown)
    struct Held { QPoint pos; qint64 t = 0; bool emitted = false; };
    QHash<int, Held> m_down;

    // last emitted action (for double-click / wheel merging)
    Action m_last;
    bool m_hasLast = false;
    qint64 m_lastClickT = 0;
    qint64 m_lastWheelT = 0, m_wheelStartT = 0;
    int m_wheelRemainder = 0;

    // keyboard
    QSet<QString> m_mods;                     // modifiers currently held
    bool m_modUsed = false;                   // a modifier was combined with another key
    struct KeyHeld { qint64 t = 0; bool emitted = false; };
    QHash<QString, KeyHeld> m_keys;

    quint64 m_window = 0;
};

}  // namespace core
