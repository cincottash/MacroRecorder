#include "Paths.h"

#include <QRandomGenerator>

#include <functional>
#include <QtMath>

namespace core {

namespace {

constexpr int kStepMs = 8;  // about 120 points per second

double easeInOut(double f) { return f * f * (3 - 2 * f); }

QVector<PathPoint> sample(int ms, const std::function<QPointF(double)>& at)
{
    QVector<PathPoint> pts;
    const int n = qMax(1, ms / kStepMs);
    for (int i = 1; i <= n; ++i) {
        const double f = double(i) / n;
        const QPointF p = at(easeInOut(f));
        pts.append({qRound(p.x()), qRound(p.y()), int(ms * f)});
    }
    return pts;
}

// A human-like path: a simulated hand pulled toward the target with random "wind", slowing down near the end
// and sometimes overshooting a little, then retimed to the requested duration.
QVector<PathPoint> human(QPoint from, QPoint to, int ms, quint32 seed)
{
    QRandomGenerator rng(seed);
    const double gravity = 9, wind0 = 3, maxStep = 15, targetArea = 12;
    double x = from.x(), y = from.y(), vx = 0, vy = 0, wx = 0, wy = 0;
    QVector<QPointF> raw{QPointF(x, y)};
    for (int guard = 0; guard < 5000; ++guard) {
        const double d = qHypot(to.x() - x, to.y() - y);
        if (d < 1) break;
        const double wind = qMin(wind0, d);
        if (d >= targetArea) {
            wx = wx / qSqrt(3) + (rng.generateDouble() * 2 - 1) * wind / qSqrt(5);
            wy = wy / qSqrt(3) + (rng.generateDouble() * 2 - 1) * wind / qSqrt(5);
        } else {
            wx /= qSqrt(3);
            wy /= qSqrt(3);
        }
        vx += wx + gravity * (to.x() - x) / d;
        vy += wy + gravity * (to.y() - y) / d;
        const double v = qHypot(vx, vy);
        const double limit = d >= targetArea ? maxStep : qMax(1.0, maxStep * d / targetArea / 2);
        if (v > limit) {
            const double s = limit / 2 + rng.generateDouble() * limit / 2;
            vx = vx / v * s;
            vy = vy / v * s;
        }
        x += vx;
        y += vy;
        raw.append(QPointF(x, y));
    }
    raw.append(QPointF(to));
    // Retime: equal time per simulated step, then thin out to the playback step.
    QVector<PathPoint> pts;
    const int n = raw.size() - 1;
    for (int i = 1; i <= n; ++i) {
        const int t = int(qint64(ms) * i / n);
        if (!pts.isEmpty() && t - pts.last().t < kStepMs && i != n) continue;
        pts.append({qRound(raw[i].x()), qRound(raw[i].y()), t});
    }
    return pts;
}

}  // namespace

int defaultMoveMs(QPoint from, QPoint to)
{
    const double d = qHypot(to.x() - from.x(), to.y() - from.y());
    return qBound(120, int(150 + d * 0.5), 1200);
}

QVector<PathPoint> makePath(PathStyle style, QPoint from, QPoint to, int ms, const QVector<PathPoint>& recorded,
                            quint32 seed)
{
    if (ms <= 0) ms = defaultMoveMs(from, to);
    QVector<PathPoint> pts;
    switch (style) {
    case PathStyle::AsRecorded:
        if (recorded.size() >= 2) {
            // Shift the recorded path so it starts where the cursor really is, blending the offset out by the end.
            const QPoint off = from - QPoint(recorded.first().x, recorded.first().y);
            const int total = qMax(1, recorded.last().t);
            for (int i = 1; i < recorded.size(); ++i) {
                const double f = 1.0 - double(recorded[i].t) / total;
                pts.append({recorded[i].x + qRound(off.x() * f), recorded[i].y + qRound(off.y() * f), recorded[i].t});
            }
            break;
        }
        [[fallthrough]];
    case PathStyle::Linear:
        pts = sample(ms, [&](double f) { return QPointF(from) + (QPointF(to) - QPointF(from)) * f; });
        break;
    case PathStyle::Spline: {
        // A gentle curve: a cubic Bézier with control points pushed sideways (the side depends on the direction).
        const QPointF a(from), b(to), d = b - a;
        const QPointF side(-d.y() * 0.18, d.x() * 0.18);
        const QPointF c1 = a + d * 0.3 + side, c2 = a + d * 0.7 + side * 0.6;
        pts = sample(ms, [&](double f) {
            const double u = 1 - f;
            return a * (u * u * u) + c1 * (3 * u * u * f) + c2 * (3 * u * f * f) + b * (f * f * f);
        });
        break;
    }
    case PathStyle::HumanLike: pts = human(from, to, ms, seed); break;
    }
    if (pts.isEmpty() || pts.last().x != to.x() || pts.last().y != to.y()) pts.append({to.x(), to.y(), ms});
    return pts;
}

}  // namespace core
