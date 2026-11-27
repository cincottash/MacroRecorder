#include "ActionPreview.h"

#include "Look.h"
#include "core/Tr.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <cmath>

using core::Action;
using core::ActionType;

namespace {
constexpr int kRestMs = 700;      // pause at the end of each round
constexpr int kPulseMs = 450;     // one click ring
constexpr int kMinMoveMs = 350;   // moves recorded "instantly" still animate visibly
}  // namespace

ActionPreview::ActionPreview(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);   // the list underneath keeps getting the pointer
    setProperty("testId", QStringLiteral("preview"));
    m_tick.setInterval(16);
    connect(&m_tick, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    hide();
}

bool ActionPreview::canPreview(const Action& a)
{
    switch (a.type) {
    case ActionType::MouseMove: case ActionType::MouseClick: case ActionType::MouseDown: case ActionType::MouseUp:
    case ActionType::Wheel: case ActionType::WaitPixel: return true;
    default: return false;
    }
}

void ActionPreview::showAction(const Action& a)
{
    m_a = a;
    m_path = a.path;
    if (a.type == ActionType::MouseMove && m_path.size() < 2)
        m_path = {{a.from.x(), a.from.y(), 0}, {a.pos.x(), a.pos.y(), qMax(a.ms, kMinMoveMs)}};
    if (a.type == ActionType::MouseMove && m_path.last().t < kMinMoveMs) {   // stretch very quick moves so they can be seen
        const double k = double(kMinMoveMs) / qMax(1, m_path.last().t);
        for (core::PathPoint& p : m_path) p.t = int(p.t * k);
    }
    m_clock.restart();
    m_tick.start();
    show();
    raise();
    update();
}

void ActionPreview::stop()
{
    m_tick.stop();
    hide();
}

int ActionPreview::loopMs() const
{
    if (m_a.type == ActionType::MouseMove) return m_path.last().t + kRestMs;
    return (m_a.type == ActionType::MouseClick ? m_a.clicks : 1) * kPulseMs + kRestMs;
}

int ActionPreview::heightForWidth(int w) const
{
    const QRect b = bounds();
    const int fh = fontMetrics().height();
    const int head = fh + 6, foot = fh + 4, pad = 10;
    const int mapW = w - 2 * pad;
    const int mapH = qBound(mapW / 4, int(mapW * double(b.height()) / qMax(1, b.width())), mapW * 3 / 4);
    return pad * 2 + head + foot + mapH;
}

QRect ActionPreview::desktop()
{
    // Screens in physical pixels, as step positions are: Qt reports each screen's corner as is and its size scaled.
    QRect r;
    for (QScreen* s : QGuiApplication::screens()) r |= QRect(s->geometry().topLeft(), s->geometry().size() * s->devicePixelRatio());
    return r;
}

QRect ActionPreview::bounds() const
{
    QRect pts;
    auto add = [&](QPoint p) { pts |= QRect(p, QSize(1, 1)); };
    add(m_a.pos);
    if (m_a.type == ActionType::MouseMove) for (const core::PathPoint& p : m_path) add({p.x, p.y});
    if (m_a.relativeToWindow) {   // the window's area: from its corner to a bit past the furthest point
        pts |= QRect(0, 0, 1, 1);
        return pts.adjusted(0, 0, qMax(80, pts.width() / 4), qMax(60, pts.height() / 4));
    }
    // Zoomed in on the step, with room around it (at least 640 x 400 screen pixels), kept on the desktop.
    const int room = qMax(80, qMax(pts.width(), pts.height()) / 4);
    QRect r = pts.adjusted(-room, -room, room, room);
    if (r.width() < 640) r.adjust(-(640 - r.width()) / 2, 0, (640 - r.width() + 1) / 2, 0);
    if (r.height() < 400) r.adjust(0, -(400 - r.height()) / 2, 0, (400 - r.height() + 1) / 2);
    const QRect d = desktop();
    if (!d.isEmpty() && d.contains(pts)) {
        if (r.width() >= d.width()) { r.setLeft(d.left()); r.setRight(d.right()); }
        else if (r.left() < d.left()) r.moveLeft(d.left());
        else if (r.right() > d.right()) r.moveRight(d.right());
        if (r.height() >= d.height()) { r.setTop(d.top()); r.setBottom(d.bottom()); }
        else if (r.top() < d.top()) r.moveTop(d.top());
        else if (r.bottom() > d.bottom()) r.moveBottom(d.bottom());
    }
    return r;
}

QPointF ActionPreview::pointAt(int ms) const
{
    if (m_path.isEmpty()) return m_a.pos;
    if (ms <= m_path.first().t) return QPointF(m_path.first().x, m_path.first().y);
    for (int i = 1; i < m_path.size(); ++i) {
        const core::PathPoint& a = m_path[i - 1];
        const core::PathPoint& b = m_path[i];
        if (ms <= b.t) {
            const double f = b.t > a.t ? double(ms - a.t) / (b.t - a.t) : 1.0;
            return QPointF(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f);
        }
    }
    return QPointF(m_path.last().x, m_path.last().y);
}

void ActionPreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    QColor card = pal.color(QPalette::Button);
    card.setAlphaF(look::values().previewPanel);   // 1: solid; lower lets the list show through
    const QColor border = pal.color(QPalette::Mid), text = pal.color(QPalette::Text);
    const QColor muted = pal.color(QPalette::PlaceholderText), accent = pal.color(QPalette::Highlight);

    // card
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(border, 1));
    p.setBrush(card);
    p.drawRoundedRect(frame, 8, 8);
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(text);
    const int pad = 10, head = QFontMetrics(f).height() + 6;
    p.drawText(QRect(pad, pad - 2, width() - 2 * pad, head), Qt::AlignLeft | Qt::AlignVCenter,
               m_a.relativeToWindow ? T("%1 (in Window)").arg(m_a.typeName()) : m_a.typeName());
    p.setFont(font());
    p.setPen(muted);
    const int foot = QFontMetrics(font()).height() + 4;
    p.drawText(QRect(pad, height() - foot - pad + 4, width() - 2 * pad, foot), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(font()).elidedText(m_a.valueText(), Qt::ElideRight, width() - 2 * pad));

    // the map: the screens (or the window) scaled to fit
    // (inset a little, so markers at the very edge of a screen stay inside the card)
    const QRectF area = QRectF(pad, pad + head, width() - 2 * pad, height() - 2 * pad - head - foot).adjusted(8, 6, -8, -6);
    const QRect b = bounds();
    const double scale = qMin(area.width() / qMax(1, b.width()), area.height() / qMax(1, b.height()));
    const QPointF offset(area.left() + (area.width() - b.width() * scale) / 2 - b.left() * scale,
                         area.top() + (area.height() - b.height() * scale) / 2 - b.top() * scale);
    auto map = [&](QPointF q) { return QPointF(q.x() * scale, q.y() * scale) + offset; };
    QColor screenFill = border;
    screenFill.setAlpha(60);
    p.setPen(QPen(border, 1));
    p.setBrush(screenFill);
    p.setClipRect(area.adjusted(-6, -4, 6, 4));
    const QRect d = desktop();
    if (m_a.relativeToWindow) {
        p.drawRoundedRect(QRectF(map(QPointF(0, 0)), map(QPointF(b.right(), b.bottom()))), 4, 4);
    } else {
        for (QScreen* s : QGuiApplication::screens()) {
            const QRect r(s->geometry().topLeft(), s->geometry().size() * s->devicePixelRatio());
            p.drawRoundedRect(QRectF(map(r.topLeft()), map(QPointF(r.right() + 1, r.bottom() + 1))), 3, 3);
        }
        // Zoomed in: a small map of the whole desktop in the corner, showing where this is.
        const QRectF seen((area.left() - offset.x()) / scale, (area.top() - offset.y()) / scale, area.width() / scale, area.height() / scale);
        if (!d.isEmpty() && (seen.width() < d.width() * 0.9 || seen.height() < d.height() * 0.9)) {
            p.setClipping(false);
            const double ow = 76, oh = qBound(22.0, ow * d.height() / qMax(1, d.width()), 56.0);
            const QRectF box(area.right() - ow, area.bottom() - oh, ow, oh);
            const double k = qMin(ow / d.width(), oh / d.height());
            auto mini = [&](QPointF q) { return box.topLeft() + QPointF((q.x() - d.left()) * k, (q.y() - d.top()) * k); };
            p.setPen(QPen(border, 1));
            p.setBrush(card);
            p.drawRect(box.adjusted(-3, -3, 3, 3));
            p.setPen(QPen(muted, 1));
            p.setBrush(screenFill);
            for (QScreen* s : QGuiApplication::screens()) {
                const QRect r(s->geometry().topLeft(), s->geometry().size() * s->devicePixelRatio());
                p.drawRect(QRectF(mini(r.topLeft()), mini(QPointF(r.right() + 1, r.bottom() + 1))).adjusted(0.5, 0.5, -0.5, -0.5));
            }
            p.setPen(QPen(accent, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(mini(seen.topLeft()), mini(seen.bottomRight())).intersected(box.adjusted(-1, -1, 1, 1)));
            p.setClipRegion(QRegion(area.adjusted(-6, -4, 6, 4).toRect()) - QRegion(box.adjusted(-4, -4, 4, 4).toAlignedRect()));
        }
    }

    const int t = m_clock.isValid() ? int(m_clock.elapsed() % qMax(1, loopMs())) : 0;
    auto crosshair = [&](QPointF c, const QColor& col) {
        p.setPen(QPen(col, 1.5));
        p.drawLine(c + QPointF(-7, 0), c + QPointF(-3, 0));
        p.drawLine(c + QPointF(3, 0), c + QPointF(7, 0));
        p.drawLine(c + QPointF(0, -7), c + QPointF(0, -3));
        p.drawLine(c + QPointF(0, 3), c + QPointF(0, 7));
    };
    auto dot = [&](QPointF c, double r, const QColor& col) {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(c, r, r);
    };

    if (m_a.type == ActionType::MouseMove) {
        QPainterPath all, done;
        all.moveTo(map(QPointF(m_path.first().x, m_path.first().y)));
        done.moveTo(all.currentPosition());
        for (int i = 1; i < m_path.size(); ++i) {
            const QPointF q = map(QPointF(m_path[i].x, m_path[i].y));
            all.lineTo(q);
            if (m_path[i].t <= t) done.lineTo(q);
        }
        const QPointF now = map(pointAt(t));
        done.lineTo(now);
        QColor faint = accent;
        faint.setAlpha(80);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(faint, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(all);
        p.setPen(QPen(accent, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(done);
        p.setPen(QPen(muted, 1.5));
        p.setBrush(card);
        p.drawEllipse(all.pointAtPercent(0), 3.5, 3.5);   // start
        crosshair(map(QPointF(m_path.last().x, m_path.last().y)), text);
        dot(now, 4.5, accent);
        return;
    }

    const QPointF c = map(m_a.pos);
    crosshair(c, text);
    if (m_a.type == ActionType::WaitPixel) {
        p.setPen(QPen(text, 1));
        p.setBrush(QColor(m_a.color));
        p.drawRect(QRectF(c + QPointF(9, -18), QSizeF(14, 14)));
        return;
    }
    if (m_a.type == ActionType::Wheel) {   // arrows sliding the way it scrolls
        const bool up = m_a.notches > 0;
        const double phase = (t % 600) / 600.0;
        p.setPen(QPen(accent, 2, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 2; ++i) {
            const double d = (phase + i * 0.5) - std::floor(phase + i * 0.5);
            const QPointF a = m_a.horizontal ? c + QPointF((up ? 1 : -1) * (6 + d * 14), 0) : c + QPointF(0, (up ? -1 : 1) * (6 + d * 14));
            const QPointF w1 = m_a.horizontal ? QPointF(up ? -4 : 4, -4) : QPointF(-4, up ? 4 : -4);
            const QPointF w2 = m_a.horizontal ? QPointF(up ? -4 : 4, 4) : QPointF(4, up ? 4 : -4);
            p.drawLine(a, a + w1);
            p.drawLine(a, a + w2);
        }
        return;
    }
    // clicks (one ring per click), presses and releases
    const int pulses = m_a.type == ActionType::MouseClick ? m_a.clicks : 1;
    const int k = t / kPulseMs;
    if (k < pulses) {
        const double f = double(t % kPulseMs) / kPulseMs;
        QColor ring = accent;
        ring.setAlphaF(1.0 - f);
        p.setPen(QPen(ring, 2));
        p.setBrush(Qt::NoBrush);
        const double r = m_a.type == ActionType::MouseUp ? 16 - f * 12 : 4 + f * 12;   // releases close in
        p.drawEllipse(c, r, r);
    }
    dot(c, m_a.type == ActionType::MouseDown ? 4.5 : 3, accent);
}
