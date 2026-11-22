#include "AppData.h"
#include "MainWindow.h"
#include "Settings.h"
#include "platform/Backend.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>

#include <cstdio>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
// Chooses Qt's display plugin before the application object exists. The window always runs on X11: directly, or
// through XWayland on a Wayland session. Global hotkeys, recording and window placement need X11, and the AppImage
// carries only that plugin (plus "offscreen"). Systems that set QT_QPA_PLATFORM=wayland for their Qt programs made
// the app abort at start ("Could not find the Qt platform plugin wayland"); now it uses XWayland's display.
// False (after saying why on stderr): there's no X display to use.
static bool chooseDisplayPlugin()
{
    const QByteArray asked = qgetenv("QT_QPA_PLATFORM").toLower();
    if (!asked.isEmpty() && !asked.startsWith("wayland")) return true;   // "offscreen", "xcb", ...: the caller's choice
    if (!qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
        return true;
    }
    fprintf(stderr, "%s needs an X11 display, and none was found (DISPLAY isn't set).\n"
                    "On a Wayland session it runs through XWayland: turn XWayland on in the desktop's settings,\n"
                    "or choose an \"X11\" or \"Xorg\" session at the login screen.\n", APP_NAME);
    return false;
}
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_LINUX
    if (!chooseDisplayPlugin()) return 1;
#endif
    platform::Backend::earlyInit();
#ifdef Q_OS_WIN
    // Started from a command prompt: let --help and errors print there.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral(APP_NAME));
    QApplication::setOrganizationName(QStringLiteral(APP_ORG));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QFont f(QStringLiteral("Segoe UI"), 10);
    QApplication::setFont(f);

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Records and plays back mouse and keyboard actions."));
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A macro file to open."));
    const QCommandLineOption play(QStringLiteral("play"), QStringLiteral("Play the macro right away."));
    const QCommandLineOption repeat(QStringLiteral("repeat"), QStringLiteral("Play it this many times (with --play)."), QStringLiteral("n"));
    const QCommandLineOption minimized(QStringLiteral("minimized"), QStringLiteral("Start minimized."));
    const QCommandLineOption quit(QStringLiteral("quit"), QStringLiteral("Close the app when playback ends (with --play)."));
    const QCommandLineOption dataFolder(QStringLiteral("data-folder"),
                                        QStringLiteral("Keep settings in this folder for this run (portable use) instead of the usual one."),
                                        QStringLiteral("folder"));
    const QCommandLineOption record(QStringLiteral("record"),
                                    QStringLiteral("Start recording at once (minimized); when you stop (Stop hotkey), save to this file and quit."),
                                    QStringLiteral("file"));
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a picture of the window to this PNG file and exit (for testing; "
                                                       "uses throwaway settings and no hotkeys)."), QStringLiteral("png"));
    cli.addOptions({play, repeat, minimized, quit, dataFolder, screenshot, record});
    cli.process(app);

    if (cli.isSet(screenshot)) {
        QTemporaryDir tmp;
        appdata::resolve(tmp.path(), nullptr);
        Settings settings;
        MainWindow w(settings, false);   // shows the default hotkeys without registering them
        w.setAttribute(Qt::WA_DontShowOnScreen);
        if (!cli.positionalArguments().isEmpty()) {
            // Never a dialog in this mode (it runs unattended): a bad file is reported here and the run fails.
            const QString file = QDir::current().absoluteFilePath(cli.positionalArguments().first());
            core::Macro m;
            QString err;
            if (!QFileInfo::exists(file) || !core::Macro::load(file, &m, &err)) {
                fprintf(stderr, "Can't open %s: %s\n", qPrintable(QDir::toNativeSeparators(file)),
                        qPrintable(err.isEmpty() ? QStringLiteral("it doesn't exist") : err));
                return 2;
            }
            w.openFile(file);
        }
        w.resize(1320, 860);
        w.show();
        const QString out = cli.value(screenshot);
        int rc = 1;
        QTimer::singleShot(500, &w, [&] {
            rc = w.grab().save(out) ? 0 : 1;
            app.quit();
        });
        app.exec();
        return rc;
    }

    const QString folder = appdata::resolve(cli.value(dataFolder), nullptr);
    if (folder.isEmpty()) return 0;  // first-run dialog cancelled
    Settings settings;
    settings.load(appdata::settingsFile());

    MainWindow w(settings);
    const QStringList files = cli.positionalArguments();
    if (!files.isEmpty()) w.openFile(QDir::current().absoluteFilePath(files.first()));
    // --record is for recording other programs, so the window starts out of the way
    if (cli.isSet(minimized) || cli.isSet(record) || settings.startMinimized) w.showMinimized(); else w.show();
    if (cli.isSet(record)) {
        const QString out = QDir::current().absoluteFilePath(cli.value(record));
        QTimer::singleShot(300, &w, [&w, out] { w.recordToFileAndQuit(out); });
    }
    if (cli.isSet(play)) {
        if (files.isEmpty()) {
            QMessageBox::warning(&w, QStringLiteral(APP_NAME), QStringLiteral("--play needs a macro file to play."));
        } else {
            const int n = cli.value(repeat).toInt();
            const bool q = cli.isSet(quit);
            QTimer::singleShot(300, &w, [&w, n, q] { w.playFromCommandLine(qMax(0, n), q); });
        }
    }
    return app.exec();
}
