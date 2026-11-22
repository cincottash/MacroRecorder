// A small animated picture of what a step does on the screen: a mouse move's path with a dot travelling along it at
// the recorded speed, or a click / scroll / pixel check at its spot. Shown in the list's empty right-hand side while
// the pointer is over a step.
#pragma once

#include "core/Action.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class ActionPreview : public QWidget {
    Q_OBJECT
public:
    explicit ActionPreview(QWidget* parent = nullptr);
    static bool canPreview(const core::Action& a);
    void showAction(const core::Action& a);   // starts the animation from the beginning
    void stop();
    const core::Action& action() const { return m_a; }
    int loopMs() const;                         // one round of the animation, including the pause at the end
    int heightForWidth(int w) const override;   // keeps the screens' shape

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QRect bounds() const;                       // the area drawn: around the step, or the window it's relative to
    static QRect desktop();                     // all screens, in physical pixels
    QPointF pointAt(int ms) const;              // where a move's pointer is after ms

    core::Action m_a;
    QVector<core::PathPoint> m_path;            // the move's path (a straight line if none was recorded)
    QElapsedTimer m_clock;
    QTimer m_tick;
};
