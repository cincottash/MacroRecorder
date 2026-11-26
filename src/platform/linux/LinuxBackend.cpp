// Linux: picks the input method for this session when the app starts.
//   X11 session                    -> X11 backend (record and play in every program)
//   Wayland + /dev/uinput access   -> play in every program through uinput; record/hotkeys via XWayland if present
//   Wayland without uinput access  -> XWayland programs only (if XWayland is running)
#include "platform/Backend.h"
#include "UinputInjector.h"
#include "X11Backend.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>

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

// A function, not a constant: the text follows the language chosen when the app starts
QString waylandHelp()
{
    return T("For recording on Wayland, choose an “X11” or “Xorg” session at the login screen. For playback into every "
             "program, give the app access to /dev/uinput (see the README, “Linux setup”).");
}

class NoBackend : public Backend {
public:
    NoBackend(QObject* parent, QString why) : Backend(parent), m_why(std::move(why)) { qRegisterMetaType<core::RawEvent>(); }
    QString name() const override { return QStringLiteral("none"); }
    QString unavailableReason() const override { return m_why; }
    void setCapture(CaptureMode) override {}
    core::Injector* injector() override { return &m_inj; }
    bool setHotkey(int, const QKeySequence& seq, QString* error) override
    {
        if (!seq.isEmpty() && error) *error = T("Global hotkeys aren't available here.");
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
        return T("Recording isn't possible on this Wayland session (no XWayland).") + QLatin1Char(' ') + waylandHelp();
    }
    QString notes() const override
    {
        return T("Wayland: playback works in every program, but hotkeys and Stop on Mouse Move aren't available. "
                 "Use the Stop button to stop.");
    }
    QString limitedTag() const override { return T("Wayland: Limited"); }
    void setCapture(CaptureMode) override {}
    core::Injector* injector() override { return m_inj.get(); }
    bool setHotkey(int, const QKeySequence& seq, QString* error) override
    {
        if (!seq.isEmpty() && error) *error = T("Global hotkeys aren't available on Wayland without XWayland.");
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
        return new NoBackend(parent, T("No graphical session was found (neither X11 nor Wayland)."));
    }

    QString uinputWhy;
    std::unique_ptr<UinputInjector> uin = UinputInjector::create(&uinputWhy);
    if (!hasX) {
        if (uin) return new UinputOnlyBackend(parent, std::move(uin));
        return new NoBackend(parent, T("This Wayland session has no XWayland, and the app has %1.").arg(uinputWhy) + QLatin1Char(' ') + waylandHelp());
    }
    auto* b = new X11Backend(parent, uin.get());
    if (uin) {
        b->setDisplayName(QStringLiteral("Wayland (uinput + XWayland)"));
        b->setLimitedTag(T("Wayland: Limited"));
        b->setNotes(T("Wayland: playback works in every program (plain US-keyboard characters only). Recording, "
                      "hotkeys, the active window and Stop on Mouse Move only see programs running through XWayland.")
                    + QLatin1Char(' ') + waylandHelp());
        UinputInjector* raw = uin.release();
        QObject::connect(b, &QObject::destroyed, [raw] { delete raw; });   // lives as long as the backend
    } else {
        b->setDisplayName(QStringLiteral("Wayland (XWayland)"));
        b->setLimitedTag(T("Wayland: Limited"));
        b->setNotes(T("Wayland: recording and playback only work with programs running through XWayland (%1).")
                        .arg(uinputWhy) + QLatin1Char(' ') + waylandHelp());
    }
    return b;
}

// ---------------------------------------------------------------- system

namespace sys {

QChar listSeparator()
{
    // Linux has no list separator setting; spreadsheets there follow the decimal mark.
    return QLocale().decimalPoint() == QStringLiteral(",") ? QLatin1Char(';') : QLatin1Char(',');
}


namespace {
QString autostartFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("autostart/" APP_NAME ".desktop"));
}
bool start(const QString& program, const QStringList& args, QString* error)
{
    if (QProcess::startDetached(program, args)) return true;
    if (error) *error = T("Couldn't run %1.").arg(program);
    return false;
}
}  // namespace

bool run(SystemAction a, QString* error)
{
    const QString session = qEnvironmentVariable("XDG_SESSION_ID");
    switch (a) {
    case SystemAction::Lock:
        if (start(QStringLiteral("loginctl"), {QStringLiteral("lock-session")}, nullptr)) return true;
        return start(QStringLiteral("xdg-screensaver"), {QStringLiteral("lock")}, error);
    case SystemAction::Sleep: return start(QStringLiteral("systemctl"), {QStringLiteral("suspend")}, error);
    case SystemAction::LogOff:
        if (!session.isEmpty()) return start(QStringLiteral("loginctl"), {QStringLiteral("terminate-session"), session}, error);
        return start(QStringLiteral("loginctl"), {QStringLiteral("terminate-user"), qEnvironmentVariable("USER")}, error);
    case SystemAction::Restart: return start(QStringLiteral("systemctl"), {QStringLiteral("reboot")}, error);
    case SystemAction::ShutDown: return start(QStringLiteral("systemctl"), {QStringLiteral("poweroff")}, error);
    }
    return false;
}

bool startsAtLogin() { return QFileInfo::exists(autostartFile()); }

bool setStartAtLogin(bool on, QString* error)
{
    const QString f = autostartFile();
    if (!on) {
        if (!QFileInfo::exists(f) || QFile::remove(f)) return true;
        if (error) *error = T("Couldn't remove %1.").arg(f);
        return false;
    }
    QDir().mkpath(QFileInfo(f).absolutePath());
    // An AppImage runs from a temporary mount; $APPIMAGE is the file the user actually has.
    const QString exe = qEnvironmentVariableIsSet("APPIMAGE") ? qEnvironmentVariable("APPIMAGE") : QCoreApplication::applicationFilePath();
    QSaveFile out(f);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=" APP_NAME "\nExec=\"%1\" --minimized\nX-GNOME-Autostart-enabled=true\n")
                      .arg(exe).toUtf8());
        if (out.commit()) return true;
    }
    if (error) *error = T("Couldn't write %1.").arg(f);
    return false;
}

bool isElevated() { return true; }   // nothing to elevate: X11 has no per-window privilege levels

bool restartElevated(const QStringList&, QString* error)
{
    if (error) *error = T("Not needed on Linux.");
    return false;
}

void beep() { QApplication::beep(); }

bool centerWindowAt(quintptr, QPoint) { return false; }   // X11 positions are already pixels

QString shortcutExtension() { return QStringLiteral(".desktop"); }

bool createShortcut(const QString& path, const QString& program, const QStringList& args, const QString& description, QString* error)
{
    // Desktop Entry rules: arguments with spaces or quotes are quoted, and inside quotes " ` $ \ are escaped
    auto quote = [](const QString& a) {
        static const QString special = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
        bool needs = a.isEmpty();
        for (QChar c : a) needs |= special.contains(c);
        if (!needs) return a;
        QString out = QStringLiteral("\"");
        for (QChar c : a) {
            if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\')) out += QLatin1Char('\\');
            out += c;
        }
        return out + QLatin1Char('"');
    };
    QStringList exec{quote(program)};
    for (const QString& a : args) exec << quote(a);
    QString escapedExec = exec.join(QLatin1Char(' '));
    escapedExec.replace(QLatin1String("\\"), QLatin1String("\\\\"));   // the file format escapes backslashes once more
    escapedExec.replace(QLatin1Char('%'), QLatin1String("%%"));
    const QString text = QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nComment=%2\nExec=%3\nIcon=%4\nTerminal=false\n")
                             .arg(QFileInfo(path).completeBaseName(), description, escapedExec, QStringLiteral(APP_NAME));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner | QFile::ExeUser);   // desktops start only executable entries
    // GNOME asks before starting an entry it doesn't trust; mark this one as trusted where gio exists
    QProcess::execute(QStringLiteral("gio"), {QStringLiteral("set"), path, QStringLiteral("metadata::trusted"), QStringLiteral("true")});
    return true;
}

}  // namespace sys

}  // namespace platform
