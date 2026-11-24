// Choosing a part of the screen with the mouse (the Find Image step's picture, an area to look in or to save):
// a still picture of every screen is shown full screen; dragging a box chooses that part, Esc cancels.
#pragma once

#include <QImage>
#include <QRect>

#include <functional>

namespace core { class Injector; }
class QWidget;

namespace screenpicker {

// `area`: the chosen part in screen pixels (the coordinates steps use); `picture`: that part of the screen.
// False if cancelled or if this system can't take pictures of the screen (`error` says which).
bool pick(core::Injector* inj, const QString& hint, QRect* area, QImage* picture, QString* error = nullptr);

// Tests: answer instead of showing anything on the screen.
inline std::function<bool(QRect* area, QImage* picture)>& forTests()
{
    static std::function<bool(QRect*, QImage*)> f;
    return f;
}

}  // namespace screenpicker
