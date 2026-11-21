// Linux: placeholder until the X11 and Wayland backends land. It reports which session it found, so the app can
// explain itself, and lets macros be opened, edited and saved. Recording and playback are disabled for now.
#include "platform/Backend.h"

#include <QProcessEnvironment>

#include <chrono>

namespace platform {

qint64 Backend::now()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

namespace {

class NullInjector : public core::Injector {
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
};

class LinuxBackend : public Backend {
public:
    explicit LinuxBackend(QObject* parent) : Backend(parent)
    {
        qRegisterMetaType<core::RawEvent>();
        const auto env = QProcessEnvironment::systemEnvironment();
        m_session = env.value(QStringLiteral("XDG_SESSION_TYPE"));
        if (m_session.isEmpty()) m_session = env.contains(QStringLiteral("WAYLAND_DISPLAY")) ? QStringLiteral("wayland") : QStringLiteral("x11");
    }
    QString name() const override { return m_session == QLatin1String("wayland") ? QStringLiteral("Wayland") : QStringLiteral("X11"); }
    QString unavailableReason() const override
    {
        return QStringLiteral("Recording and playback on Linux (%1) are coming in an update. "
                              "You can open, edit and save macros meanwhile.").arg(name());
    }
    void setCapture(CaptureMode) override {}
    core::Injector* injector() override { return &m_inj; }
    bool setHotkey(int, const QKeySequence& seq, QString* error) override
    {
        if (seq.isEmpty()) return true;
        if (error) *error = QStringLiteral("Global hotkeys aren't available on Linux yet.");
        return false;
    }

private:
    QString m_session;
    NullInjector m_inj;
};

}  // namespace

Backend* createLinuxBackend(QObject* parent) { return new LinuxBackend(parent); }

}  // namespace platform
