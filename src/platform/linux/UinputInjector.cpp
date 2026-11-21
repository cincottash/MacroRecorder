#include "UinputInjector.h"
#include "LinuxKeys.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

#include <cstring>

#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace platform {

std::unique_ptr<UinputInjector> UinputInjector::create(QString* whyNot)
{
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (whyNot) *whyNot = QStringLiteral("no access to /dev/uinput (%1)").arg(QString::fromLocal8Bit(strerror(errno)));
        return nullptr;
    }
    // The whole desktop in compositor coordinates: the absolute mouse maps its range onto it.
    QRect desk;
    for (QScreen* s : QGuiApplication::screens()) desk = desk.united(s->geometry());
    if (desk.isEmpty()) desk = QRect(0, 0, 1920, 1080);

    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
    for (int k = 1; k < 249; ++k) ok &= ioctl(fd, UI_SET_KEYBIT, k) == 0;                 // keyboard keys
    for (int b = BTN_LEFT; b <= BTN_TASK; ++b) ok &= ioctl(fd, UI_SET_KEYBIT, b) == 0;    // mouse buttons
    ok &= ioctl(fd, UI_SET_EVBIT, EV_REL) == 0 && ioctl(fd, UI_SET_RELBIT, REL_WHEEL) == 0 && ioctl(fd, UI_SET_RELBIT, REL_HWHEEL) == 0;
    ok &= ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0 && ioctl(fd, UI_SET_ABSBIT, ABS_X) == 0 && ioctl(fd, UI_SET_ABSBIT, ABS_Y) == 0;
    uinput_abs_setup ax{}, ay{};
    ax.code = ABS_X;
    ax.absinfo.maximum = desk.width() - 1;
    ay.code = ABS_Y;
    ay.absinfo.maximum = desk.height() - 1;
    ok &= ioctl(fd, UI_ABS_SETUP, &ax) == 0 && ioctl(fd, UI_ABS_SETUP, &ay) == 0;
    uinput_setup us{};
    us.id.bustype = BUS_VIRTUAL;
    us.id.vendor = 0x4d52;   // "MR"
    us.id.product = 0x0001;
    std::strncpy(us.name, "MacroRecorder virtual input", UINPUT_MAX_NAME_SIZE - 1);
    ok &= ioctl(fd, UI_DEV_SETUP, &us) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        if (whyNot) *whyNot = QStringLiteral("couldn't create the virtual input device (%1)").arg(QString::fromLocal8Bit(strerror(errno)));
        ::close(fd);
        return nullptr;
    }
    usleep(300000);   // give the compositor time to pick up the new device
    return std::unique_ptr<UinputInjector>(new UinputInjector(fd, desk));
}

UinputInjector::UinputInjector(int fd, QRect desktop) : m_fd(fd), m_desktop(desktop), m_last(QCursor::pos()) {}

UinputInjector::~UinputInjector()
{
    ioctl(m_fd, UI_DEV_DESTROY);
    ::close(m_fd);
}

void UinputInjector::send(int type, int code, int value)
{
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (::write(m_fd, &ev, sizeof ev) != sizeof ev) problem(QStringLiteral("The virtual input device stopped accepting input."));
}

void UinputInjector::sync() { send(EV_SYN, SYN_REPORT, 0); }

void UinputInjector::problem(const QString& p)
{
    if (m_problem.isEmpty()) m_problem = p;
}

QPoint UinputInjector::cursor()
{
    // Wayland doesn't tell programs where the pointer is; the last position we set is the best we know.
    QMutexLocker l(&m_mx);
    return m_last;
}

void UinputInjector::moveTo(QPoint p)
{
    QMutexLocker l(&m_mx);
    send(EV_ABS, ABS_X, qBound(0, p.x() - m_desktop.x(), m_desktop.width() - 1));
    send(EV_ABS, ABS_Y, qBound(0, p.y() - m_desktop.y(), m_desktop.height() - 1));
    sync();
    m_last = p;
}

void UinputInjector::button(core::MouseButton b, bool down)
{
    static const int kMap[] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA};
    QMutexLocker l(&m_mx);
    send(EV_KEY, kMap[int(b)], down ? 1 : 0);
    sync();
}

void UinputInjector::wheel(int notches, bool horizontal)
{
    QMutexLocker l(&m_mx);
    send(EV_REL, horizontal ? REL_HWHEEL : REL_WHEEL, notches);
    sync();
}

bool UinputInjector::key(const QString& name, bool down)
{
    const int code = lnx::evdevFromName(name == QLatin1String("AltGr") ? QStringLiteral("Alt") : name);
    if (!code) return false;
    QMutexLocker l(&m_mx);
    send(EV_KEY, code, down ? 1 : 0);
    sync();
    return true;
}

void UinputInjector::typeText(const QString& ch)
{
    // uinput sends physical keys; which character they type depends on the keyboard layout, which programs
    // aren't told on Wayland. The US layout is assumed for ASCII; other characters can't be typed this way.
    const QList<uint> cps = ch.toUcs4();
    int code = 0;
    bool shift = false;
    if (cps.size() != 1 || cps.first() > 0x7e || !lnx::evdevFromAscii(char(cps.first()), &code, &shift)) {
        QMutexLocker l(&m_mx);
        problem(QStringLiteral("On Wayland only plain (US keyboard) characters can be typed; “%1” was skipped.").arg(ch));
        return;
    }
    QMutexLocker l(&m_mx);
    if (shift) { send(EV_KEY, KEY_LEFTSHIFT, 1); sync(); }
    send(EV_KEY, code, 1);
    sync();
    send(EV_KEY, code, 0);
    sync();
    if (shift) { send(EV_KEY, KEY_LEFTSHIFT, 0); sync(); }
}

bool UinputInjector::focusWindow(const QString&, const QString&, core::TitleMatch)
{
    QMutexLocker l(&m_mx);
    problem(QStringLiteral("On Wayland, programs can't bring other windows to the front: “Window focus” steps were skipped. "
                           "Make sure the right window is in front before playing."));
    return true;
}

QPoint UinputInjector::activeWindowOrigin()
{
    QMutexLocker l(&m_mx);
    problem(QStringLiteral("On Wayland, window positions aren't available: positions “relative to the window” were used as screen positions."));
    return {};
}

QString UinputInjector::pixelColor(QPoint)
{
    QMutexLocker l(&m_mx);
    problem(QStringLiteral("On Wayland, programs can't read the screen: “Wait for pixel color” steps time out."));
    return {};
}

bool UinputInjector::isKeyDown(const QString&)
{
    QMutexLocker l(&m_mx);
    problem(QStringLiteral("On Wayland, “Wait for key” can't see key presses: those steps time out."));
    return false;
}

QString UinputInjector::takeProblem()
{
    QMutexLocker l(&m_mx);
    QString p = m_problem;
    m_problem.clear();
    return p;
}

}  // namespace platform
