#include "ScreenPicker.h"

#include "core/Input.h"

#include <QApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>

namespace screenpicker {

namespace {

struct Shared {
    QEventLoop loop;
    QRect result;          // screen pixels
    bool done = false;
};

// One per screen: the still picture of that screen, darkened except inside the box being dragged.
class Overlay : public QWidget {
public:
    Overlay(QScreen* s, const QImage& picture, const QString& hint, Shared* st)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), m_screen(s), m_pic(picture), m_hint(hint), m_st(st)
    {
        setProperty("solidOverlay", true);   // Window Opacity leaves it alone
        setAttribute(Qt::WA_ShowWithoutActivating, false);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true);
        setGeometry(s->geometry());
    }
    // Screen pixels of a point in this window: steps use the system's pixels, which differ on scaled screens
    QPoint toScreen(QPointF local) const
    {
        const qreal dpr = m_screen->devicePixelRatio();
        return m_screen->geometry().topLeft() + QPoint(qRound(local.x() * dpr), qRound(local.y() * dpr));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.drawImage(rect(), m_pic);
        const QRect box = QRect(m_from, m_to).normalized();
        QPainterPath dim;
        dim.addRect(rect());
        if (m_dragging) {
            QPainterPath hole;
            hole.addRect(box);
            dim = dim.subtracted(hole);
        }
        p.fillPath(dim, QColor(0, 0, 0, 110));
        const QColor accent(0x3B, 0x82, 0xF6);
        if (m_dragging) {
            p.setPen(QPen(Qt::white, 1));
            p.drawRect(box.adjusted(-1, -1, 0, 0));
            p.setPen(QPen(accent, 1, Qt::DashLine));
            p.drawRect(box.adjusted(-1, -1, 0, 0));
            const QRect phys = QRect(toScreen(box.topLeft()), toScreen(box.bottomRight() + QPoint(1, 1))).normalized();
            label(p, QStringLiteral("%1 × %2 px").arg(phys.width()).arg(phys.height()), box.bottomLeft() + QPoint(0, 8));
        }
        // The hint, at the top of the screen
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.15);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QString text = m_hint + QStringLiteral("   ·   Esc: Cancel");
        const QRect r(QPoint((width() - fm.horizontalAdvance(text)) / 2 - 16, 24), QSize(fm.horizontalAdvance(text) + 32, fm.height() + 16));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 24, 225));
        p.drawRoundedRect(r, 8, 8);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, text);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::RightButton) { finish(false); return; }
        if (e->button() != Qt::LeftButton) return;
        m_dragging = true;
        m_from = m_to = e->position().toPoint();
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_dragging) return;
        m_to = e->position().toPoint();
        update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (!m_dragging || e->button() != Qt::LeftButton) return;
        m_to = e->position().toPoint();
        const QRect box = QRect(m_from, m_to).normalized();
        if (box.width() < 3 || box.height() < 3) { m_dragging = false; update(); return; }   // a click, not a box
        m_st->result = QRect(toScreen(box.topLeft()), toScreen(box.bottomRight() + QPoint(1, 1))).normalized().adjusted(0, 0, -1, -1);
        finish(true);
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) finish(false);
    }

private:
    void label(QPainter& p, const QString& text, QPoint at)
    {
        const QFontMetrics fm(font());
        QRect r(at, QSize(fm.horizontalAdvance(text) + 12, fm.height() + 6));
        if (r.bottom() > height() - 4) r.moveBottom(at.y() - 16 - QRect(m_from, m_to).normalized().height());
        p.fillRect(r, QColor(20, 20, 24, 225));
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, text);
    }
    void finish(bool ok)
    {
        m_st->done = ok;
        m_st->loop.quit();
    }

    QScreen* m_screen;
    QImage m_pic;
    QString m_hint;
    Shared* m_st;
    bool m_dragging = false;
    QPoint m_from, m_to;
};

}  // namespace

bool pick(core::Injector* inj, const QString& hint, QRect* area, QImage* picture, QString* error)
{
    if (forTests()) return forTests()(area, picture);
    if (!inj) return false;
    // The app's own windows step out of the way (invisible, not hidden: hiding a dialog would close it)
    QList<QPair<QPointer<QWidget>, double>> faded;
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w->isVisible() && !w->isMinimized()) { faded.append({w, w->windowOpacity()}); w->setWindowOpacity(0.0); }
    QEventLoop settle;
    QTimer::singleShot(250, &settle, &QEventLoop::quit);   // the system needs a moment to redraw what was behind them
    settle.exec();
    const QRect bounds = inj->screenBounds();
    const QImage full = inj->grabScreen(bounds);
    auto restore = [&] {
        for (auto& [w, o] : faded)
            if (w) w->setWindowOpacity(o);
    };
    if (full.isNull() || bounds.isEmpty()) {
        restore();
        if (error) *error = QStringLiteral("Pictures of the screen can't be taken here.");
        return false;
    }
    Shared st;
    QList<Overlay*> overlays;
    for (QScreen* s : QGuiApplication::screens()) {
        const QRect g = s->geometry();
        const QRect phys(g.topLeft(), (QSizeF(g.size()) * s->devicePixelRatio()).toSize());
        auto* o = new Overlay(s, full.copy(phys.translated(-bounds.topLeft())), hint, &st);
        overlays << o;
        o->show();
    }
    if (!overlays.isEmpty()) {
        overlays.first()->activateWindow();
        overlays.first()->raise();
    }
    st.loop.exec();
    qDeleteAll(overlays);
    restore();
    if (!st.done) return false;
    if (area) *area = st.result;
    if (picture) *picture = full.copy(st.result.translated(-bounds.topLeft()));
    return true;
}

}  // namespace screenpicker
