// Windows' own text recognition (Windows.Media.Ocr, Windows 10 and later): the words in a picture with their places.
#pragma once

#include "core/Input.h"

namespace platform::win {

// Any thread. Large pictures are read in pieces (the engine takes at most a few thousand pixels a side).
bool recognizeText(const QImage& img, QVector<core::TextWord>* words, QString* error);

}  // namespace platform::win
