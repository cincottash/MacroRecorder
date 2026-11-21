// Playback through /dev/uinput: a virtual keyboard and an absolute-position mouse that every Wayland compositor
// accepts like real hardware. Needs write access to /dev/uinput (see the README's Linux section).
#pragma once

#include "core/Input.h"

#include <QMutex>
#include <QRect>

#include <memory>

namespace platform {

class UinputInjector : public core::Injector {
public:
    static std::unique_ptr<UinputInjector> create(QString* whyNot);
    ~UinputInjector() override;

    QPoint cursor() override;
    void moveTo(QPoint p) override;
    void button(core::MouseButton b, bool down) override;
    void wheel(int notches, bool horizontal) override;
    bool key(const QString& name, bool down) override;
    void typeText(const QString& ch) override;
    bool focusWindow(const QString& exe, const QString& title, core::TitleMatch match) override;
    QPoint activeWindowOrigin() override;
    QString pixelColor(QPoint p) override;
    bool isKeyDown(const QString& name) override;
    QString takeProblem() override;

private:
    UinputInjector(int fd, QRect desktop);
    void send(int type, int code, int value);
    void sync();
    void problem(const QString& p);

    int m_fd;
    QRect m_desktop;
    QPoint m_last;
    QString m_problem;
    QMutex m_mx;
};

}  // namespace platform
