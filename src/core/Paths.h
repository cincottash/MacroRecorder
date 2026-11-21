// Mouse paths for playback: the recorded path, or one generated between the start and end point.
#pragma once

#include "Action.h"

namespace core {

enum class PathStyle { AsRecorded, Linear, Spline, HumanLike };

// Points (with times in ms from the start) from `from` to `to`, lasting about `ms`. The last point is exactly `to`.
QVector<PathPoint> makePath(PathStyle style, QPoint from, QPoint to, int ms, const QVector<PathPoint>& recorded,
                            quint32 seed);

int defaultMoveMs(QPoint from, QPoint to);  // for moves added by hand (no recorded duration)

}  // namespace core
