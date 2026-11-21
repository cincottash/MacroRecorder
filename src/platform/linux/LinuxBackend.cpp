// Linux: picks the input method for this session when the app starts.
//   X11 session                    -> X11 backend (record and play in every program)
//   Wayland + /dev/uinput access   -> play in every program through uinput; record/hotkeys via XWayland if present
//   Wayland without uinput access  -> XWayland programs only (if XWayland is running)
#include "platform/Backend.h"
#include "UinputInjector.h"
#include "X11Backend.h"

#include <QProcessEnvironment>

#include <chrono>

#include <X11/Xlib.h>

namespace platform {

void Backend::earlyInit() { XInitThreads(); }   // before any other Xlib call; several threads use Xlib

qint64 Backend::now()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

namespace {

const QString kWaylandHelp = QStringLiteral(
    "For recording on Wayland, choose an “X11” or “Xorg” session at the login screen. For playback into every "
    "program, give the app access to /dev/uinput (see the README, “Linux setup”).");

class NoBackend : public Backend {
public:
    NoBackend(QObject* parent, QString why) : Backend(parent), m_why(std::move(why)) { qRegisterMetaType<core::RawEvent>(); }
    QString name() const override { return QStringLiteral("none"); }
    QString unavailableReason() const override { return m_why; }
    void setCapture(CaptureMode) override {}
    core::Injector* injector() override { return &m_inj; }
    bool setHotkey(int, const QKeySequence& seq, QString* error) override
    {
        if (!seq.isEmpty() && error) *error = QStringLiteral("Global hotkeys aren't available here.");
        return seq.isEmpty();
    }

private:
    class Null : public core::Injector {
    public:
        QPoint cursor() override { return {}; }
        void moveTo(QPoint) override {}
        void button(core::MouseButton, bool) override {}
        void wheel(int, bool) override {}
        bool key(const QString&, bool) override { return false; }
        void typeText(const QString&) override {}
        bool focusWindow(const QString&, const QString&, core::TitleMatch) override { return false; }
        QPoint activeWindowOrigin() override { return {}; }
        QString pixelColor(QPoint) override { return {}; }
        bool isKeyDown(const QString&) override { return false; }
    } m_inj;
    QString m_why;
};

// Wayland without XWayland: playback through uinput only.
class UinputOnlyBackend : public Backend {
public:
    UinputOnlyBackend(QObject* parent, std::unique_ptr<UinputInjector> inj) : Backend(parent), m_inj(std::move(inj))
    {
        qRegisterMetaType<core::RawEvent>();
    }
    QString name() const override { return QStringLiteral("Wayland (uinput)"); }
    QString recordUnavailableReason() const override
    {
        return QStringLiteral("Recording isn't possible on this Wayland session (no XWayland). ") + kWaylandHelp;
    }
    QString notes() const override
    {
        return QStringLiteral("Wayland: playback works in every program, but hotkeys and “stop if I move the mouse” aren't available. "
                              "Use the Stop button to stop.");
    }
    void setCapture(CaptureMode) override {}
    core::Injector* injector() override { return m_inj.get(); }
    bool setHotkey(int, const QKeySequence& seq, QString* error) override
    {
        if (!seq.isEmpty() && error) *error = QStringLiteral("Global hotkeys aren't available on Wayland without XWayland.");
        return seq.isEmpty();
    }

private:
    std::unique_ptr<UinputInjector> m_inj;
};

}  // namespace

Backend* createLinuxBackend(QObject* parent)
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString session = env.value(QStringLiteral("XDG_SESSION_TYPE")).toLower();
    const bool hasX = !env.value(QStringLiteral("DISPLAY")).isEmpty();
    const bool wayland = session == QLatin1String("wayland") || (session.isEmpty() && !env.value(QStringLiteral("WAYLAND_DISPLAY")).isEmpty());

    if (!wayland) {
        if (hasX) return new X11Backend(parent);
        return new NoBackend(parent, QStringLiteral("No graphical session was found (neither X11 nor Wayland)."));
    }

    QString uinputWhy;
    std::unique_ptr<UinputInjector> uin = UinputInjector::create(&uinputWhy);
    if (!hasX) {
        if (uin) return new UinputOnlyBackend(parent, std::move(uin));
        return new NoBackend(parent, QStringLiteral("This Wayland session has no XWayland, and the app has %1. ").arg(uinputWhy) + kWaylandHelp);
    }
    auto* b = new X11Backend(parent, uin.get());
    if (uin) {
        b->setDisplayName(QStringLiteral("Wayland (uinput + XWayland)"));
        b->setNotes(QStringLiteral("Wayland: playback works in every program (plain US-keyboard characters only). Recording, "
                                   "hotkeys and “stop if I move the mouse” only see programs running through XWayland. ") + kWaylandHelp);
        UinputInjector* raw = uin.release();
        QObject::connect(b, &QObject::destroyed, [raw] { delete raw; });   // lives as long as the backend
    } else {
        b->setDisplayName(QStringLiteral("Wayland (XWayland)"));
        b->setNotes(QStringLiteral("Wayland: recording and playback only work with programs running through XWayland (%1). ")
                        .arg(uinputWhy) + kWaylandHelp);
    }
    return b;
}

}  // namespace platform
