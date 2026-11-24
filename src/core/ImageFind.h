// Finds a small picture (a button, an icon) inside a screen capture, for the Find Image step.
#pragma once

#include <QImage>
#include <QPoint>

namespace core {

struct ImageMatch {
    bool found = false;
    QPoint topLeft;      // in the haystack's pixels
    double score = 0;    // average difference per color channel, 0 (identical) to 255
};

// The best place where `needle` appears in `haystack`, allowing an average difference of `tolerancePct` percent per
// color channel (0 = identical pixels). Pictures are compared pixel for pixel: no scaling or rotation.
ImageMatch findImage(const QImage& haystack, const QImage& needle, int tolerancePct);

}  // namespace core
