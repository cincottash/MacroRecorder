#include "AppData.h"
#include "Language.h"
#include "MainWindow.h"
#include "Settings.h"
#include "Themes.h"
#include "core/Tr.h"
#include "platform/Backend.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>

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

// The name the running copy listens on: per user, and per settings folder when --data-folder is given (portable
// copies and tests run side by side).
static QString instanceName(const QString& dataFolderArg)
{
    QByteArray key = qgetenv("USERNAME") + '|' + qgetenv("USER") + '|' + QDir::homePath().toUtf8();
    if (!dataFolderArg.isEmpty()) key += '|' + QDir::cleanPath(QDir::current().absoluteFilePath(dataFolderArg)).toLower().toUtf8();
    return QStringLiteral(APP_NAME "-") + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(16));
}

// True if a running copy took the message (this start then has nothing more to do).
static bool handToRunningCopy(const QString& name, const QJsonObject& msg)
{
    QLocalSocket s;
    s.connectToServer(name);
    if (!s.waitForConnected(500)) return false;
    s.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
    if (!s.waitForBytesWritten(2000)) return false;
    if (!s.canReadLine()) s.waitForReadyRead(3000);
    return s.readAll().startsWith("ok");
}

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
    const QCommandLineOption languageOpt(QStringLiteral("language"),
                                         QStringLiteral("Show the app in this language for this run only: en (English) or es (Spanish)."),
                                         QStringLiteral("code"));
    cli.addOptions({play, repeat, minimized, quit, dataFolder, screenshot, record, languageOpt});
    cli.process(app);
    if (cli.isSet(languageOpt) && !language::isKnown(cli.value(languageOpt))) {
        fprintf(stderr, "Unknown language \"%s\": use en or es.\n", qPrintable(cli.value(languageOpt)));
        return 2;
    }

    if (cli.isSet(screenshot)) {
        QTemporaryDir tmp;
        appdata::resolve(tmp.path(), nullptr);
        language::install(cli.isSet(languageOpt) ? cli.value(languageOpt) : QStringLiteral("en"));   // never asked here
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

    // One running copy per settings folder: a second start hands its file (or "come to the front") to the first, which
    // already holds the global hotkeys. --record runs on its own (it records, saves and quits).
    const QStringList files = cli.positionalArguments();
    const QString instance = instanceName(cli.value(dataFolder));
    if (!cli.isSet(record)) {
        QJsonObject msg;
        if (!files.isEmpty()) msg[QStringLiteral("file")] = QDir::current().absoluteFilePath(files.first());
        msg[QStringLiteral("play")] = cli.isSet(play);
        msg[QStringLiteral("show")] = !cli.isSet(minimized);
        if (handToRunningCopy(instance, msg)) {
            printf("%s is already running: %s\n", APP_NAME,
                   files.isEmpty() ? "brought it to the front." : cli.isSet(play) ? "it plays the macro." : "it opens the macro.");
            return 0;
        }
    }

    // The language: the one kept in the settings file, or asked now. Asked before the welcome window (which asks
    // where to keep the settings), so that one shows in the chosen language too. Starts that must not wait for an
    // answer (--play, --record, --minimized, Start at Login) use the system's language and ask at a normal start.
    Settings saved;
    if (const QString known = appdata::knownSettingsFile(cli.value(dataFolder)); !known.isEmpty()) saved.load(known);
    themes::apply(themes::forChoice(saved.theme));   // the question and the welcome window in the app's theme
    QString lang = cli.value(languageOpt);
    if (lang.isEmpty()) lang = saved.language;
    bool keepLanguage = !cli.isSet(languageOpt);
    if (!language::isKnown(lang)) {
        const bool unattended = cli.isSet(play) || cli.isSet(record) || cli.isSet(minimized);
        lang = unattended ? language::systemDefault() : language::ask(nullptr, language::systemDefault());
        keepLanguage = !unattended;
    }
    language::install(lang);

    const QString folder = appdata::resolve(cli.value(dataFolder), nullptr);
    if (folder.isEmpty()) return 0;  // first-run dialog cancelled
    Settings settings;
    settings.load(appdata::settingsFile());
    if (keepLanguage && settings.language != lang) {   // kept in the app data folder at once
        settings.language = lang;
        settings.save(appdata::settingsFile());
    }

    int rc = 0;
    {   // the window and the running-copy server end here, before a restart (File > Settings > Language) starts the next copy
    MainWindow w(settings);
    QLocalServer server;
    if (!cli.isSet(record)) {
        QLocalServer::removeServer(instance);   // a socket file left by a crash (Linux); no copy answered above
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (server.listen(instance)) {
            QObject::connect(&server, &QLocalServer::newConnection, &w, [&server, &w] {
                while (QLocalSocket* c = server.nextPendingConnection()) {
                    QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
                    QObject::connect(c, &QLocalSocket::readyRead, &w, [c, &w] {
                        if (!c->canReadLine()) return;
                        const QJsonObject msg = QJsonDocument::fromJson(c->readLine().trimmed()).object();
                        c->write("ok\n");
                        c->flush();
                        c->disconnectFromServer();
                        QTimer::singleShot(0, &w, [&w, msg] { w.handOver(msg); });
                    });
                }
            });
        }
    }
    if (!files.isEmpty()) w.openFile(QDir::current().absoluteFilePath(files.first()));
    // --record is for recording other programs, so the window starts out of the way
    if (cli.isSet(minimized) || cli.isSet(record) || settings.startMinimized) w.showMinimized(); else w.show();
    if (cli.isSet(record)) {
        const QString out = QDir::current().absoluteFilePath(cli.value(record));
        QTimer::singleShot(300, &w, [&w, out] { w.recordToFileAndQuit(out); });
    }
    if (cli.isSet(play)) {
        if (files.isEmpty()) {
            QMessageBox::warning(&w, QStringLiteral(APP_NAME), T("--play needs a macro file to play."));
        } else {
            const int n = cli.value(repeat).toInt();
            const bool q = cli.isSet(quit);
            QTimer::singleShot(300, &w, [&w, n, q] { w.playFromCommandLine(qMax(0, n), q); });
        }
    }
    rc = app.exec();
    }
    // A restart asked for while running (a new language): the hotkeys and the running-copy name are free now
    if (!MainWindow::restartAfterQuit().program.isEmpty()) {
        const auto& again = MainWindow::restartAfterQuit();
        if (!QProcess::startDetached(again.program, again.args))
            fprintf(stderr, "%s couldn't start again by itself: open it again.\n", APP_NAME);
    }
    return rc;
}
