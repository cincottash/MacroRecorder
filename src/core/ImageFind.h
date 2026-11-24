// Finds a small picture (a button, an icon) inside a screen capture, for the Find Image step.
#pragma once

#include "Input.h"

#include <QImage>
#include <QPoint>
#include <QVector>

namespace core {

struct ImageMatch {
    bool found = false;
    QPoint topLeft;      // in the haystack's pixels
    double score = 0;    // average difference per color channel, 0 (identical) to 255
};

// The best place where `needle` appears in `haystack`, allowing an average difference of `tolerancePct` percent per
// color channel (0 = identical pixels). Pictures are compared pixel for pixel: no scaling or rotation.
ImageMatch findImage(const QImage& haystack, const QImage& needle, int tolerancePct);

// Find Text: where `phrase` is among words read from the screen, in reading order (line by line, left to right).
// Case, spaces and line breaks don't matter ("Save As" matches the words "Save" + "As", "Total:" matches "Total" + ":").
// The box around the words that hold it; an empty rect if it's not there.
QRect findPhrase(const QVector<TextWord>& words, const QString& phrase);
QString joinText(const QVector<TextWord>& words);   // the text read: words with spaces, a line break between lines

}  // namespace core
