#include "MainWindow.h"

#include "ActionDialog.h"
#include "ActionPreview.h"
#include "ActionTable.h"
#include "AppData.h"
#include "AutoRunDialogs.h"
#include "FileDialogs.h"
#include "Language.h"
#include "ThemeFile.h"
#include "Themes.h"
#include "core/Tr.h"

#include <QActionGroup>
#include <cstdio>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

using core::Action;
using core::ActionType;

namespace {

const QString kRepoUrl = QStringLiteral("https://github.com/cincottash/MacroRecorder");
const QString kMime = QStringLiteral("application/x-" APP_NAME "-actions");
const QString kExt = QStringLiteral("macro");
enum HotkeyId { HkRecord = 1, HkPlay = 2, HkStop = 3, HkNewRecording = 4, HkPlayFrom = 5, HkPlaySelection = 6, HkCapture = 7 };
constexpr int kWaitForImage = 100;   // addAction() variant: a Find Image (or Find Text) step that waits for it
constexpr int kReadText = 101;       // addAction() variant: a Find Text step that only reads the text
constexpr int kMacroHotkeyBase = 100;   // Macro > Macro Hotkeys: hotkey ids 100, 101, ...

// A ring on the screen at the selected step's position. Click-through, never takes focus.
class PositionMarker : public QWidget {
public:
    explicit PositionMarker(QWidget* owner)
        : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput
                             | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFixedSize(46, 46);
        setProperty("testId", QStringLiteral("marker"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c = QRectF(rect()).center();
        for (const auto& [color, w] : {std::pair{QColor(255, 255, 255, 230), 5.0}, std::pair{QColor(0xE5, 0x48, 0x4D), 2.5}}) {
            p.setPen(QPen(color, w));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, 15, 15);
            p.drawLine(QPointF(c.x(), c.y() - 21), QPointF(c.x(), c.y() - 8));
            p.drawLine(QPointF(c.x(), c.y() + 8), QPointF(c.x(), c.y() + 21));
            p.drawLine(QPointF(c.x() - 21, c.y()), QPointF(c.x() - 8, c.y()));
            p.drawLine(QPointF(c.x() + 8, c.y()), QPointF(c.x() + 21, c.y()));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xE5, 0x48, 0x4D));
        p.drawEllipse(c, 2, 2);
    }
};

bool hasScreenPosition(const Action& a)
{
    switch (a.type) {
    case ActionType::MouseMove: case ActionType::MouseClick: case ActionType::MouseDown: case ActionType::MouseUp:
    case ActionType::Wheel: case ActionType::WaitPixel: return true;
    default: return false;
    }
}

QString quotedArg(const QString& s) { return QLatin1Char('"') + s + QLatin1Char('"'); }

QToolButton* linkButton(const QString& text)
{
    auto* b = new QToolButton;
    b->setObjectName(QStringLiteral("link"));
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QToolButton* toolButton(const QString& text, const QString& tip)
{
    auto* b = new QToolButton;
    b->setObjectName(QStringLiteral("tool"));
    b->setText(text);
    b->setToolTip(tip);
    return b;
}

QLabel* muted(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("muted"));
    l->setWordWrap(true);
    return l;
}

QString seconds(int ms)
{
    if (ms < 60000) return T("%1 s").arg(QLocale().toString(ms / 1000.0, 'f', 1));   // the system's decimal mark
    const int s = ms / 1000;
    return T("%1 min %2 s").arg(s / 60).arg(s % 60);
}

QString filterKey(ActionType t)
{
    switch (t) {
    case ActionType::MouseMove: return QStringLiteral("moves");
    case ActionType::MouseClick: case ActionType::MouseDown: case ActionType::MouseUp: case ActionType::Wheel:
        return QStringLiteral("clicks");
    case ActionType::KeyPress: case ActionType::KeyDown: case ActionType::KeyUp: case ActionType::Text:
        return QStringLiteral("keys");
    case ActionType::Wait: return QStringLiteral("waits");
    case ActionType::WindowFocus: case ActionType::WindowMove: return QStringLiteral("windows");
    default: return QStringLiteral("other");   // no Show filter: always shown (If, Group, ...)
    }
}

}  // namespace

MainWindow::MainWindow(Settings& s, bool hotkeys, QWidget* parent)
    : QMainWindow(parent), m_s(s), m_backend(platform::Backend::create(this)), m_hotkeysEnabled(hotkeys)
{
    // The system's menu and drop-down animations, before Window Opacity changes them (see applyOpacity)
    for (Qt::UIEffect e : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
                           Qt::UI_FadeTooltip})
        m_effectsDefault.insert(int(e), QApplication::isEffectEnabled(e));
    m_player = new core::Player(m_backend->injector(), this);
    setWindowIcon(QIcon(QStringLiteral(":/icon.svg")));
    themes::setCustom(themefile::fromStored(m_s.customThemes));   // imported themes, before the Theme menu is built
    buildMenus();
    buildUi();
    applyTheme();
    // The left column as wide as its cards need (in Spanish the labels run longer): nothing cut at its right edge
    if (QWidget* inner = m_leftScroll->widget()) {
        inner->ensurePolished();
        for (QWidget* w : inner->findChildren<QWidget*>()) w->ensurePolished();
        const int need = inner->minimumSizeHint().width() + m_leftScroll->verticalScrollBar()->sizeHint().width() + 2;
        m_leftScroll->setMinimumWidth(qMax(330, need));
        m_leftScroll->setMaximumWidth(qMax(400, need));
    }

    connect(m_backend, &platform::Backend::rawEvent, &m_recorder, &core::Recorder::feed);
    connect(m_backend, &platform::Backend::hotkeyPressed, this, &MainWindow::onHotkey);
    connect(m_backend, &platform::Backend::userInput, this, [this](bool key) {
        if (m_state != State::Playing || m_player->paused() || m_msgBox || m_askBox) return;
        if (key && m_s.stopOnKey) stopAll(T("Stopped: a key was pressed (Stop on Key Press is on)."));
        else if (!key && m_s.stopOnMouse) stopAll(T("Stopped: the mouse moved (Stop on Mouse Move is on)."));
    });
    connect(&m_recorder, &core::Recorder::actionAdded, this, [this](const Action& a) {
        m_model.insert(m_recordInsert++, a);
        m_table->scrollTo(m_model.index(m_recordInsert - 1, 0));
        log(T("Recorded: %1 · %2").arg(a.typeName(), a.valueText()), true);
        updateHeader();
    });
    connect(&m_recorder, &core::Recorder::lastActionChanged, this, [this](const Action& a) {
        m_model.replace(m_recordInsert - 1, a);
        updateHeader();
    });
    connect(m_player, &core::Player::step, this, [this](int index, int round, int rounds) {
        if (!m_playingOther.isEmpty()) {   // a macro file that isn't the one in the list: no row to mark
            if (m_s.detailedLog && index < m_otherActions.size())
                log(T("Step %1: %2").arg(index + 1).arg(m_otherActions.at(index).typeName()), true);
            setPill(T("▶ %1 · Step %2 of %3").arg(QFileInfo(m_playingOther).completeBaseName()).arg(index + 1)
                        .arg(m_otherActions.size()), QStringLiteral("ok"));
            return;
        }
        if (index >= m_model.rowCount()) return;
        m_model.setPlayingRow(index);
        if (m_s.detailedLog) log(T("Step %1: %2").arg(index + 1).arg(m_model.at(index).typeName()), true);
        const QString r = rounds > 0 ? T("%1 of %2", "rounds").arg(round).arg(rounds) : T("round %1").arg(round);
        setPill(T("▶ Playing %1 · Step %2 of %3").arg(r).arg(index + 1).arg(m_model.rowCount()), QStringLiteral("ok"));
    });
    connect(m_player, &core::Player::message, this, [this](const QString& t, bool) { log(t); });
    connect(m_player, &core::Player::notify, this, [this](const QString& t) {
        log(T("Notification: %1").arg(t));
        if (!m_s.showNotifications) return;
        if (!m_tray) m_tray = new QSystemTrayIcon(windowIcon(), this);
        m_tray->show();
        m_tray->showMessage(QStringLiteral(APP_NAME), t, QSystemTrayIcon::Information, 5000);
    });
    connect(m_player, &core::Player::messageBox, this, [this](const QString& t) { showMessageBox(t); });
    connect(m_player, &core::Player::askText, this, [this](const QString& q, const QString& d) { showAskBox(q, d); });
    connect(m_player, &core::Player::askAI, this, [this](const QString& p, int ms) { askAI(p, ms); });
    connect(m_player, &core::Player::detail, this, [this](const QString& t) { log(t, true); });
    connect(m_player, &core::Player::clipboard, this, [](const QString& t) { QApplication::clipboard()->setText(t); });
    // The player's thread can't read the clipboard: it gets a copy whenever the clipboard changes
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] { m_player->setClipboardCache(QApplication::clipboard()->text()); });
    connect(m_player, &core::Player::beep, this, [this] { m_backend->beep(); });
    connect(m_player, &core::Player::finished, this, [this](bool stopped, const QString& why) {
        m_backend->setBlockKeyboard(false);
        m_backend->setCapture(platform::CaptureMode::Off);
        if (m_msgBox) m_msgBox->done(0);   // stopped while a Message box step was waiting
        if (m_askBox) m_askBox->done(QDialog::Rejected);
        if (m_aiReply) m_aiReply->abort();   // stopped while waiting for the AI server
        m_model.setPlayingRow(-1);
        const QString other = m_playingOther;
        m_playingOther.clear();
        m_otherActions.clear();
        const QString what = other.isEmpty() ? QString() : QFileInfo(other).fileName() + QStringLiteral(": ");
        if (!why.isEmpty()) log(T("Stopped: %1%2").arg(what, why));
        else log(what + (stopped ? T("Playback stopped.") : T("Playback finished.")));
        setState(State::Idle);
        registerHotkeys();   // releases the Stop hotkey (Esc) so it works normally in other programs again
        if (isMinimized() && m_s.hideWhilePlaying && other.isEmpty()) showNormal();
        // "When Finished" (close, shut down...) belongs to the open macro: a hotkey or a schedule never shuts the PC down
        if (!stopped && why.isEmpty() && other.isEmpty()) runAfterPlayback();
        if (m_quitWhenDone) qApp->quit();
    });

    m_countdown.setInterval(1000);
    connect(&m_countdown, &QTimer::timeout, this, [this] {
        if (--m_countdownLeft > 0) {
            setPill(T("Starting in %1 s…").arg(m_countdownLeft), QStringLiteral("warn"));
            return;
        }
        m_countdown.stop();
        beginPlayback();
    });
    m_themePreviewTimer.setSingleShot(true);
    m_themePreviewTimer.setInterval(90);
    connect(&m_themePreviewTimer, &QTimer::timeout, this, [this] {
        if (!m_themePreviewing.isEmpty()) themes::apply(themes::forChoice(m_themePreviewing), centralWidget());
    });
    m_scheduleTimer.setInterval(1000);
    connect(&m_scheduleTimer, &QTimer::timeout, this, [this] { checkSchedules(); });
    reschedule();
    m_scheduleTimer.start();
    // A hotkey field getting (or losing) the keyboard: global hotkeys step aside so the field receives the keys
    connect(qApp, &QApplication::focusChanged, this, [this] { if (typingHotkey() != m_hotkeysPaused) registerHotkeys(); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { if (typingHotkey() != m_hotkeysPaused) registerHotkeys(); });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { m_s.save(appdata::settingsFile()); });
    connect(&m_model, &ActionModel::edited, this, [this] { setDirty(true); });
    m_backend->injector()->setScanCodesOnly(m_s.scanCodes);
    m_marker = new PositionMarker(this);
    qApp->installEventFilter(this);   // menus, drop-down lists and dialogs get the window's opacity as they open
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { updateMarker(); });

    // Size: remembered, or a comfortable size that fits the screen.
    if (m_s.geometry.isEmpty() || !restoreGeometry(m_s.geometry)) {
        const QRect avail = screen()->availableGeometry();
        resize(qMin(1320, int(avail.width() * 0.9)), qMin(860, int(avail.height() * 0.9)));
        move(avail.center() - rect().center());
    }
    if (m_s.alwaysOnTop) setWindowFlag(Qt::WindowStaysOnTopHint, true);

    registerHotkeys();
    const QString why = m_backend->unavailableReason();
    log(why.isEmpty() ? T("Ready. Input method: %1.").arg(m_backend->name()) : why);
    if (appdata::isTemporary() && m_hotkeysEnabled)
        log(T("Settings aren't being kept (Don't Keep App Data is on): the theme and options reset when the app "
                           "closes. To keep them, choose a folder in File > Settings > Change App Data Folder…"));
    if (why.isEmpty() && !m_backend->recordUnavailableReason().isEmpty()) log(m_backend->recordUnavailableReason());
    if (!m_backend->notes().isEmpty()) log(m_backend->notes());
    setState(State::Idle);
}

MainWindow::~MainWindow()
{
    m_player->stop();
    for (auto it = m_effectsDefault.cbegin(); it != m_effectsDefault.cend(); ++it)   // the system's animations back
        QApplication::setEffectEnabled(Qt::UIEffect(it.key()), it.value());
}

// ------------------------------------------------------------------------------------------------ building

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(T("&File"));
    file->addAction(T("&New"), QKeySequence::New, this, [this] { newMacro(); });
    file->addAction(T("&Open…"), QKeySequence::Open, this, [this] { open(); });
    m_recentMenu = file->addMenu(T("Open &Recent"));
    rebuildRecentMenu();
    file->addSeparator();
    m_saveAct = file->addAction(T("&Save"), QKeySequence::Save, this, [this] { save(false); });
    file->addAction(T("Save &As…"), QKeySequence::SaveAs, this, [this] { save(true); });
    file->addSeparator();
    file->addAction(T("&Export to CSV…"), this, [this] { exportCsv(); });
    file->addAction(T("Create &Desktop Shortcut…"), this, [this] { createDesktopShortcut(); })
        ->setToolTip(T("A shortcut on the desktop that plays the saved macro (and then closes the app, if it "
                                    "wasn't already running)."));
    file->addAction(T("Copy &Command Line"), this, [this] { copyCommandLine(); })
        ->setToolTip(T("For a desktop shortcut or Windows Task Scheduler: plays the saved macro, then closes."));
    file->addSeparator();
    QMenu* settings = file->addMenu(T("Se&ttings"));
    // Each language's name in its own language, so it can be found whatever the app shows now
    QMenu* languages = settings->addMenu(T("Language"));
    auto* languageGroup = new QActionGroup(languages);
    for (const language::Choice& c : language::available()) {
        QAction* a = languages->addAction(c.name);
        a->setCheckable(true);
        a->setChecked(c.code == (m_s.language.isEmpty() ? language::current() : m_s.language));
        a->setData(QStringLiteral("language:") + c.code);   // not a label to check: the name is in its own language
        languageGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, code = c.code] { chooseLanguage(code); });
    }
    settings->addSeparator();
    settings->addAction(T("Change App Data Folder…"), this, [this] {
        QString msg;
        if (appdata::change(this, &msg)) { m_s.save(appdata::settingsFile()); log(msg); }
        else if (!msg.isEmpty()) QMessageBox::warning(this, T("App Data Folder"), msg);
        if (m_notKept) m_notKept->setVisible(appdata::isTemporary());
    });
    settings->addAction(T("Reset Settings to Defaults…"), this, [this] {
        if (QMessageBox::question(this, T("Reset Settings"),
                                  T("Reset playback, recording and hotkey settings to their defaults?\n"
                                    "Your theme, layout and recent files are kept.")) != QMessageBox::Yes)
            return;
        m_s.resetOptions();
        m_s.save(appdata::settingsFile());
        syncWidgetsFromSettings();
        registerHotkeys();
        m_backend->injector()->setScanCodesOnly(m_s.scanCodes);
        log(T("Settings reset to defaults."));
    });
    settings->addAction(T("AI Server…"), this, [this] { editAiServer(); })
        ->setToolTip(T("Where Ask AI steps send their questions: LM Studio on this PC (free) unless changed."));
    settings->addAction(T("Start Over…"), this, [this] { startOver(); })
        ->setToolTip(T("Every setting back to its default, as if the app had just been installed. "
                                    "Your saved macros aren't touched."));
    settings->addSeparator();
    auto toggle = [&](const QString& text, bool on, const QString& tip, std::function<void(bool)> f) {
        QAction* a = settings->addAction(text);
        a->setCheckable(true);
        a->setChecked(on);
        a->setToolTip(tip);
        connect(a, &QAction::toggled, this, [this, f](bool v) { f(v); saveSettingsSoon(); });
        return a;
    };
    m_confirmDeleteAct = toggle(T("Confirm Before Deleting Steps"), m_s.confirmDelete, {}, [this](bool v) { m_s.confirmDelete = v; });
    m_startMinAct = toggle(T("Start Minimized"), m_s.startMinimized, {}, [this](bool v) { m_s.startMinimized = v; });
    m_loginAct = toggle(T("Start at Login"), m_hotkeysEnabled && m_backend->startsAtLogin(),
                        T("Starts minimized, ready for your hotkeys."), [this](bool v) {
        QString err;
        if (!m_backend->setStartAtLogin(v, &err)) {
            log(T("Start at Login: %1").arg(err));
            const QSignalBlocker b(m_loginAct);
            m_loginAct->setChecked(!v);
        } else {
            log(v ? T("%1 will start when you log in.").arg(QStringLiteral(APP_NAME))
                  : T("%1 won't start when you log in any more.").arg(QStringLiteral(APP_NAME)));
        }
    });
#ifdef Q_OS_WIN
    m_scanAct = toggle(T("Send Keys as Scan Codes (Games)"), m_s.scanCodes,
                       T("Some games read only the keyboard's hardware codes. Leave off unless keys don't arrive."),
                       [this](bool v) { m_s.scanCodes = v; m_backend->injector()->setScanCodesOnly(v); });
    if (!m_backend->isElevated()) {
        settings->addSeparator();
        settings->addAction(T("Restart as Administrator…"), this, [this] {
            if (m_state != State::Idle || !maybeSave()) return;
            QStringList args;
            if (!m_file.isEmpty()) args << m_file;
            QString err;
            if (!m_backend->restartElevated(args, &err)) { log(T("Restart as administrator: %1").arg(err)); return; }
            m_dirty = false;
            close();
        })->setToolTip(T("Needed to control programs that run as administrator."));
    }
#endif
    file->addSeparator();
    file->addAction(T("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* macro = menuBar()->addMenu(T("&Macro"));
    m_buttonActions.clear();
    connect(macro, &QMenu::aboutToShow, this, [this] {
        for (auto& [act, btn] : m_buttonActions) act->setEnabled(btn && btn->isEnabled());
    });
    auto mirror = [&](const QString& text, std::function<QPushButton*()> btn) {
        QAction* a = macro->addAction(text);
        connect(a, &QAction::triggered, this, [btn] { if (btn()->isEnabled()) btn()->click(); });
        return a;
    };
    // The buttons don't exist yet; look them up when used.
    m_buttonActions << qMakePair(mirror(T("&Record"), [this] { return m_recordBtn; }), static_cast<QPushButton*>(nullptr));
    m_editActs << macro->addAction(T("Record After Selected Step"), this, [this] { startRecording(RecordWhere::AfterSelection); });
    macro->addAction(T("&New Recording"), this, [this] { startRecording(RecordWhere::New); })
        ->setToolTip(T("Starts a new macro (asks to save changes first) and records."));
    macro->addSeparator();
    m_buttonActions << qMakePair(mirror(T("&Play"), [this] { return m_playBtn; }), static_cast<QPushButton*>(nullptr));
    m_buttonActions << qMakePair(mirror(T("Play &Selection"), [this] { return m_playSelBtn; }), static_cast<QPushButton*>(nullptr));
    m_editActs << macro->addAction(T("Play From Selected &Step"), this, [this] { startPlaying(PlayRange::FromSelected); });
    m_editActs << macro->addAction(T("Play &Up to Selected Step"), this, [this] { startPlaying(PlayRange::UpToSelected); });
    m_editActs << macro->addAction(T("Play This Step &Only"), this, [this] { startPlaying(PlayRange::ThisStep); });
    m_buttonActions << qMakePair(mirror(T("P&ause / Resume"), [this] { return m_pauseBtn; }), static_cast<QPushButton*>(nullptr));
    m_buttonActions << qMakePair(mirror(T("S&top"), [this] { return m_stopBtn; }), static_cast<QPushButton*>(nullptr));
    macro->addSeparator();
    QMenu* add = buildAddMenu(this);
    add->setTitle(T("&Add Action"));
    macro->addMenu(add);
    macro->addAction(T("&Clean Up"), this, [this] { cleanUp(); })
        ->setToolTip(T("Merges back-to-back waits and moves, and removes empty ones."));
    macro->addSeparator();
    macro->addAction(T("Macro &Hotkeys…"), this, [this] { editMacroHotkeys(); })
        ->setToolTip(T("Hotkeys that play a saved macro from any program, without opening it."));
    macro->addAction(T("Sc&hedules…"), this, [this] { editSchedules(); })
        ->setToolTip(T("Plays saved macros at set times while the app runs."));

    QMenu* edit = menuBar()->addMenu(T("&Edit"));
    m_undoAct = edit->addAction(T("&Undo"), QKeySequence::Undo, this, [this] { undo(); });
    m_redoAct = edit->addAction(T("&Redo"), QKeySequence::Redo, this, [this] { redo(); });
    edit->addSeparator();
    m_editActs << edit->addAction(T("Cu&t"), QKeySequence::Cut, this, [this] { copySelected(true); });
    m_editActs << edit->addAction(T("&Copy"), QKeySequence::Copy, this, [this] { copySelected(false); });
    edit->addAction(T("&Paste"), QKeySequence::Paste, this, [this] { paste(); });
    m_editActs << edit->addAction(T("&Delete"), QKeySequence::Delete, this, [this] { deleteSelected(); });
    edit->addAction(T("Select &All"), QKeySequence::SelectAll, this, [this] { m_table->selectAll(); });
    edit->addSeparator();
    m_editActs << edit->addAction(T("&Edit Step…"), QKeySequence(Qt::Key_F2), this, [this] {
        const auto rows = selectedRows();
        if (!rows.isEmpty()) editRow(rows.first());
    });
    m_editActs << edit->addAction(T("E&nable / Disable"), QKeySequence(Qt::CTRL | Qt::Key_E), this, [this] { toggleEnabled(); });
    m_editActs << edit->addAction(T("Move &Up"), QKeySequence(Qt::ALT | Qt::Key_Up), this, [this] { moveSelected(-1); });
    m_editActs << edit->addAction(T("Move Do&wn"), QKeySequence(Qt::ALT | Qt::Key_Down), this, [this] { moveSelected(1); });
    edit->addSeparator();
    QAction* combine = edit->addAction(T("C&ombine Into Click or Key Press"), this, [this] { combineSelected(); });
    combine->setToolTip(T("Select a button down and its button up (or a key down and up): they become one click (key press)."));
    m_editActs << combine;
    edit->addAction(T("&Find and Replace…"), QKeySequence(Qt::CTRL | Qt::Key_H), this, [this] { findReplace(); });
    edit->addSeparator();
    QAction* groupAct = edit->addAction(T("&Group Selected Steps"), QKeySequence(Qt::CTRL | Qt::Key_G), this, [this] { groupSelected(); });
    groupAct->setToolTip(T("Puts the selected steps in a named group that can be folded away (and turned off as a whole)."));
    m_editActs << groupAct;
    m_editActs << edit->addAction(T("U&ngroup"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), this, [this] { ungroupSelected(); });
    edit->addAction(T("Fold All Groups"), this, [this] { foldAll(true); });
    edit->addAction(T("Unfold All Groups"), this, [this] { foldAll(false); });

    m_viewMenu = menuBar()->addMenu(T("&View"));
    QMenu* theme = m_viewMenu->addMenu(T("&Theme"));
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    theme->setToolTipsVisible(true);
    // The groups (the entries that open a list) come first, then Match Operating System.
    m_customThemeMenu = theme->addMenu(themes::displayName(themes::customGroup()));   // shown once a theme has been imported
    for (const QString& g : themes::groups()) {
        QMenu* sub = theme->addMenu(themes::displayName(g));
        QList<const themes::Theme*> list;
        for (const themes::Theme& t : themes::all())
            if (t.group == g) list << &t;
        std::sort(list.begin(), list.end(), [](auto* a, auto* b) {
            return themes::displayName(a->name).compare(themes::displayName(b->name), Qt::CaseInsensitive) < 0;
        });
        for (auto* t : list) addThemeAction(sub, themes::displayName(t->name), t->key);
    }
    addThemeAction(theme, T("Match Operating System"), QStringLiteral("system"));
    theme->addSeparator();
    QAction* importAct = theme->addAction(T("&Import Theme…"), this, [this] { importTheme(); });
    importAct->setToolTip(T("Adds a theme from a .json file: one exported from here, or a VS Code color theme. "
                                         "It's kept with the other themes under Custom Themes."));
    QAction* exportAct = theme->addAction(T("&Export Current Theme…"), this, [this] { exportTheme(); });
    exportAct->setToolTip(T("Saves the theme in use as a .json file: a starting point for a theme of your own."));
    m_removeThemeMenu = theme->addMenu(T("&Remove Custom Theme"));
    rebuildCustomThemeMenus();
    connect(m_viewMenu, &QMenu::aboutToHide, this, [this] { endThemePreview(); });
    connect(theme, &QMenu::aboutToHide, this, [this] { endThemePreview(); });
    // The pointer on something that isn't a theme (a group's name such as "Dark Themes", or Import Theme…): show the
    // theme in use again. Otherwise the last theme the pointer passed over stayed previewed, and it looked as if
    // pointing at "Dark Themes" had changed the theme.
    connect(theme, &QMenu::hovered, this, [this](QAction* a) {
        const bool isTheme = !a->menu() && m_themeGroup->actions().contains(a);
        if (!isTheme && !m_themePreviewing.isEmpty() && m_themePreviewing != m_s.theme) previewTheme(m_s.theme);
    });
    m_viewMenu->addSeparator();
    m_logAct = m_viewMenu->addAction(T("Show Activity &Log"));
    m_logAct->setCheckable(true);
    m_logAct->setChecked(m_s.showLog);
    connect(m_logAct, &QAction::toggled, this, [this](bool on) { setCardOpen(QStringLiteral("ACTIVITY"), on); });
    QAction* detail = m_viewMenu->addAction(T("&Detailed Activity Log"));
    detail->setCheckable(true);
    detail->setChecked(m_s.detailedLog);
    detail->setToolTip(T("Also logs every recorded action and every step played."));
    connect(detail, &QAction::toggled, this, [this](bool on) { m_s.detailedLog = on; saveSettingsSoon(); });
    QAction* top = m_viewMenu->addAction(T("Always on &Top"));
    top->setCheckable(true);
    top->setChecked(m_s.alwaysOnTop);
    connect(top, &QAction::toggled, this, [this](bool on) {
        m_s.alwaysOnTop = on;
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        show();
        applyOpacity();
        saveSettingsSoon();
    });
    QMenu* opacity = m_viewMenu->addMenu(T("Window &Opacity"));
    {
        // One slider; the level always applies. The window changes as the slider moves, so the level can be judged
        // against what's behind it.
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(26, 5, 14, 5);
        h->setSpacing(10);
        m_opacitySlider = new QSlider(Qt::Horizontal);
        m_opacitySlider->setRange(30, 100);
        m_opacitySlider->setSingleStep(5);
        m_opacitySlider->setPageStep(10);
        m_opacitySlider->setMinimumWidth(170);
        m_opacitySlider->setValue(m_s.windowOpacity);
        m_opacitySlider->setProperty("testId", QStringLiteral("opacitySlider"));
        auto* value = new QLabel(T("%1 %").arg(m_s.windowOpacity));
        value->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("100 %")) + 4);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(new QLabel(T("Opacity")));
        h->addWidget(m_opacitySlider, 1);
        h->addWidget(value);
        auto* wa = new QWidgetAction(opacity);
        wa->setDefaultWidget(row);
        opacity->addAction(wa);
        connect(m_opacitySlider, &QSlider::valueChanged, this, [this, value](int v) {
            m_s.windowOpacity = v;
            value->setText(T("%1 %").arg(v));
            applyOpacity();
            saveSettingsSoon();
        });
    }
    m_markerAct = m_viewMenu->addAction(T("&Mark Selected Step on Screen"));
    m_markerAct->setCheckable(true);
    m_markerAct->setChecked(m_s.showPosition);
    connect(m_markerAct, &QAction::toggled, this, [this](bool on) { m_s.showPosition = on; updateMarker(); saveSettingsSoon(); });
    m_previewAct = m_viewMenu->addAction(T("&Preview Steps on Hover"));
    m_previewAct->setCheckable(true);
    m_previewAct->setChecked(m_s.showPreview);
    m_previewAct->setToolTip(T("Pointing at a mouse step in the list shows its path or position, animated."));
    connect(m_previewAct, &QAction::toggled, this, [this](bool on) { m_s.showPreview = on; if (!on) showPreview(-1); saveSettingsSoon(); });
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(T("&Reset View Options"), this, [this] {
        m_s.resetView();
        for (QAction* a : m_themeGroup->actions()) a->setChecked(a->data().toString() == m_s.theme);
        for (auto it = m_cards.cbegin(); it != m_cards.cend(); ++it) setCardOpen(it.key(), true, false);
        m_logAct->setChecked(true);
        for (QAction* a : {m_markerAct, m_previewAct}) { const QSignalBlocker b(a); a->setChecked(true); }
        m_opacitySlider->setValue(m_s.windowOpacity);   // also updates the value shown beside it
        applyOpacity();
        updateMarker();
        setWindowFlag(Qt::WindowStaysOnTopHint, false);
        show();
        m_fittingColumns = true;
        m_table->horizontalHeader()->resizeSections(QHeaderView::Interactive);
        m_fittingColumns = false;
        fitValueColumn();
        applyTheme();
        saveSettingsSoon();
    });

    QMenu* help = menuBar()->addMenu(T("&Help"));
    help->addAction(T("Project &Website"), this, [] { QDesktopServices::openUrl(QUrl(kRepoUrl)); });
    help->addAction(T("Open App &Data Folder"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(appdata::folder()));
    });
    help->addAction(T("&Report a Problem"), this, [] { QDesktopServices::openUrl(QUrl(kRepoUrl + QStringLiteral("/issues"))); });
    help->addAction(T("Check for &Updates…"), this, [this] { checkForUpdates(); })
        ->setToolTip(T("Asks GitHub for the latest release. Nothing is downloaded or installed by itself."));
    help->addSeparator();
    help->addAction(T("&About " APP_NAME), this, [this] {
        QMessageBox::about(this, T("About " APP_NAME),
                           QStringLiteral("<b>" APP_NAME " " APP_VERSION "</b><br>")
                               + T("Records and plays back mouse and keyboard actions.") + QStringLiteral("<br><br>Copyright © 2026 cincottash<br>")
                               + T("Licensed under the GNU GPL v3.0.") + QStringLiteral("<br><br>")
                               + T("Input method: %1").arg(m_backend->name())
                               + QStringLiteral("<br><a href=\"%1\">%1</a>").arg(kRepoUrl));
    });
}

QMenu* MainWindow::buildAddMenu(QWidget* parent)
{
    auto* menu = new QMenu(parent);
    auto add = [&](QMenu* m, const QString& text, ActionType t) {
        QAction* a = m->addAction(text);
        connect(a, &QAction::triggered, this, [this, t] { addAction(t); });
    };
    auto addImage = [&](QMenu* m, const QString& text, core::FindDo what) {
        QAction* a = m->addAction(text);
        connect(a, &QAction::triggered, this, [this, what] { addAction(ActionType::FindImage, int(what)); });
    };
    QMenu* mouse = menu->addMenu(T("Mouse"));
    auto addText = [&](QMenu* m, const QString& text, int variant) {
        QAction* a = m->addAction(text);
        connect(a, &QAction::triggered, this, [this, variant] { addAction(ActionType::FindText, variant); });
    };
    add(mouse, T("Click"), ActionType::MouseClick);
    addImage(mouse, T("Click Image"), core::FindDo::Click);
    addText(mouse, T("Click Text"), int(core::FindDo::Click));
    add(mouse, T("Move"), ActionType::MouseMove);
    add(mouse, T("Scroll"), ActionType::Wheel);
    add(mouse, T("Button Down (Start a Drag)"), ActionType::MouseDown);
    add(mouse, T("Button Up (End a Drag)"), ActionType::MouseUp);
    QMenu* keys = menu->addMenu(T("Keyboard"));
    add(keys, T("Key Press / Shortcut"), ActionType::KeyPress);
    add(keys, T("Text"), ActionType::Text);
    add(keys, T("Key Down (Hold)"), ActionType::KeyDown);
    add(keys, T("Key Up (Release)"), ActionType::KeyUp);
    QMenu* wait = menu->addMenu(T("Wait", "menu of waiting steps"));
    add(wait, T("Wait", "step type"), ActionType::Wait);
    {
        QAction* a = wait->addAction(T("Wait for Image"));
        connect(a, &QAction::triggered, this, [this] { addAction(ActionType::FindImage, kWaitForImage); });
    }
    addText(wait, T("Wait for Text"), kWaitForImage);
    add(wait, T("Wait for Pixel Color"), ActionType::WaitPixel);
    add(wait, T("Wait for Key"), ActionType::WaitKey);
    add(wait, T("Wait for File"), ActionType::WaitFile);
    add(wait, T("Ask for Text"), ActionType::AskText);
    QMenu* flow = menu->addMenu(T("Flow"));
    add(flow, T("Repeat", "step type"), ActionType::Repeat);
    add(flow, T("Go To"), ActionType::Goto);
    add(flow, T("Run Another Macro"), ActionType::RunMacro);
    flow->addSeparator();
    add(flow, T("Group", "step type"), ActionType::Group);
    QMenu* logic = menu->addMenu(T("Variables and Conditions"));
    add(logic, T("Set Variable"), ActionType::SetVariable);
    add(logic, T("If"), ActionType::If);
    add(logic, T("Else"), ActionType::Else);
    add(logic, T("End If"), ActionType::EndIf);
    logic->addSeparator();
    addText(logic, T("Read Text From Screen"), kReadText);
    add(logic, T("Ask AI"), ActionType::AskAI);
    QMenu* other = menu->addMenu(T("Programs and Windows"));
    add(other, T("Window Focus"), ActionType::WindowFocus);
    add(other, T("Move / Resize Window"), ActionType::WindowMove);
    add(other, T("Run Program"), ActionType::RunProgram);
    add(other, T("Notification"), ActionType::Notify);
    add(other, T("Message Box"), ActionType::MessageBox);
    add(other, T("Beep"), ActionType::Beep);
    other->addSeparator();
    addImage(other, T("Find Image"), core::FindDo::Nothing);
    add(other, T("Save Screenshot"), ActionType::Screenshot);
    add(other, T("Set Clipboard"), ActionType::SetClipboard);
    return menu;
}

QFrame* MainWindow::makeCard(const QString& title, QVBoxLayout** body, QWidget* headerExtra)
{
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    auto* lay = new QVBoxLayout(frame);
    lay->setContentsMargins(14, 10, 14, 12);
    lay->setSpacing(8);
    auto* head = new QHBoxLayout;
    // The title is the card's name in the settings (folded cards) and in the code: shown translated, kept in English
    auto* t = new QLabel(QCoreApplication::translate("app", title.toUtf8().constData()));
    t->setObjectName(QStringLiteral("cardTitle"));
    head->addWidget(t);
    head->addStretch();
    if (headerExtra) head->addWidget(headerExtra);
    auto* toggle = linkButton(T("Hide ▾"));
    head->addWidget(toggle);
    lay->addLayout(head);
    auto* b = new QWidget;
    auto* bl = new QVBoxLayout(b);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    lay->addWidget(b, 1);
    *body = bl;
    m_cards.insert(title, {frame, b, toggle});
    connect(toggle, &QToolButton::clicked, this, [this, title] {
        const Card& c = m_cards[title];
        setCardOpen(title, !c.body->isVisibleTo(c.frame));
    });
    return frame;
}

void MainWindow::setCardOpen(const QString& name, bool open, bool save)
{
    if (!m_cards.contains(name)) return;
    const Card& c = m_cards[name];
    c.body->setVisible(open);
    c.toggle->setText(open ? T("Hide ▾") : T("Show ▴"));
    c.frame->setSizePolicy(QSizePolicy::Preferred, open ? QSizePolicy::Preferred : QSizePolicy::Maximum);
    m_s.foldedCards.removeAll(name);
    if (!open) m_s.foldedCards << name;
    if (name == QLatin1String("ACTIVITY")) {
        m_s.showLog = open;
        if (m_logAct && m_logAct->isChecked() != open) {
            const QSignalBlocker b(m_logAct);
            m_logAct->setChecked(open);
        }
    }
    updateStretch();
    if (save) saveSettingsSoon();
}

void MainWindow::updateStretch()
{
    if (!m_rightCol) return;
    const bool actionsOpen = !m_s.foldedCards.contains(QStringLiteral("ACTIONS"));
    const bool logOpen = !m_s.foldedCards.contains(QStringLiteral("ACTIVITY"));
    m_rightCol->setStretchFactor(m_actionsCard, actionsOpen ? 3 : 0);
    m_rightCol->setStretchFactor(m_activityCard, logOpen ? 1 : 0);
    m_rightCol->setStretch(m_rightCol->count() - 1, (actionsOpen || logOpen) ? 0 : 1);  // trailing spacer
}

void MainWindow::buildUi()
{
    auto* root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    auto* outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header
    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("header"));
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(16, 10, 16, 10);
    auto* title = new QLabel(QStringLiteral(APP_NAME));
    title->setObjectName(QStringLiteral("appTitle"));
    m_fileLabel = new QLabel;
    m_fileLabel->setObjectName(QStringLiteral("muted"));
    m_pill = new QLabel;
    m_pill->setObjectName(QStringLiteral("pill"));
    hl->addWidget(title);
    hl->addSpacing(16);
    hl->addWidget(m_fileLabel, 1);
    // "Don't Keep App Data" is on: say so where it can't be missed, one click from changing it.
    m_notKept = linkButton(T("Settings Not Kept"));
    m_notKept->setProperty("testId", QStringLiteral("notKept"));
    m_notKept->setToolTip(T("Don't Keep App Data is on, so the theme and every other setting start fresh each time "
                                         "the app opens. Click to choose a folder to keep them in."));
    m_notKept->setVisible(appdata::isTemporary() && m_hotkeysEnabled);   // (screenshots use throwaway settings on purpose)
    connect(m_notKept, &QToolButton::clicked, this, [this] { trigger(T("Change App Data Folder…")); });
    hl->addWidget(m_notKept);
    // Where the input method reaches only some programs (Wayland): say so in the window, with a link to the details.
    m_limits = linkButton(T("What's Limited?"));
    m_limits->setProperty("testId", QStringLiteral("limits"));
    m_limits->setToolTip(m_backend->notes());
    m_limits->setVisible(!m_backend->limitedTag().isEmpty());
    connect(m_limits, &QToolButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(kRepoUrl + QStringLiteral("#linux-experimental"))); });
    hl->addWidget(m_limits);
    hl->addSpacing(10);
    hl->addWidget(m_pill);
    outer->addWidget(header);

    // Columns
    auto* cols = new QHBoxLayout;
    cols->setContentsMargins(14, 14, 14, 14);
    cols->setSpacing(14);

    auto* leftInner = new QWidget;
    auto* left = new QVBoxLayout(leftInner);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(14);
    left->addWidget(buildPlaybackCard());
    left->addWidget(buildRecordingCard());
    left->addWidget(buildHotkeysCard());
    left->addStretch(1);
    auto* scroll = new QScrollArea;
    scroll->setWidget(leftInner);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(330);
    scroll->setMaximumWidth(400);
    m_leftScroll = scroll;
    cols->addWidget(scroll);

    auto* rightW = new QWidget;
    m_rightCol = new QVBoxLayout(rightW);
    m_rightCol->setContentsMargins(0, 0, 0, 0);
    m_rightCol->setSpacing(14);
    m_actionsCard = buildActionsCard();
    m_activityCard = buildActivityCard();
    m_rightCol->addWidget(m_actionsCard);
    m_rightCol->addWidget(m_activityCard);
    m_rightCol->addStretch(0);
    cols->addWidget(rightW, 1);
    outer->addLayout(cols, 1);

    // Bottom bar
    auto* bottom = new QFrame;
    bottom->setObjectName(QStringLiteral("bottomBar"));
    auto* bl = new QHBoxLayout(bottom);
    bl->setContentsMargins(16, 10, 16, 10);
    m_playSelBtn = new QPushButton(T("▶ Play Selection"));
    m_recordBtn = new QPushButton(T("● Record"));
    m_recordBtn->setObjectName(QStringLiteral("record"));
    m_playBtn = new QPushButton(T("▶ Play"));
    m_playBtn->setObjectName(QStringLiteral("primary"));
    m_pauseBtn = new QPushButton(T("❚❚ Pause"));
    m_stopBtn = new QPushButton(T("■ Stop"));
    bl->addWidget(m_playSelBtn);
    bl->addStretch();
    for (QPushButton* b : {m_recordBtn, m_playBtn, m_pauseBtn, m_stopBtn}) bl->addWidget(b);
    outer->addWidget(bottom);
    setCentralWidget(root);

    // The Macro menu mirrors these buttons (same order as created in buildMenus).
    QPushButton* const order[] = {m_recordBtn, m_playBtn, m_playSelBtn, m_pauseBtn, m_stopBtn};
    for (int i = 0; i < m_buttonActions.size() && i < 5; ++i) m_buttonActions[i].second = order[i];

    connect(m_recordBtn, &QPushButton::clicked, this, [this] {
        if (m_state == State::Recording) stopRecording(); else startRecording();
    });
    connect(m_playBtn, &QPushButton::clicked, this, [this] { startPlaying(PlayRange::All); });
    connect(m_playSelBtn, &QPushButton::clicked, this, [this] { startPlaying(PlayRange::Selection); });
    connect(m_pauseBtn, &QPushButton::clicked, this, [this] { togglePause(); });
    connect(m_stopBtn, &QPushButton::clicked, this, [this] { stopAll(); });

    // Hidden names so automated tests can find each control (not shown anywhere).
    const QList<QPair<QObject*, const char*>> ids = {
        {m_recordBtn, "record"}, {m_playBtn, "play"}, {m_pauseBtn, "pause"}, {m_stopBtn, "stop"}, {m_playSelBtn, "playSel"},
        {m_addBtn, "add"}, {m_editBtn, "edit"}, {m_deleteBtn, "delete"}, {m_upBtn, "up"}, {m_downBtn, "down"},
        {m_speed, "speed"}, {m_rTimes, "rTimes"}, {m_rSecs, "rSecs"}, {m_rForever, "rForever"}, {m_repeatTimes, "repeatTimes"},
        {m_repeatSecs, "repeatSecs"}, {m_pathStyle, "pathStyle"}, {m_typing, "typing"}, {m_countdownCheck, "countdown"},
        {m_countdownSecs, "countdownSecs"}, {m_stopOnKey, "stopOnKey"}, {m_stopOnMouse, "stopOnMouse"},
        {m_blockKeys, "blockKeys"}, {m_restoreMouse, "restoreMouse"}, {m_showNotif, "showNotif"}, {m_after, "after"},
        {m_timesUnit, "timesUnit"}, {m_pathDetail, "pathDetail"}, {m_relPos, "relPos"},
        {m_hidePlay, "hidePlay"}, {m_recMoves, "recMoves"}, {m_minMove, "minMove"}, {m_recKeys, "recKeys"},
        {m_mergeTyping, "mergeTyping"}, {m_shortestPause, "shortestPause"}, {m_hideRec, "hideRec"},
        {m_hkRecord, "hkRecord"}, {m_hkPlay, "hkPlay"}, {m_hkStop, "hkStop"}, {m_hkError, "hkError"},
        {m_hkNewRec, "hkNewRec"}, {m_hkPlayFrom, "hkPlayFrom"}, {m_hkPlaySel, "hkPlaySel"}, {m_hkCapture, "hkCapture"},
        {m_table, "table"}, {m_log, "log"}, {m_pill, "pill"}, {m_fileLabel, "fileLabel"}};
    for (const auto& [o, id] : ids) o->setProperty("testId", QString::fromLatin1(id));
    for (auto it = m_filters.cbegin(); it != m_filters.cend(); ++it) it.value()->setProperty("testId", QStringLiteral("filter_") + it.key());
    for (auto it = m_cards.cbegin(); it != m_cards.cend(); ++it) it.value().toggle->setProperty("testId", QStringLiteral("fold_") + it.key());

    for (const QString& name : std::as_const(m_s.foldedCards)) setCardOpen(name, false, false);
    if (!m_s.showLog) setCardOpen(QStringLiteral("ACTIVITY"), false, false);
    updateStretch();
}

QWidget* MainWindow::buildPlaybackCard()
{
    QVBoxLayout* b;
    QFrame* card = makeCard(QStringLiteral("PLAYBACK"), &b);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    int r = 0;
    m_speed = new QSpinBox;
    m_speed->setRange(10, 1000);
    m_speed->setSingleStep(25);
    m_speed->setSuffix(QStringLiteral(" %"));
    m_speed->setValue(m_s.speedPct);
    m_speed->setToolTip(T("100 % plays at the recorded speed; 200 % is twice as fast."));
    grid->addWidget(new QLabel(T("Speed")), r, 0);
    grid->addWidget(m_speed, r++, 1);

    // Repeat: the number in a box, its unit as plain text after it.
    m_rTimes = new QRadioButton;
    m_rSecs = new QRadioButton;
    m_rForever = new QRadioButton(T("Until Stopped"));
    m_repeatTimes = new QSpinBox;
    m_repeatTimes->setRange(1, 1000000);
    m_repeatTimes->setValue(m_s.repeatTimes);
    m_timesUnit = new QLabel(m_s.repeatTimes == 1 ? T("Time", "repeat once") : T("Times", "repeat count"));
    m_repeatSecs = new QSpinBox;
    m_repeatSecs->setRange(1, 864000);
    m_repeatSecs->setValue(m_s.repeatSeconds);
    for (QSpinBox* sb : {m_repeatTimes, m_repeatSecs}) sb->setMinimumWidth(90);
    auto radioRow = [](QRadioButton* rb, QWidget* w, QWidget* unit) {
        auto* x = new QWidget;
        auto* h = new QHBoxLayout(x);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(rb);
        if (w) h->addWidget(w);
        if (unit) h->addWidget(unit);
        h->addStretch();
        return x;
    };
    auto* repeatGroup = new QButtonGroup(card);   // one choice, even though each option sits in its own row
    for (QRadioButton* rb : {m_rTimes, m_rSecs, m_rForever}) repeatGroup->addButton(rb);
    grid->addWidget(new QLabel(T("Repeat")), r, 0);
    grid->addWidget(radioRow(m_rTimes, m_repeatTimes, m_timesUnit), r++, 1);
    grid->addWidget(radioRow(m_rSecs, m_repeatSecs, new QLabel(T("Seconds"))), r++, 1);
    grid->addWidget(radioRow(m_rForever, nullptr, nullptr), r++, 1);
    (m_s.repeatMode == QLatin1String("seconds") ? m_rSecs : m_s.repeatMode == QLatin1String("forever") ? m_rForever : m_rTimes)->setChecked(true);

    m_pathStyle = new QComboBox;
    m_pathStyle->addItem(T("As Recorded"), QStringLiteral("recorded"));
    m_pathStyle->addItem(T("Straight Line"), QStringLiteral("linear"));
    m_pathStyle->addItem(T("Smooth Curve"), QStringLiteral("spline"));
    m_pathStyle->addItem(T("Human-Like"), QStringLiteral("human"));
    m_pathStyle->setCurrentIndex(qMax(0, m_pathStyle->findData(m_s.pathStyle)));
    m_pathStyle->setToolTip(T("How the mouse travels between points. Human-like wobbles and slows down near the target, "
                                           "for programs that notice robotic movement."));
    grid->addWidget(new QLabel(T("Mouse Path")), r, 0);
    grid->addWidget(m_pathStyle, r++, 1);

    m_typing = new QComboBox;
    m_typing->addItem(T("As Recorded"), QStringLiteral("recorded"));
    m_typing->addItem(T("Instant"), QStringLiteral("instant"));
    m_typing->setCurrentIndex(qMax(0, m_typing->findData(m_s.typing)));
    m_typing->setToolTip(T("As recorded keeps your typing rhythm (some programs miss keys typed too fast)."));
    grid->addWidget(new QLabel(T("Typing")), r, 0);
    grid->addWidget(m_typing, r++, 1);

    m_after = new QComboBox;
    m_after->addItem(T("Do Nothing"), QStringLiteral("nothing"));
    m_after->addItem(T("Close App"), QStringLiteral("quit"));
    m_after->addItem(T("Lock Computer"), QStringLiteral("lock"));
    m_after->addItem(T("Sleep"), QStringLiteral("sleep"));
    m_after->addItem(T("Log Off"), QStringLiteral("logoff"));
    m_after->addItem(T("Restart"), QStringLiteral("restart"));
    m_after->addItem(T("Shut Down"), QStringLiteral("shutdown"));
    m_after->setCurrentIndex(qMax(0, m_after->findData(m_s.afterPlayback)));
    m_after->setToolTip(T("After the macro finishes on its own (not when you stop it). Lock, sleep, log off, restart and "
                                       "shut down wait 30 seconds first, so you can cancel."));
    grid->addWidget(new QLabel(T("When Finished")), r, 0);
    grid->addWidget(m_after, r++, 1);
    grid->setColumnStretch(1, 1);
    b->addLayout(grid);

    m_countdownCheck = new QCheckBox(T("Countdown Before Playing"));
    m_countdownCheck->setChecked(m_s.countdown);
    m_countdownSecs = new QSpinBox;
    m_countdownSecs->setRange(1, 60);
    m_countdownSecs->setSuffix(QStringLiteral(" s"));
    m_countdownSecs->setValue(m_s.countdownSeconds);
    auto* cd = new QHBoxLayout;
    cd->addWidget(m_countdownCheck);
    cd->addWidget(m_countdownSecs);
    cd->addStretch();
    b->addLayout(cd);
    m_stopOnKey = new QCheckBox(T("Stop on Key Press"));
    m_stopOnKey->setChecked(m_s.stopOnKey);
    m_stopOnMouse = new QCheckBox(T("Stop on Mouse Move"));
    m_stopOnMouse->setChecked(m_s.stopOnMouse);
    m_blockKeys = new QCheckBox(T("Block Keyboard While Playing"));
    m_blockKeys->setChecked(m_s.blockKeyboard);
    const bool canBlock = m_backend->canBlockKeyboard();
    m_blockKeys->setEnabled(canBlock);
    m_blockKeys->setToolTip(canBlock ? T("Keys you press don't reach other programs while a macro plays. "
                                         "The Stop and Pause hotkeys still work.")
                                     : T("Only available on Windows."));
    m_restoreMouse = new QCheckBox(T("Restore Mouse Position"));
    m_restoreMouse->setChecked(m_s.restoreMouse);
    m_showNotif = new QCheckBox(T("Show Notifications"));
    m_showNotif->setChecked(m_s.showNotifications);
    m_showNotif->setToolTip(T("Notification steps pop up on the desktop. Off: they're only written to the activity log."));
    m_hidePlay = new QCheckBox(T("Minimize While Playing"));
    m_hidePlay->setChecked(m_s.hideWhilePlaying);
    for (QCheckBox* c : {m_stopOnKey, m_stopOnMouse, m_blockKeys, m_restoreMouse, m_showNotif, m_hidePlay}) b->addWidget(c);

    auto changed = [this] {
        m_s.speedPct = m_speed->value();
        m_s.repeatMode = m_rSecs->isChecked() ? QStringLiteral("seconds") : m_rForever->isChecked() ? QStringLiteral("forever") : QStringLiteral("times");
        m_s.repeatTimes = m_repeatTimes->value();
        m_s.repeatSeconds = m_repeatSecs->value();
        m_s.pathStyle = m_pathStyle->currentData().toString();
        m_s.typing = m_typing->currentData().toString();
        m_s.afterPlayback = m_after->currentData().toString();
        m_s.countdown = m_countdownCheck->isChecked();
        m_s.countdownSeconds = m_countdownSecs->value();
        m_s.stopOnKey = m_stopOnKey->isChecked();
        m_s.stopOnMouse = m_stopOnMouse->isChecked();
        m_s.blockKeyboard = m_blockKeys->isChecked();
        m_s.restoreMouse = m_restoreMouse->isChecked();
        m_s.showNotifications = m_showNotif->isChecked();
        m_s.hideWhilePlaying = m_hidePlay->isChecked();
        m_countdownSecs->setEnabled(m_s.countdown);
        m_timesUnit->setText(m_s.repeatTimes == 1 ? T("Time", "repeat once") : T("Times", "repeat count"));
        if (m_state == State::Playing) applyWatch();   // stop / block options apply right away
        saveSettingsSoon();
    };
    for (QSpinBox* sb : {m_speed, m_repeatTimes, m_repeatSecs, m_countdownSecs}) connect(sb, &QSpinBox::valueChanged, this, changed);
    for (QRadioButton* rb : {m_rTimes, m_rSecs, m_rForever}) connect(rb, &QRadioButton::toggled, this, changed);
    for (QComboBox* c : {m_pathStyle, m_typing, m_after}) connect(c, &QComboBox::currentIndexChanged, this, changed);
    for (QCheckBox* c : {m_countdownCheck, m_stopOnKey, m_stopOnMouse, m_blockKeys, m_restoreMouse, m_showNotif, m_hidePlay})
        connect(c, &QCheckBox::toggled, this, changed);
    // Typing a number selects its repeat option.
    connect(m_repeatTimes, &QSpinBox::valueChanged, this, [this] { m_rTimes->setChecked(true); });
    connect(m_repeatSecs, &QSpinBox::valueChanged, this, [this] { m_rSecs->setChecked(true); });
    m_countdownSecs->setEnabled(m_s.countdown);
    return card;
}

QWidget* MainWindow::buildRecordingCard()
{
    QVBoxLayout* b;
    QFrame* card = makeCard(QStringLiteral("RECORDING"), &b);
    m_recMoves = new QCheckBox(T("Mouse Moves"));
    m_recMoves->setChecked(m_s.recordMoves);
    m_recMoves->setToolTip(T("Off: only clicks and scrolls are recorded; the mouse jumps straight to each click."));
    m_minMove = new QSpinBox;
    m_minMove->setRange(1, 100);
    m_minMove->setSuffix(QStringLiteral(" px"));
    m_minMove->setValue(m_s.minMovePx);
    m_minMove->setToolTip(T("Movements smaller than this are left out of recorded paths."));
    auto* smallest = new QLabel(T("Smallest"));
    smallest->setToolTip(m_minMove->toolTip());
    auto* row = new QHBoxLayout;
    row->addWidget(m_recMoves);
    row->addSpacing(6);
    row->addWidget(smallest);
    row->addWidget(m_minMove);
    row->addStretch();
    b->addLayout(row);

    m_pathDetail = new QSlider(Qt::Horizontal);
    m_pathDetail->setRange(0, 100);
    m_pathDetail->setSingleStep(5);
    m_pathDetail->setPageStep(10);
    m_pathDetail->setValue(m_s.pathDetail);
    m_pathDetail->setToolTip(T("How closely mouse paths are kept. 100 % keeps every point; lower leaves out points that "
                                            "barely change a path's shape (smaller files, paths slightly less exact)."));
    m_pathDetailValue = new QLabel(T("%1 %").arg(m_s.pathDetail));
    m_pathDetailValue->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("100 %")) + 4);
    auto* detailLabel = new QLabel(T("Path Detail"));
    detailLabel->setToolTip(m_pathDetail->toolTip());
    auto* rowD = new QHBoxLayout;
    rowD->addWidget(detailLabel);
    rowD->addWidget(m_pathDetail, 1);
    rowD->addWidget(m_pathDetailValue);
    b->addLayout(rowD);

    m_recKeys = new QCheckBox(T("Keyboard"));
    m_recKeys->setChecked(m_s.recordKeyboard);
    m_mergeTyping = new QCheckBox(T("Merge Typing Into Text"));
    m_mergeTyping->setChecked(m_s.mergeTyping);
    m_mergeTyping->setToolTip(T("Typed characters become one Text step instead of one step per key."));
    auto* row2 = new QHBoxLayout;
    row2->addWidget(m_recKeys);
    row2->addWidget(m_mergeTyping);
    row2->addStretch();
    b->addLayout(row2);
    m_shortestPause = new QSpinBox;
    m_shortestPause->setRange(0, 60000);
    m_shortestPause->setSingleStep(100);
    m_shortestPause->setSuffix(QStringLiteral(" ms"));
    m_shortestPause->setValue(m_s.shortestPauseMs);
    m_shortestPause->setToolTip(T("Pauses at least this long become their own Wait steps you can edit. "
                                               "Shorter pauses are kept with the next step, so timing stays exact."));
    auto* row3 = new QHBoxLayout;
    row3->addWidget(new QLabel(T("Show Pauses From")));
    row3->addWidget(m_shortestPause);
    row3->addStretch();
    b->addLayout(row3);
    m_relPos = new QCheckBox(T("Positions Relative to Window"));
    m_relPos->setChecked(m_s.relativePositions);
    m_relPos->setToolTip(T("Clicks are recorded from the window's top-left corner instead of the screen's, so the macro "
                                        "still works when the window is somewhere else."));
    m_smartClicks = new QCheckBox(T("Find Clicked Buttons by Picture"));
    m_smartClicks->setChecked(m_s.smartClicks);
    m_smartClicks->setProperty("testId", QStringLiteral("smartClicks"));
    m_smartClicks->setToolTip(T("Keeps a small picture of what each click lands on. Playing, the click looks for that "
                                             "picture first and clicks it wherever it is now (a window that moved, a list that "
                                             "scrolled); if it isn't on the screen, it clicks where it was recorded."));
    m_hideRec = new QCheckBox(T("Minimize While Recording"));
    m_hideRec->setChecked(m_s.hideWhileRecording);
    b->addWidget(m_relPos);
    b->addWidget(m_smartClicks);
    b->addWidget(m_hideRec);
    auto changed = [this] {
        m_s.smartClicks = m_smartClicks->isChecked();
        m_s.recordMoves = m_recMoves->isChecked();
        m_s.minMovePx = m_minMove->value();
        m_s.pathDetail = m_pathDetail->value();
        m_s.recordKeyboard = m_recKeys->isChecked();
        m_s.mergeTyping = m_mergeTyping->isChecked();
        m_s.shortestPauseMs = m_shortestPause->value();
        m_s.relativePositions = m_relPos->isChecked();
        m_s.hideWhileRecording = m_hideRec->isChecked();
        m_mergeTyping->setEnabled(m_s.recordKeyboard);
        m_pathDetailValue->setText(T("%1 %").arg(m_s.pathDetail));
        saveSettingsSoon();
    };
    for (QCheckBox* c : {m_recMoves, m_recKeys, m_mergeTyping, m_relPos, m_smartClicks, m_hideRec}) connect(c, &QCheckBox::toggled, this, changed);
    for (QSpinBox* sb : {m_minMove, m_shortestPause}) connect(sb, &QSpinBox::valueChanged, this, changed);
    connect(m_pathDetail, &QSlider::valueChanged, this, changed);
    connect(m_minMove, &QSpinBox::valueChanged, this, [this] { m_recMoves->setChecked(true); });
    m_mergeTyping->setEnabled(m_s.recordKeyboard);
    return card;
}

QWidget* MainWindow::buildHotkeysCard()
{
    QVBoxLayout* b;
    QFrame* card = makeCard(QStringLiteral("HOTKEYS"), &b);
    auto* grid = new QGridLayout;
    auto make = [&](const QString& text, const QString& value, const QString& tip, int row) {
        auto* e = new QKeySequenceEdit(QKeySequence(value));
        e->setMaximumSequenceLength(1);
        e->setClearButtonEnabled(true);
        e->setToolTip(tip);
        auto* l = new QLabel(text);
        l->setToolTip(tip);
        grid->addWidget(l, row, 0);
        grid->addWidget(e, row, 1);
        return e;
    };
    const QString anywhere = T("Works from any program.");
    m_hkRecord = make(T("Record / Stop"), m_s.hkRecord, anywhere + QLatin1Char(' ') + T("Records at the end of the list."), 0);
    m_hkNewRec = make(T("New Recording"), m_s.hkNewRecording, anywhere + QLatin1Char(' ') + T("Starts a new macro (asks to save changes first) and records."), 1);
    m_hkPlay = make(T("Play / Pause"), m_s.hkPlay, anywhere, 2);
    m_hkPlayFrom = make(T("Play From Selected"), m_s.hkPlayFrom, anywhere + QLatin1Char(' ') + T("Plays from the selected step to the end."), 3);
    m_hkPlaySel = make(T("Play Selection"), m_s.hkPlaySelection, anywhere + QLatin1Char(' ') + T("Plays only the selected steps."), 4);
    m_hkStop = make(T("Stop"), m_s.hkStop, T("Active only while recording or playing, so the key keeps working normally otherwise."), 5);
    m_hkCapture = make(T("Capture Position"), m_s.hkCapture,
                       T("While a step's editor is open: point at a spot and press this to take the mouse position "
                         "(and the color, for Wait for pixel color). Active only then."), 6);
    grid->setColumnStretch(1, 1);
    b->addLayout(grid);
    m_hkError = new QLabel;
    m_hkError->setProperty("tone", QStringLiteral("warn"));
    m_hkError->setWordWrap(true);
    m_hkError->hide();
    b->addWidget(m_hkError);
    b->addWidget(muted(T("Hotkeys are never recorded into macros.")));
    for (QKeySequenceEdit* e : {m_hkRecord, m_hkNewRec, m_hkPlay, m_hkPlayFrom, m_hkPlaySel, m_hkStop, m_hkCapture}) {
        connect(e, &QKeySequenceEdit::editingFinished, this, [this] {
            auto text = [](QKeySequenceEdit* x) { return x->keySequence().toString(QKeySequence::PortableText); };
            m_s.hkRecord = text(m_hkRecord);
            m_s.hkNewRecording = text(m_hkNewRec);
            m_s.hkPlay = text(m_hkPlay);
            m_s.hkPlayFrom = text(m_hkPlayFrom);
            m_s.hkPlaySelection = text(m_hkPlaySel);
            m_s.hkStop = text(m_hkStop);
            m_s.hkCapture = text(m_hkCapture);
            registerHotkeys();
            saveSettingsSoon();
        });
        connect(e, &QKeySequenceEdit::keySequenceChanged, this, [e](const QKeySequence& k) {
            if (k.isEmpty()) emit e->editingFinished();
        });
    }
    return card;
}

QWidget* MainWindow::buildActionsCard()
{
    m_addBtn = toolButton(T("+ Add"), T("Add a step after the selected one (or at the end)."));
    m_addBtn->setMenu(buildAddMenu(m_addBtn));
    m_addBtn->setPopupMode(QToolButton::InstantPopup);
    QVBoxLayout* b;
    QFrame* card = makeCard(QStringLiteral("ACTIONS"), &b);
    auto* bar = new QHBoxLayout;
    m_editBtn = toolButton(T("Edit", "button"), T("Edit the selected step (or double-click it)."));
    m_deleteBtn = toolButton(T("Delete", "button"), T("Delete the selected steps."));
    m_upBtn = toolButton(QStringLiteral("↑"), T("Move up (Alt+Up)"));
    m_downBtn = toolButton(QStringLiteral("↓"), T("Move down (Alt+Down)"));
    for (QToolButton* t : {m_addBtn, m_editBtn, m_deleteBtn, m_upBtn, m_downBtn}) bar->addWidget(t);
    bar->addSpacing(12);
    bar->addWidget(new QLabel(T("Show:")));
    const QList<QPair<QString, QString>> filters = {{QStringLiteral("moves"), T("Moves", "filter")},
                                                    {QStringLiteral("clicks"), T("Clicks")},
                                                    {QStringLiteral("keys"), T("Keys")},
                                                    {QStringLiteral("waits"), T("Waits")},
                                                    {QStringLiteral("windows"), T("Windows", "filter")}};
    for (const auto& [key, text] : filters) {
        auto* c = new QCheckBox(text);
        c->setChecked(true);
        c->setToolTip(T("Hidden steps still play."));
        m_filters.insert(key, c);
        bar->addWidget(c);
        connect(c, &QCheckBox::toggled, this, [this] { applyFilter(); });
    }
    bar->addStretch();
    b->addLayout(bar);

    m_table = new ActionTable;
    m_table->setModel(&m_model);
    m_preview = new ActionPreview(m_table->viewport());
    connect(m_table, &ActionTable::hoveredRow, this, [this](int row) { showPreview(row); });
    connect(m_table, &ActionTable::groupToggled, this, [this](int row, bool fold) {
        m_model.setCollapsed(row, fold);
        applyFilter();
    });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](QPoint p) {
        if (QMenu* m = stepMenu(p)) {
            m->setAttribute(Qt::WA_DeleteOnClose);
            m->popup(m_table->viewport()->mapToGlobal(p));
        }
    });
    connect(&m_model, &QAbstractItemModel::modelReset, this, [this] { showPreview(-1); });
    connect(&m_model, &QAbstractItemModel::layoutChanged, this, [this] { showPreview(-1); });
    connect(&m_model, &QAbstractItemModel::rowsRemoved, this, [this] { showPreview(-1); });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);   // click-and-drag, Shift+click, Ctrl+click
    m_table->setDragEnabled(false);
    m_table->setAutoScroll(true);                                       // dragging past the edge scrolls
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(26);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->setWordWrap(false);
    auto* h = m_table->horizontalHeader();
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setStretchLastSection(true);
    h->resizeSection(ActionModel::ColNum, 48);
    h->resizeSection(ActionModel::ColAction, 200);
    h->resizeSection(ActionModel::ColValue, 360);
    h->resizeSection(ActionModel::ColLabel, 110);
    m_fittingColumns = true;
    if (!m_s.tableState.isEmpty()) h->restoreState(m_s.tableState);
    // The Action column fits the longest step name in the app's language (Spanish names run longer), unless the
    // columns were sized by hand
    if (m_s.fitValueColumn) {
        int longest = 0;
        for (int t = 0; t <= int(core::kLastActionType); ++t) {
            Action a;
            a.type = ActionType(t);
            for (int variant = 0; variant < 3; ++variant) {   // the names that change with a step's settings
                a.clicks = variant == 1 ? 2 : 1;
                a.waitGone = variant == 2;
                a.findDo = variant == 1 ? core::FindDo::DoubleClick : core::FindDo::Nothing;
                longest = qMax(longest, m_table->fontMetrics().horizontalAdvance(a.typeName()));
            }
        }
        const int want = longest + 18 + 30;   // the icon and the cell's padding
        if (h->sectionSize(ActionModel::ColAction) < want) h->resizeSection(ActionModel::ColAction, want);
    }
    m_fittingColumns = false;
    connect(h, &QHeaderView::sectionResized, this, [this](int logical) {
        // A column dragged by hand (not the last one, which only fills the rest): the user's widths from now on
        if (!m_fittingColumns && logical != ActionModel::ColComment && m_s.fitValueColumn) m_s.fitValueColumn = false;
        m_s.tableState = m_table->horizontalHeader()->saveState();
        saveSettingsSoon();
    });
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& i) {
        if (i.column() != ActionModel::ColLabel && i.column() != ActionModel::ColComment) editRow(i.row());
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateUi(); updateMarker(); });
    b->addWidget(m_table, 1);

    connect(m_editBtn, &QToolButton::clicked, this, [this] {
        const auto rows = selectedRows();
        if (!rows.isEmpty()) editRow(rows.first());
    });
    connect(m_deleteBtn, &QToolButton::clicked, this, [this] { deleteSelected(); });
    connect(m_upBtn, &QToolButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_downBtn, &QToolButton::clicked, this, [this] { moveSelected(1); });
    return card;
}

QWidget* MainWindow::buildActivityCard()
{
    QVBoxLayout* b;
    QFrame* card = makeCard(QStringLiteral("ACTIVITY"), &b);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setMinimumHeight(90);
    b->addWidget(m_log, 1);
    return card;
}

// ------------------------------------------------------------------------------------------------ state

void MainWindow::setPill(const QString& text, const QString& tone)
{
    m_pill->setText(text);
    m_pill->setProperty("tone", tone);
    m_pill->style()->unpolish(m_pill);
    m_pill->style()->polish(m_pill);
}

void MainWindow::setState(State s)
{
    m_state = s;
    const QString why = m_backend->unavailableReason();
    switch (s) {
    case State::Idle:
        if (!why.isEmpty()) setPill(T("● Can't Record Here"), QStringLiteral("warn"));
        else if (!m_backend->recordUnavailableReason().isEmpty()) setPill(T("● Ready (Playback Only)"), QStringLiteral("warn"));
        else if (!m_backend->limitedTag().isEmpty())
            setPill(T("● Ready (%1)").arg(m_backend->limitedTag()), QStringLiteral("warn"));
        else setPill(T("● Ready"), QStringLiteral("idle"));
        break;
    case State::Countdown: break;
    case State::Recording: setPill(T("● Recording"), QStringLiteral("err")); break;
    case State::Playing: setPill(T("▶ Playing"), QStringLiteral("ok")); break;
    }
    updateUi();
    applyOpacity();
}

void MainWindow::updateUi()
{
    const bool idle = m_state == State::Idle;
    const bool busy = m_state == State::Playing || m_state == State::Countdown;
    const QString why = m_backend->unavailableReason();
    const QString recWhy = m_backend->recordUnavailableReason();
    const bool canRun = why.isEmpty();
    const bool canRecord = recWhy.isEmpty();
    const bool hasActions = m_model.rowCount() > 0;
    const bool hasSel = !selectedRows().isEmpty();

    m_recordBtn->setText(m_state == State::Recording ? T("■ Stop Recording") : T("● Record"));
    m_recordBtn->setEnabled(canRecord && (idle || m_state == State::Recording));
    m_recordBtn->setToolTip(!canRecord ? recWhy
                            : m_state == State::Recording ? T("Stop recording (%1)").arg(m_s.hkStop)
                            : busy ? T("Stop playback first.")
                                   : T("Record your mouse and keyboard, added to the end of the list (%1).").arg(m_s.hkRecord));
    m_playBtn->setEnabled(canRun && idle && hasActions);
    m_playBtn->setToolTip(!canRun ? why : !hasActions ? T("Record or add some steps first.")
                                        : !idle ? T("Already busy.") : T("Play the whole macro (%1).").arg(m_s.hkPlay));
    m_playSelBtn->setEnabled(canRun && idle && hasSel);
    m_playSelBtn->setToolTip(!canRun ? why : !hasSel ? T("Select the steps to play first.") : T("Play only the selected steps."));
    m_pauseBtn->setEnabled(m_state == State::Playing);
    m_pauseBtn->setText(m_player->paused() ? T("▶ Resume") : T("❚❚ Pause"));
    m_pauseBtn->setToolTip(m_state == State::Playing ? T("Pause or resume playback (%1).").arg(m_s.hkPlay) : T("Only while playing."));
    m_stopBtn->setEnabled(!idle);
    m_stopBtn->setToolTip(idle ? T("Nothing is running.") : T("Stop (%1).").arg(m_s.hkStop));

    for (QToolButton* t : {m_addBtn}) t->setEnabled(idle);
    for (QToolButton* t : {m_editBtn, m_deleteBtn, m_upBtn, m_downBtn}) {
        t->setEnabled(idle && hasSel);
    }
    for (QAction* a : std::as_const(m_editActs)) a->setEnabled(idle && hasSel);
    m_undoAct->setEnabled(idle && !m_undo.isEmpty());
    m_redoAct->setEnabled(idle && !m_redo.isEmpty());
    m_table->setEditTriggers(idle ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed : QAbstractItemView::NoEditTriggers);
    updateMarker();
    for (QWidget* w : {static_cast<QWidget*>(m_speed), static_cast<QWidget*>(m_pathStyle), static_cast<QWidget*>(m_typing),
                       static_cast<QWidget*>(m_rTimes), static_cast<QWidget*>(m_rSecs), static_cast<QWidget*>(m_rForever),
                       static_cast<QWidget*>(m_repeatTimes), static_cast<QWidget*>(m_repeatSecs)}) {
        if (!w->property("baseTip").isValid()) w->setProperty("baseTip", w->toolTip());
        w->setEnabled(!busy);
        w->setToolTip(busy ? T("Can't be changed while playing. Stop first.") : w->property("baseTip").toString());
    }
    updateHeader();
}

void MainWindow::updateHeader()
{
    const QString name = m_file.isEmpty() ? T("Untitled") : QFileInfo(m_file).fileName();
    core::Macro m;
    m.actions = m_model.actions();
    const int n = m_model.rowCount();
    const QString count = n == 1 ? T("1 action") : T("%1 actions").arg(QLocale().toString(n));
    m_fileLabel->setText(QStringLiteral("%1%2  ·  %3  ·  %4").arg(name, m_dirty ? QStringLiteral("*") : QString(), count, seconds(m.totalMs())));
    setWindowTitle(QStringLiteral("%1%2 – " APP_NAME).arg(name, m_dirty ? QStringLiteral("*") : QString()));
}

void MainWindow::log(const QString& text, bool detailed)
{
    if (detailed && !m_s.detailedLog) return;
    m_log->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + text);
}

void MainWindow::saveSettingsSoon() { m_saveTimer.start(); }

void MainWindow::applyTheme()
{
    QWidget* c = centralWidget();
    if (c && !c->styleSheet().isEmpty()) {  // a preview was showing: clear the per-widget override
        c->setStyleSheet(QString());
        c->setPalette(QPalette());
    }
    themes::apply(themes::forChoice(m_s.theme));
}

QAction* MainWindow::addThemeAction(QMenu* menu, const QString& text, const QString& key)
{
    QAction* a = menu->addAction(text);
    a->setCheckable(true);
    a->setChecked(m_s.theme == key);
    a->setData(key);
    m_themeGroup->addAction(a);
    connect(a, &QAction::hovered, this, [this, key] { previewTheme(key); });
    connect(a, &QAction::triggered, this, [this, key] {
        m_s.theme = key;
        saveSettingsSoon();
        applyTheme();
    });
    return a;
}

void MainWindow::rebuildCustomThemeMenus()
{
    for (QAction* a : m_customThemeMenu->actions()) {
        m_themeGroup->removeAction(a);
        delete a;
    }
    m_removeThemeMenu->clear();
    QList<themes::Theme> list;
    for (const themes::Theme& t : themes::all())
        if (t.group == themes::customGroup()) list << t;
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    for (const themes::Theme& t : list) {
        addThemeAction(m_customThemeMenu, t.name, t.key);
        const QString key = t.key;
        m_removeThemeMenu->addAction(t.name, this, [this, key] { removeCustomTheme(key); });
    }
    m_customThemeMenu->menuAction()->setVisible(!list.isEmpty());
    m_removeThemeMenu->menuAction()->setEnabled(!list.isEmpty());
}

void MainWindow::importTheme()
{
    const QString path = QFileDialog::getOpenFileName(this, T("Import Theme"), m_s.lastFolder,
                                                      T("Theme Files (*.json);;All Files (*)"), nullptr,
                                                      filedialogs::options());
    if (path.isEmpty()) return;
    QString error;
    if (!importThemeFile(path, &error))
        QMessageBox::warning(this, T("Couldn't Import the Theme"),
                             T("%1 can't be used as a theme.\n\n%2").arg(QFileInfo(path).fileName(), error));
}

bool MainWindow::importThemeFile(const QString& path, QString* error)
{
    const themefile::Result r = themefile::read(path);
    if (!r.ok()) {
        if (error) *error = r.error;
        log(T("Couldn't import the theme %1: %2").arg(QDir::toNativeSeparators(path), r.error));
        return false;
    }
    // A custom theme with the same name is replaced (importing an edited file again updates it).
    QStringList kept;
    bool replaced = false;
    for (const QString& stored : m_s.customThemes) {
        const themefile::Result old = themefile::parse(stored.toUtf8(), QString());
        if (old.ok() && old.theme.key == r.theme.key) { replaced = true; continue; }
        kept << stored;
    }
    kept << QString::fromUtf8(themefile::write(r.theme, false));
    m_s.customThemes = kept;
    themes::setCustom(themefile::fromStored(m_s.customThemes));
    rebuildCustomThemeMenus();
    m_s.theme = r.theme.key;
    for (QAction* a : m_themeGroup->actions()) a->setChecked(a->data().toString() == m_s.theme);
    applyTheme();
    saveSettingsSoon();
    log(replaced ? T("Updated the custom theme \u201C%1\u201D.").arg(r.theme.name)
                 : T("Imported the theme \u201C%1\u201D. It's in View > Theme > Custom Themes.").arg(r.theme.name));
    for (const QString& note : r.notes) log(note);
    if (appdata::isTemporary())
        log(T("Don't Keep App Data is on, so the imported theme will be gone the next time the app opens."));
    return true;
}

void MainWindow::exportTheme()
{
    const themes::Theme t = themes::forChoice(m_s.theme);
    QString fileName = t.name;
    for (QChar& ch : fileName)
        if (QStringLiteral("\\/:*?\"<>|").contains(ch)) ch = QLatin1Char('_');
    const QString folder = m_s.lastFolder.isEmpty() ? QDir::homePath() : m_s.lastFolder;
    const QString path = QFileDialog::getSaveFileName(this, T("Export Current Theme"),
                                                      QDir(folder).filePath(fileName + QStringLiteral(".json")),
                                                      T("Theme Files (*.json)"), nullptr, filedialogs::options());
    if (path.isEmpty()) return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(themefile::write(t)) < 0 || !f.commit()) {
        QMessageBox::warning(this, T("Couldn't Export the Theme"),
                             T("%1 couldn't be saved: %2").arg(QDir::toNativeSeparators(path), f.errorString()));
        return;
    }
    log(T("Exported the theme \u201C%1\u201D to %2. Change its colors and import it to make a theme of your own.")
            .arg(t.name, QDir::toNativeSeparators(path)));
}

void MainWindow::removeCustomTheme(const QString& key)
{
    QString name;
    QStringList kept;
    for (const QString& stored : m_s.customThemes) {
        const themefile::Result r = themefile::parse(stored.toUtf8(), QString());
        if (r.ok() && r.theme.key == key) { name = r.theme.name; continue; }
        kept << stored;
    }
    m_s.customThemes = kept;
    themes::setCustom(themefile::fromStored(m_s.customThemes));
    rebuildCustomThemeMenus();
    if (m_s.theme == key) m_s.theme = Settings().theme;
    for (QAction* a : m_themeGroup->actions()) a->setChecked(a->data().toString() == m_s.theme);
    applyTheme();
    saveSettingsSoon();
    log(T("Removed the custom theme \u201C%1\u201D.").arg(name));
}

// Every other window of the app gets the main window's opacity too: menus, drop-down lists, dialogs, message
// boxes, tooltips and the on-screen position marker.
bool MainWindow::followsOpacity(const QWidget* w) const
{
    // The screen-area picker shows a still picture of the screen: see-through, it would look doubled
    return w && w != this && w->isWindow() && !w->property("solidOverlay").toBool();
}

void MainWindow::applyOpacity()
{
    const double level = m_s.windowOpacity / 100.0;   // View > Window Opacity; the same in every state
    filedialogs::opacityPercent() = m_s.windowOpacity;   // file dialogs: the app's own while see-through
    if (qAbs(windowOpacity() - level) > 0.01) setWindowOpacity(level);
    // A menu opened by a click (or a tooltip fading in) plays an animation, which draws it solid. So below 100 %
    // the menu, drop-down and tooltip animations are off; at 100 % the system's own settings are back.
    const bool solid = level >= 0.995;
    for (auto it = m_effectsDefault.cbegin(); it != m_effectsDefault.cend(); ++it) {
        const bool want = solid && it.value();
        if (QApplication::isEffectEnabled(Qt::UIEffect(it.key())) != want)
            QApplication::setEffectEnabled(Qt::UIEffect(it.key()), want);
    }
    // Every menu, drop-down list and dialog, open or not: one that has its level before it opens shows it from the
    // start (and open ones change at once, as the slider sits in a menu).
    QList<QWidget*> targets = QApplication::topLevelWidgets();
    for (QMenu* m : findChildren<QMenu*>()) targets << m;
    for (QWidget* w : targets)
        if (followsOpacity(w) && qAbs(w->windowOpacity() - level) > 0.01) w->setWindowOpacity(level);
}

void MainWindow::fitValueColumn()
{
    if (!m_s.fitValueColumn || !m_table) return;
    QHeaderView* h = m_table->horizontalHeader();
    // The Value column takes a wide window's extra room, so long values aren't cut while the list's right side is
    // empty. The Comment column keeps 400 px: room for comments and for the hover preview drawn there.
    const int others = h->sectionSize(ActionModel::ColNum) + h->sectionSize(ActionModel::ColAction) + h->sectionSize(ActionModel::ColLabel);
    const int want = qMax(360, m_table->viewport()->width() - others - 400);
    if (h->sectionSize(ActionModel::ColValue) == want) return;
    m_fittingColumns = true;
    h->resizeSection(ActionModel::ColValue, want);
    m_fittingColumns = false;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e)
{
    if (e->type() == QEvent::Resize && m_table && watched == m_table->viewport()) fitValueColumn();
    // Polish: a popup made later (a new context menu or drop-down list) gets the level before it's first shown
    if ((e->type() == QEvent::Show || e->type() == QEvent::Polish) && watched->isWidgetType()) {
        auto* w = static_cast<QWidget*>(watched);
        if (followsOpacity(w)) w->setWindowOpacity(m_s.windowOpacity / 100.0);
    }
    return QMainWindow::eventFilter(watched, e);
}

void MainWindow::previewTheme(const QString& key)
{
    m_themePreviewing = key;
    m_themePreviewTimer.start();
}

void MainWindow::endThemePreview()
{
    // Qt closes the menu before it reports the click, so restore on the next event-loop turn; by then a click
    // has saved the new theme and applyTheme() applies whichever is saved.
    m_themePreviewTimer.stop();
    if (m_themePreviewing.isEmpty()) return;
    m_themePreviewing.clear();
    QTimer::singleShot(0, this, [this] { applyTheme(); });
}

// ------------------------------------------------------------------------------------------------ files

void MainWindow::setDirty(bool d)
{
    m_dirty = d;
    updateHeader();
}

bool MainWindow::maybeSave()
{
    if (!m_dirty || m_model.rowCount() == 0) return true;
    const auto r = QMessageBox::question(this, T("Unsaved Changes"),
                                         m_file.isEmpty() ? T("Save the changes to this macro?") : T("Save the changes to %1?").arg(QFileInfo(m_file).fileName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save(false);
    return true;
}

bool MainWindow::save(bool saveAs)
{
    QString path = m_file;
    if (saveAs || path.isEmpty()) {
        const QString start = m_file.isEmpty() ? QDir(m_s.lastFolder.isEmpty() ? QDir::homePath() : m_s.lastFolder).filePath(T("Untitled") + QLatin1Char('.') + kExt) : m_file;
        path = QFileDialog::getSaveFileName(this, T("Save Macro"), start, T("Macros (*.%1)").arg(kExt),
                                            nullptr, filedialogs::options());
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += QLatin1Char('.') + kExt;
    }
    core::Macro m;
    m.actions = m_model.actions();
    QString err;
    if (!m.save(path, &err)) {
        QMessageBox::warning(this, T("Couldn't Save"), T("Couldn't save %1:\n%2").arg(path, err));
        return false;
    }
    m_file = path;
    m_s.lastFolder = QFileInfo(path).absolutePath();
    addRecent(path);
    setDirty(false);
    const QString saved = QFileInfo(path).fileName();
    log(m.actions.size() == 1 ? T("Saved %1 (1 action).").arg(saved)
                              : T("Saved %1 (%2 actions).").arg(saved, QLocale().toString(m.actions.size())));
    return true;
}

void MainWindow::newMacro()
{
    if (m_state != State::Idle || !maybeSave()) return;
    snapshot();
    m_model.setActions({});
    m_file.clear();
    setDirty(false);
    updateUi();
}

void MainWindow::open()
{
    if (m_state != State::Idle || !maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, T("Open Macro"),
                                                      m_s.lastFolder.isEmpty() ? QDir::homePath() : m_s.lastFolder,
                                                      T("Macros (*.%1);;All files (*)").arg(kExt), nullptr,
                                                      filedialogs::options());
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, T("Not Found"), T("%1 doesn't exist any more.").arg(path));
        m_s.recent.removeAll(path);
        rebuildRecentMenu();
        return false;
    }
    core::Macro m;
    QString err;
    if (!core::Macro::load(path, &m, &err)) {
        QMessageBox::warning(this, T("Couldn't Open"), T("Couldn't open %1:\n%2").arg(path, err));
        return false;
    }
    if (!err.isEmpty()) log(err);
    m_undo.clear();
    m_redo.clear();
    m_model.setActions(m.actions);
    m_file = path;
    m_s.lastFolder = QFileInfo(path).absolutePath();
    addRecent(path);
    setDirty(false);
    applyFilter();
    const QString opened = QFileInfo(path).fileName();
    log(m.actions.size() == 1 ? T("Opened %1 (1 action).").arg(opened)
                              : T("Opened %1 (%2 actions).").arg(opened, QLocale().toString(m.actions.size())));
    updateUi();
    return true;
}

void MainWindow::addRecent(const QString& path)
{
    m_s.recent.removeAll(path);
    m_s.recent.prepend(path);
    while (m_s.recent.size() > 10) m_s.recent.removeLast();
    rebuildRecentMenu();
    saveSettingsSoon();
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    for (const QString& p : std::as_const(m_s.recent)) {
        m_recentMenu->addAction(QDir::toNativeSeparators(p), this, [this, p] {
            if (m_state == State::Idle && maybeSave()) openFile(p);
        });
    }
    m_recentMenu->setEnabled(!m_s.recent.isEmpty());
}

// ------------------------------------------------------------------------------------------------ editing

void MainWindow::snapshot()
{
    m_undo.append(m_model.actions());
    if (m_undo.size() > 200) m_undo.removeFirst();
    m_redo.clear();
}

void MainWindow::undo()
{
    if (m_undo.isEmpty() || m_state != State::Idle) return;
    m_redo.append(m_model.actions());
    m_model.setActions(m_undo.takeLast());
    setDirty(true);
    applyFilter();
    updateUi();
}

void MainWindow::redo()
{
    if (m_redo.isEmpty() || m_state != State::Idle) return;
    m_undo.append(m_model.actions());
    m_model.setActions(m_redo.takeLast());
    setDirty(true);
    applyFilter();
    updateUi();
}

QList<int> MainWindow::selectedRows() const
{
    QList<int> rows;
    if (!m_table || !m_table->selectionModel()) return rows;
    for (const QModelIndex& i : m_table->selectionModel()->selectedRows()) rows << i.row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

int MainWindow::insertRow() const
{
    const auto rows = selectedRows();
    return rows.isEmpty() ? m_model.rowCount() : rows.last() + 1;
}

QStringList MainWindow::labels() const
{
    QStringList l;
    for (const Action& a : m_model.actions())
        if (!a.label.isEmpty() && !l.contains(a.label)) l << a.label;
    return l;
}

void MainWindow::selectRows(int first, int last)
{
    m_table->clearSelection();
    if (first < 0 || last < first) return;
    const QItemSelection sel(m_model.index(first, 0), m_model.index(last, ActionModel::ColCount - 1));
    m_table->selectionModel()->select(sel, QItemSelectionModel::Select);
    m_table->scrollTo(m_model.index(first, 0));
}

void MainWindow::addAction(ActionType t, int variant)
{
    if (m_state != State::Idle) return;
    Action a;
    a.type = t;
    const QPoint cur = m_backend->injector()->cursor();
    a.pos = cur;
    a.from = cur;
    switch (t) {
    case ActionType::Wait: a.ms = 1000; break;
    case ActionType::Wheel: a.notches = -1; break;
    case ActionType::WindowFocus: a.ms = 10000; break;
    case ActionType::WindowMove: a.ms = 5000; break;
    case ActionType::WaitPixel: a.ms = 30000; break;
    case ActionType::WaitFile: a.ms = 60000; break;
    case ActionType::Repeat: a.count = 2; break;
    case ActionType::KeyPress: case ActionType::KeyDown: case ActionType::KeyUp: case ActionType::WaitKey: a.key = QStringLiteral("Enter"); break;
    case ActionType::AskText: a.var = T("answer", "variable name"); break;
    case ActionType::AskAI: a.var = T("answer", "variable name"); a.ms = 60000; break;
    case ActionType::FindText:
        if (variant == kReadText) {
            a.var = T("screen_text", "variable name");
        } else if (variant == kWaitForImage) {
            a.ms = 30000;
            a.stopIfMissing = true;
        } else {
            a.findDo = core::FindDo(qBound(0, variant, int(core::FindDo::RightClick)));
            a.ms = a.findDo == core::FindDo::Nothing ? 0 : 10000;
            a.stopIfMissing = a.findDo != core::FindDo::Nothing;
        }
        break;
    case ActionType::Group: a.label = T("Group %1").arg(groupCount() + 1); break;
    case ActionType::Screenshot: a.file = T("Screenshot {=now(\"yyyy-MM-dd HH.mm.ss\")}.png"); break;
    case ActionType::FindImage:
        a.tolerance = 10;
        if (variant == kWaitForImage) {   // Wait for Image: until it appears, or stop
            a.ms = 30000;
            a.stopIfMissing = true;
        } else {
            a.findDo = core::FindDo(qBound(0, variant, int(core::FindDo::RightClick)));
            a.ms = a.findDo == core::FindDo::Nothing ? 0 : 10000;   // clicking: wait a little for it, and never click elsewhere
            a.stopIfMissing = a.findDo != core::FindDo::Nothing;
        }
        break;
    default: break;
    }
    ActionDialog d(a, labels(), m_backend->injector(), this);
    d.setWindowTitle(T("Add: %1").arg(a.typeName()));
    if (execDialog(d) != QDialog::Accepted) return;
    snapshot();
    const auto rows = selectedRows();
    if ((t == ActionType::If || t == ActionType::Group) && !rows.isEmpty()) {
        // Around the selected steps: the block starts before the first and ends after the last
        Action end;
        end.type = t == ActionType::If ? ActionType::EndIf : ActionType::EndGroup;
        m_model.insert(rows.last() + 1, end);
        m_model.insert(rows.first(), d.result());
        selectRows(rows.first(), rows.last() + 2);
        log(rows.size() == 1 ? T("%1 added around 1 step.").arg(d.result().typeName())
                             : T("%1 added around %2 steps.").arg(d.result().typeName(), QLocale().toString(rows.size())));
    } else {
        const int row = insertRow();
        m_model.insert(row, d.result());
        if (t == ActionType::If || t == ActionType::Group) {   // a block comes with its end
            Action end;
            end.type = t == ActionType::If ? ActionType::EndIf : ActionType::EndGroup;
            m_model.insert(row + 1, end);
        }
        selectRows(row, row);
    }
    setDirty(true);
    applyFilter();
    updateUi();
}

QMenu* MainWindow::stepMenu(QPoint viewportPos)
{
    if (m_state != State::Idle) return nullptr;
    // The main menu's own items, found by their text (in the app's language; the & of a shortcut letter ignored)
    auto named = [this](QString text) -> QAction* {
        text.remove(QLatin1Char('&'));
        for (QAction* a : findChildren<QAction*>()) {
            QString t = a->text();
            t.remove(QLatin1Char('&'));
            if (!a->menu() && t == text) return a;
        }
        return nullptr;
    };
    const int row = m_table->rowAt(viewportPos.y());
    // The step right-clicked becomes the chosen one, unless it's already part of the selection (File Explorer)
    if (row >= 0 && !m_table->selectionModel()->isRowSelected(row, {})) selectRows(row, row);
    if (row < 0 && !selectedRows().isEmpty()) m_table->clearSelection();
    updateUi();
    auto* menu = new QMenu(this);
    menu->setProperty("testId", QStringLiteral("stepMenu"));
    auto add = [&](const QString& text) {
        if (QAction* a = named(text)) menu->addAction(a);
    };
    if (row >= 0) {
        add(T("&Edit Step…"));
        if (m_model.at(row).type == ActionType::Group) {
            const bool folded = m_model.at(row).collapsed;
            menu->addAction(folded ? T("Show Group's Steps") : T("Fold Group"), this, [this, row, folded] {
                m_model.setCollapsed(row, !folded);
                applyFilter();
            });
        }
        menu->addSeparator();
        add(T("Play This Step &Only"));
        add(T("Play &Selection"));
        add(T("Play From Selected &Step"));
        add(T("Play &Up to Selected Step"));
        add(T("Record After Selected Step"));
        menu->addSeparator();
        add(T("Cu&t"));
        add(T("&Copy"));
        add(T("&Paste"));
        add(T("&Delete"));
        menu->addSeparator();
        add(T("E&nable / Disable"));
        add(T("Move &Up"));
        add(T("Move Do&wn"));
        add(T("&Group Selected Steps"));
        if (m_model.at(row).type == ActionType::Group) add(T("U&ngroup"));
    } else {
        add(T("&Paste"));
        add(T("Select &All"));
    }
    menu->addSeparator();
    QMenu* addMenu = buildAddMenu(menu);
    addMenu->setTitle(T("Add Action"));
    menu->addMenu(addMenu);
    // The Play Selection button's state decides that item (it mirrors the button)
    for (auto& [act, btn] : m_buttonActions) act->setEnabled(btn && btn->isEnabled());
    return menu;
}

int MainWindow::groupCount() const
{
    int n = 0;
    for (const Action& a : m_model.actions()) n += a.type == ActionType::Group;
    return n;
}

void MainWindow::groupSelected()
{
    const auto rows = selectedRows();
    if (rows.isEmpty() || m_state != State::Idle) return;
    snapshot();
    Action g;
    g.type = ActionType::Group;
    g.label = T("Group %1").arg(groupCount() + 1);
    Action end;
    end.type = ActionType::EndGroup;
    m_model.insert(rows.last() + 1, end);
    m_model.insert(rows.first(), g);
    selectRows(rows.first(), rows.first());
    setDirty(true);
    applyFilter();
    updateUi();
    // Name it right away, in the list
    m_table->setCurrentIndex(m_model.index(rows.first(), ActionModel::ColLabel));
    m_table->edit(m_model.index(rows.first(), ActionModel::ColLabel));
}

void MainWindow::ungroupSelected()
{
    const auto rows = selectedRows();
    if (rows.isEmpty() || m_state != State::Idle) return;
    QList<int> remove;
    for (int r : rows) {
        if (m_model.at(r).type != ActionType::Group) continue;
        remove << r;
        const int end = m_model.groupEnd(r);
        if (end >= 0 && end < m_model.rowCount() && m_model.at(end).type == ActionType::EndGroup) remove << end;
    }
    if (remove.isEmpty()) { log(T("Ungroup: select a group's first step (its name).")); return; }
    snapshot();
    m_model.removeRowsList(remove);
    m_table->clearSelection();
    setDirty(true);
    applyFilter();
    updateUi();
}

void MainWindow::foldAll(bool fold)
{
    for (int r = 0; r < m_model.rowCount(); ++r)
        if (m_model.at(r).type == ActionType::Group) m_model.setCollapsed(r, fold);
    applyFilter();
}

void MainWindow::editRow(int row)
{
    if (m_state != State::Idle || row < 0 || row >= m_model.rowCount()) return;
    ActionDialog d(m_model.at(row), labels(), m_backend->injector(), this);
    if (execDialog(d) != QDialog::Accepted) return;
    snapshot();
    m_model.replace(row, d.result());
    setDirty(true);
    updateUi();
}

void MainWindow::deleteSelected(bool ask)
{
    const auto rows = selectedRows();
    if (rows.isEmpty() || m_state != State::Idle) return;
    if (ask && m_s.confirmDelete
        && QMessageBox::question(this, T("Delete Steps"),
                                 rows.size() == 1 ? T("Delete step %1?").arg(rows.first() + 1)
                                                  : T("Delete these %1 steps?").arg(rows.size())) != QMessageBox::Yes)
        return;
    snapshot();
    m_model.removeRowsList(rows);
    if (m_model.rowCount() > 0) selectRows(qMin(rows.first(), m_model.rowCount() - 1), qMin(rows.first(), m_model.rowCount() - 1));
    setDirty(true);
    updateUi();
}

void MainWindow::copySelected(bool cut)
{
    const auto rows = selectedRows();
    if (rows.isEmpty()) return;
    QJsonArray arr;
    for (int r : rows) arr.append(m_model.at(r).toJson());
    auto* mime = new QMimeData;
    const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    mime->setData(kMime, json);
    mime->setText(QString::fromUtf8(json));
    QApplication::clipboard()->setMimeData(mime);
    if (cut) deleteSelected(false);   // cut keeps a copy: no need to ask
}

void MainWindow::paste()
{
    if (m_state != State::Idle) return;
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    QByteArray data = mime ? mime->data(kMime) : QByteArray();
    if (data.isEmpty() && mime) data = mime->text().toUtf8();
    const QJsonArray arr = QJsonDocument::fromJson(data).array();
    QList<Action> list;
    for (const QJsonValue& v : arr) {
        bool ok = false;
        Action a = Action::fromJson(v.toObject(), &ok);
        if (ok) list << a;
    }
    if (list.isEmpty()) { log(T("Nothing to paste: copy some steps first.")); return; }
    snapshot();
    int row = insertRow();
    const int first = row;
    for (const Action& a : list) m_model.insert(row++, a);
    selectRows(first, row - 1);
    setDirty(true);
    applyFilter();
    updateUi();
}

void MainWindow::moveSelected(int delta)
{
    const auto rows = selectedRows();
    if (rows.isEmpty() || m_state != State::Idle) return;
    const int first = rows.first(), last = rows.last();
    if (last - first + 1 != rows.size()) { log(T("Select a single block of steps to move them.")); return; }
    if ((delta < 0 && first == 0) || (delta > 0 && last == m_model.rowCount() - 1)) return;
    snapshot();
    QList<Action> acts = m_model.actions();
    const QList<Action> block = acts.mid(first, rows.size());
    acts.remove(first, rows.size());
    acts.insert(first + delta, rows.size(), Action());
    for (int i = 0; i < block.size(); ++i) acts[first + delta + i] = block[i];
    m_model.setActions(acts);
    selectRows(first + delta, last + delta);
    setDirty(true);
    applyFilter();
}

void MainWindow::toggleEnabled()
{
    const auto rows = selectedRows();
    if (rows.isEmpty() || m_state != State::Idle) return;
    snapshot();
    const bool enable = !m_model.at(rows.first()).enabled;
    for (int r : rows) {
        Action a = m_model.at(r);
        a.enabled = enable;
        m_model.replace(r, a);
    }
    setDirty(true);
    updateHeader();
}

void MainWindow::cleanUp()
{
    if (m_state != State::Idle) return;
    QList<Action> out;
    int merged = 0, removed = 0;
    for (const Action& a : m_model.actions()) {
        if ((a.type == ActionType::Wait && a.ms == 0 && a.label.isEmpty())
            || (a.type == ActionType::MouseMove && a.from == a.pos && a.label.isEmpty())) {
            ++removed;
            continue;
        }
        if (!out.isEmpty() && a.label.isEmpty() && a.enabled && out.last().enabled) {
            Action& p = out.last();
            if (a.type == ActionType::Wait && p.type == ActionType::Wait) {
                p.ms += a.ms + a.delayMs;
                ++merged;
                continue;
            }
            if (a.type == ActionType::MouseMove && p.type == ActionType::MouseMove && a.delayMs < 50) {
                // Two moves in a row: one move along both paths.
                const int offset = p.ms + a.delayMs;
                for (const core::PathPoint& pt : a.path) p.path.append({pt.x, pt.y, pt.t + offset});
                p.ms = offset + a.ms;
                p.pos = a.pos;
                ++merged;
                continue;
            }
        }
        out << a;
    }
    if (!merged && !removed) { log(T("Clean up: nothing to tidy.")); return; }
    snapshot();
    m_model.setActions(out);
    setDirty(true);
    applyFilter();
    updateUi();
    log(T("Clean up: merged %1 step(s), removed %2 empty step(s).").arg(merged).arg(removed));
}

void MainWindow::applyFilter()
{
    QItemSelection hiddenSelected;
    for (int r = 0; r < m_model.rowCount(); ++r) {
        const QString k = filterKey(m_model.at(r).type);
        const bool show = (!m_filters.contains(k) || m_filters[k]->isChecked()) && !m_model.foldedAway(r);
        m_table->setRowHidden(r, !show);
        // Steps folded away in a group are never selected unseen (Delete would remove what can't be seen)
        if (!show && m_model.foldedAway(r) && m_table->selectionModel()->isRowSelected(r, {}))
            hiddenSelected.select(m_model.index(r, 0), m_model.index(r, ActionModel::ColCount - 1));
    }
    if (!hiddenSelected.isEmpty()) m_table->selectionModel()->select(hiddenSelected, QItemSelectionModel::Deselect);
}

// ------------------------------------------------------------------------------------------------ record / play

core::RecordOptions MainWindow::recordOptions() const
{
    core::RecordOptions o;
    o.mouseMoves = m_s.recordMoves;
    o.minMovePx = m_s.minMovePx;
    o.keyboard = m_s.recordKeyboard;
    o.mergeTyping = m_s.mergeTyping;
    o.shortestPauseMs = m_s.shortestPauseMs;
    o.doubleClickMs = m_backend->doubleClickMs();
    o.relativeToWindow = m_s.relativePositions;
    o.pathDetail = m_s.pathDetail;
    return o;
}

core::PlayOptions MainWindow::playOptions() const
{
    core::PlayOptions o;
    o.speed = m_s.speedPct / 100.0;
    if (m_s.repeatMode == QLatin1String("seconds")) { o.repeatMode = core::RepeatMode::Seconds; o.repeatCount = m_s.repeatSeconds; }
    else if (m_s.repeatMode == QLatin1String("forever")) { o.repeatMode = core::RepeatMode::Forever; }
    else { o.repeatMode = core::RepeatMode::Times; o.repeatCount = m_s.repeatTimes; }
    const QString p = m_s.pathStyle;
    o.path = p == QLatin1String("linear") ? core::PathStyle::Linear : p == QLatin1String("spline") ? core::PathStyle::Spline
           : p == QLatin1String("human") ? core::PathStyle::HumanLike : core::PathStyle::AsRecorded;
    o.typingAsRecorded = m_s.typing != QLatin1String("instant");
    o.restoreMouse = m_s.restoreMouse;
    return o;
}

void MainWindow::syncWidgetsFromSettings()
{
    const QList<QWidget*> all = {m_speed, m_repeatTimes, m_repeatSecs, m_countdownSecs, m_rTimes, m_rSecs, m_rForever, m_pathStyle,
                                 m_typing, m_after, m_countdownCheck, m_stopOnKey, m_stopOnMouse, m_blockKeys, m_restoreMouse,
                                 m_showNotif, m_hidePlay, m_recMoves, m_minMove, m_pathDetail, m_recKeys, m_mergeTyping,
                                 m_shortestPause, m_relPos, m_smartClicks, m_hideRec, m_hkRecord, m_hkNewRec, m_hkPlay, m_hkPlayFrom, m_hkPlaySel,
                                 m_hkStop, m_hkCapture};
    for (QWidget* w : all) w->blockSignals(true);
    m_speed->setValue(m_s.speedPct);
    m_repeatTimes->setValue(m_s.repeatTimes);
    m_timesUnit->setText(m_s.repeatTimes == 1 ? T("Time", "repeat once") : T("Times", "repeat count"));
    m_repeatSecs->setValue(m_s.repeatSeconds);
    (m_s.repeatMode == QLatin1String("seconds") ? m_rSecs : m_s.repeatMode == QLatin1String("forever") ? m_rForever : m_rTimes)->setChecked(true);
    m_pathStyle->setCurrentIndex(qMax(0, m_pathStyle->findData(m_s.pathStyle)));
    m_typing->setCurrentIndex(qMax(0, m_typing->findData(m_s.typing)));
    m_after->setCurrentIndex(qMax(0, m_after->findData(m_s.afterPlayback)));
    m_countdownCheck->setChecked(m_s.countdown);
    m_countdownSecs->setValue(m_s.countdownSeconds);
    m_countdownSecs->setEnabled(m_s.countdown);
    m_stopOnKey->setChecked(m_s.stopOnKey);
    m_stopOnMouse->setChecked(m_s.stopOnMouse);
    m_blockKeys->setChecked(m_s.blockKeyboard);
    m_restoreMouse->setChecked(m_s.restoreMouse);
    m_showNotif->setChecked(m_s.showNotifications);
    m_hidePlay->setChecked(m_s.hideWhilePlaying);
    m_recMoves->setChecked(m_s.recordMoves);
    m_minMove->setValue(m_s.minMovePx);
    m_pathDetail->setValue(m_s.pathDetail);
    m_pathDetailValue->setText(T("%1 %").arg(m_s.pathDetail));
    m_recKeys->setChecked(m_s.recordKeyboard);
    m_mergeTyping->setChecked(m_s.mergeTyping);
    m_mergeTyping->setEnabled(m_s.recordKeyboard);
    m_shortestPause->setValue(m_s.shortestPauseMs);
    m_relPos->setChecked(m_s.relativePositions);
    m_smartClicks->setChecked(m_s.smartClicks);
    m_hideRec->setChecked(m_s.hideWhileRecording);
    m_hkRecord->setKeySequence(QKeySequence(m_s.hkRecord));
    m_hkNewRec->setKeySequence(QKeySequence(m_s.hkNewRecording));
    m_hkPlay->setKeySequence(QKeySequence(m_s.hkPlay));
    m_hkPlayFrom->setKeySequence(QKeySequence(m_s.hkPlayFrom));
    m_hkPlaySel->setKeySequence(QKeySequence(m_s.hkPlaySelection));
    m_hkStop->setKeySequence(QKeySequence(m_s.hkStop));
    m_hkCapture->setKeySequence(QKeySequence(m_s.hkCapture));
    for (QWidget* w : all) w->blockSignals(false);
    for (auto [a, v] : {std::pair{m_confirmDeleteAct, m_s.confirmDelete}, std::pair{m_startMinAct, m_s.startMinimized},
                        std::pair{m_markerAct, m_s.showPosition}, std::pair{m_scanAct, m_s.scanCodes}}) {
        if (!a) continue;
        const QSignalBlocker blk(a);
        a->setChecked(v);
    }
}

QHash<QString, QString> MainWindow::appHotkeys() const
{
    return {{T("Record / Stop"), m_s.hkRecord},     {T("New Recording"), m_s.hkNewRecording},
            {T("Play / Pause"), m_s.hkPlay},        {T("Play From Selected"), m_s.hkPlayFrom},
            {T("Play Selection"), m_s.hkPlaySelection}, {T("Stop"), m_s.hkStop},
            {T("Capture Position"), m_s.hkCapture}};
}

bool MainWindow::typingHotkey() const
{
    if (m_state != State::Idle || QGuiApplication::applicationState() != Qt::ApplicationActive) return false;
    const QWidget* f = QApplication::focusWidget();
    return f && (qobject_cast<const QKeySequenceEdit*>(f) || (f->parentWidget() && qobject_cast<const QKeySequenceEdit*>(f->parentWidget())));
}

void MainWindow::registerHotkeys()
{
    if (!m_hotkeysEnabled) return;   // screenshot / test mode: show the hotkeys, never register them
    // While a hotkey field has the keyboard, no hotkey is registered: pressing F10 there must enter F10 in the field
    // instead of starting playback (the system gives a registered hotkey to the app, never to the field)
    m_hotkeysPaused = typingHotkey();
    struct Hk { int id; QString key; QString name; bool active; };
    // Stop is registered only while something runs, and Capture position only while a step editor with a position
    // is open, so those keys work normally in other programs the rest of the time.
    const QList<Hk> list = {
        {HkRecord, m_s.hkRecord, T("Record / Stop"), true},
        {HkNewRecording, m_s.hkNewRecording, T("New Recording"), true},
        {HkPlay, m_s.hkPlay, T("Play / Pause"), true},
        {HkPlayFrom, m_s.hkPlayFrom, T("Play From Selected"), true},
        {HkPlaySelection, m_s.hkPlaySelection, T("Play Selection"), true},
        {HkStop, m_s.hkStop, T("Stop"), m_state != State::Idle},
        {HkCapture, m_s.hkCapture, T("Capture Position"), m_openDialog && m_openDialog->hasPosition()},
    };
    QList<Hk> all = list;
    for (int i = 0; i < m_s.macroHotkeys.size(); ++i)   // Macro > Macro Hotkeys
        all.append({kMacroHotkeyBase + i, m_s.macroHotkeys.at(i).key,
                    T("Macro %1").arg(QFileInfo(m_s.macroHotkeys.at(i).file).completeBaseName()), true});
    for (int i = int(m_s.macroHotkeys.size()); i < m_macroHotkeysRegistered; ++i) {   // removed ones
        QString err;
        m_backend->setHotkey(kMacroHotkeyBase + i, QKeySequence(), &err);
    }
    m_macroHotkeysRegistered = int(m_s.macroHotkeys.size());
    QStringList problems;
    QHash<QString, QString> owner;   // key -> the first hotkey that uses it; a second one with the same key is ignored
    for (const Hk& h : std::as_const(all)) {
        const bool duplicate = !h.key.isEmpty() && owner.contains(h.key);
        if (duplicate) problems << T("%1: %2 is already the %3 hotkey.").arg(h.name, h.key, owner.value(h.key));
        else if (!h.key.isEmpty()) owner.insert(h.key, h.name);
        const bool want = h.active && !h.key.isEmpty() && !duplicate && !m_hotkeysPaused;
        QString err;
        if (!m_backend->setHotkey(h.id, want ? QKeySequence(h.key) : QKeySequence(), &err) && want)
            problems << h.name + QStringLiteral(": ") + err;
    }
    if (m_s.hkStop.isEmpty()) problems << T("No Stop hotkey: you'll need to click Stop (or move the mouse) to stop playback.");
    if (m_hkError) {
        m_hkError->setText(problems.join(QLatin1Char('\n')));
        m_hkError->setVisible(!problems.isEmpty());
    }
    if (m_recordBtn) updateUi();
}

void MainWindow::onHotkey(int id)
{
    if (id == HkCapture) { if (m_openDialog) m_openDialog->captureNow(); return; }
    if (id == HkStop) { stopAll(); return; }
    // While a dialog is open (a step editor, a file dialog, a question), starting something would be confusing.
    if (m_state == State::Idle && QApplication::activeModalWidget()) return;
    if (id >= kMacroHotkeyBase) {
        const int i = id - kMacroHotkeyBase;
        if (i >= m_s.macroHotkeys.size()) return;
        const QString file = m_s.macroHotkeys.at(i).file;
        if (m_state == State::Playing && m_playingOther == file) { stopAll(T("Its hotkey was pressed again.")); return; }
        if (m_state != State::Idle) {
            log(T("%1 wasn't played: something else is recording or playing.").arg(QFileInfo(file).fileName()));
            return;
        }
        playFile(file, T("its hotkey"));
        return;
    }
    switch (id) {
    case HkRecord:
        if (m_state == State::Recording) stopRecording();
        else if (m_state == State::Idle) startRecording(RecordWhere::End);
        break;
    case HkNewRecording:
        if (m_state == State::Recording) stopRecording();
        else if (m_state == State::Idle) startRecording(RecordWhere::New);
        break;
    case HkPlay:
        if (m_state == State::Playing) togglePause();
        else if (m_state == State::Idle) startPlaying(PlayRange::All);
        break;
    case HkPlayFrom:
        if (m_state == State::Idle) startPlaying(selectedRows().isEmpty() ? PlayRange::All : PlayRange::FromSelected);
        break;
    case HkPlaySelection:
        if (m_state == State::Idle) {
            if (selectedRows().isEmpty()) log(T("Play selection: select some steps first."));
            else startPlaying(PlayRange::Selection);
        }
        break;
    default: break;
    }
}

void MainWindow::startRecording(RecordWhere where)
{
    if (m_state != State::Idle) return;
    const QString why = m_backend->recordUnavailableReason();
    if (!why.isEmpty()) { QMessageBox::information(this, T("Can't Record"), why); return; }
    if (where == RecordWhere::New) {
        if (!maybeSave()) return;
        m_undo.clear();
        m_redo.clear();
        m_model.setActions({});
        m_file.clear();
        setDirty(false);
    }
    if (where == RecordWhere::AfterSelection && selectedRows().isEmpty()) where = RecordWhere::End;
    m_beforeRecording = m_model.actions();
    m_recordInsert = where == RecordWhere::AfterSelection ? insertRow() : m_model.rowCount();
    m_table->clearSelection();
    m_recorder.start(recordOptions(), platform::Backend::now(), m_backend->injector()->cursor());
    m_backend->setSmartClicks(m_s.smartClicks);
    m_backend->setCapture(platform::CaptureMode::Record);
    setState(State::Recording);
    registerHotkeys();
    const QString stopKey = m_s.hkStop.isEmpty() ? m_s.hkRecord : m_s.hkStop;
    log(where == RecordWhere::AfterSelection
            ? T("Recording after step %1… press %2 or click Stop recording to finish. Clicks on this window aren't recorded.")
                  .arg(QString::number(m_recordInsert), stopKey)
            : T("Recording… press %1 or click Stop recording to finish. Clicks on this window aren't recorded.").arg(stopKey));
    if (m_s.hideWhileRecording) showMinimized();
}

void MainWindow::stopRecording()
{
    if (m_state != State::Recording) return;
    m_backend->setCapture(platform::CaptureMode::Off);
    m_recorder.finish();
    const int added = m_model.rowCount() - m_beforeRecording.size();
    if (added > 0) {
        m_undo.append(m_beforeRecording);
        m_redo.clear();
        setDirty(true);
    }
    setState(State::Idle);
    registerHotkeys();
    applyFilter();
    core::Macro m;
    m.actions = m_model.actions().mid(m_recordInsert - added, added);
    log(added == 1 ? T("Recording stopped · 1 action · %1.").arg(seconds(m.totalMs()))
                   : T("Recording stopped · %1 actions · %2.").arg(QLocale().toString(added), seconds(m.totalMs())));
    if (!m_recordTo.isEmpty()) {   // --record FILE: save and quit
        core::Macro all;
        all.actions = m_model.actions();
        QString err;
        if (!all.save(m_recordTo, &err)) { fprintf(stderr, "Couldn't save %s: %s\n", qPrintable(m_recordTo), qPrintable(err)); QApplication::exit(1); }
        else { setDirty(false); QApplication::exit(0); }
        return;
    }
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}

void MainWindow::startPlaying(PlayRange range, int repeatOverride)
{
    if (m_state != State::Idle || m_model.rowCount() == 0) return;
    const QString why = m_backend->unavailableReason();
    if (!why.isEmpty()) { QMessageBox::information(this, T("Can't Play"), why); return; }
    m_pending = playOptions();
    if (repeatOverride > 0) { m_pending.repeatMode = core::RepeatMode::Times; m_pending.repeatCount = repeatOverride; }
    const auto rows = selectedRows();
    if (range != PlayRange::All && rows.isEmpty()) return;
    switch (range) {
    case PlayRange::All: break;
    case PlayRange::Selection: m_pending.first = rows.first(); m_pending.last = rows.last(); break;
    case PlayRange::FromSelected: m_pending.first = rows.first(); m_pending.last = -1; break;
    case PlayRange::UpToSelected: m_pending.first = 0; m_pending.last = rows.first(); break;
    case PlayRange::ThisStep:   // trying out one step: once, whatever the repeat setting
        m_pending.first = m_pending.last = rows.first();
        m_pending.repeatMode = core::RepeatMode::Times;
        m_pending.repeatCount = 1;
        break;
    }
    if (m_s.countdown && !m_quitWhenDone) {
        m_countdownLeft = m_s.countdownSeconds;
        setState(State::Countdown);
        registerHotkeys();
        setPill(T("Starting in %1 s…").arg(m_countdownLeft), QStringLiteral("warn"));
        log(T("Playing in %1 s. Switch to the window the macro works in.").arg(m_countdownLeft));
        m_countdown.start();
    } else {
        beginPlayback();
    }
}

void MainWindow::beginPlayback()
{
    core::Macro m;
    m.actions = m_model.actions();
    setState(State::Playing);
    registerHotkeys();
    m_player->setPaused(false);
    applyWatch();
    const QString how = m_pending.repeatMode == core::RepeatMode::Times
                            ? (m_pending.repeatCount == 1 ? T("once") : T("%1 times").arg(m_pending.repeatCount))
                        : m_pending.repeatMode == core::RepeatMode::Seconds ? T("for %1 s").arg(m_pending.repeatCount)
                                                                            : T("until stopped");
    log(T("Playing %1 at %2 %.").arg(how).arg(m_s.speedPct));
    if (m_s.hideWhilePlaying) showMinimized();
    m_player->setClipboardCache(QApplication::clipboard()->text());
    m_player->start(m, m_pending);
}

void MainWindow::togglePause()
{
    if (m_state != State::Playing) return;
    m_player->setPaused(!m_player->paused());
    applyWatch();   // paused: the user may use the mouse and keyboard
    if (m_player->paused()) {
        setPill(T("❚❚ Paused"), QStringLiteral("warn"));
        log(T("Paused. Press %1 or Resume to continue.").arg(m_s.hkPlay));
    } else {
        setPill(T("▶ Playing"), QStringLiteral("ok"));
        log(T("Resumed."));
    }
    updateUi();
}

void MainWindow::stopAll(const QString& why)
{
    switch (m_state) {
    case State::Recording: stopRecording(); break;
    case State::Countdown:
        m_countdown.stop();
        log(T("Playback cancelled."));
        setState(State::Idle);
        registerHotkeys();
        break;
    case State::Playing:
        if (!why.isEmpty()) log(why);
        m_backend->setBlockKeyboard(false);
        m_backend->setCapture(platform::CaptureMode::Off);
        m_player->stop();
        m_player->setPaused(false);
        break;  // finished() resets the state
    case State::Idle: break;
    }
}

void MainWindow::recordToFileAndQuit(const QString& path)
{
    m_recordTo = path;
    startRecording();
    if (m_state != State::Recording) { fprintf(stderr, "Can't record: %s\n", qPrintable(m_backend->recordUnavailableReason())); QApplication::exit(1); }
}

void MainWindow::playFromCommandLine(int repeat, bool quitWhenDone)
{
    m_quitWhenDone = quitWhenDone;
    startPlaying(PlayRange::All, repeat);
}

bool MainWindow::playFile(const QString& path, const QString& why)
{
    const QString name = QFileInfo(path).fileName();
    if (m_state != State::Idle) {
        log(T("%1 (%2) wasn't played: something else is recording or playing.").arg(name, why));
        return false;
    }
    if (QApplication::activeModalWidget()) {
        log(T("%1 (%2) wasn't played: a window of this app is waiting for an answer.").arg(name, why));
        return false;
    }
    const QString reason = m_backend->unavailableReason();
    if (!reason.isEmpty()) { log(T("%1 (%2) wasn't played: %3").arg(name, why, reason)); return false; }
    core::Macro m;
    QString err;
    if (!QFileInfo::exists(path)) { log(T("%1 (%2) wasn't played: the file isn't there any more (%3).").arg(name, why, QDir::toNativeSeparators(path))); return false; }
    if (!core::Macro::load(path, &m, &err)) { log(T("%1 (%2) wasn't played: %3").arg(name, why, err)); return false; }
    if (!err.isEmpty()) log(T("%1: %2").arg(name, err));
    if (m.actions.isEmpty()) { log(T("%1 (%2) wasn't played: it has no steps.").arg(name, why)); return false; }
    m_playingOther = path;
    m_otherActions = m.actions;
    m_pending = playOptions();
    m_pending.repeatMode = core::RepeatMode::Times;   // once: the macro's own Repeat steps still work
    m_pending.repeatCount = 1;
    setState(State::Playing);
    registerHotkeys();
    m_player->setPaused(false);
    applyWatch();
    log(T("Playing %1 (%2).").arg(name, why));
    m_player->setClipboardCache(QApplication::clipboard()->text());
    m_player->start(m, m_pending);
    return true;
}

void MainWindow::handOver(const QJsonObject& msg)
{
    const QString file = msg.value(QStringLiteral("file")).toString();
    if (msg.value(QStringLiteral("play")).toBool() && !file.isEmpty()) {
        playFile(file, T("started again with --play"));   // in the background: the macro's target keeps the focus
        return;
    }
    if (!file.isEmpty()) {
        if (m_state != State::Idle || QApplication::activeModalWidget())
            log(T("%1 wasn't opened: finish what's running (or close the open window) first.").arg(QFileInfo(file).fileName()));
        else if (maybeSave()) openFile(file);
    }
    if (!file.isEmpty() || msg.value(QStringLiteral("show")).toBool()) {
        m_quitWhenDone = false;   // started from a shortcut with --quit, and now opened on purpose: stay open
        if (isMinimized()) showNormal();
        show();
        raise();
        activateWindow();
    }
}

void MainWindow::createDesktopShortcut()
{
    if (m_state != State::Idle) return;
    if (m_file.isEmpty() || m_dirty) {
        QMessageBox box(QMessageBox::Information, T("Create Desktop Shortcut"),
                        T("The shortcut plays the saved macro file. Save the macro first?"),
                        QMessageBox::Save | QMessageBox::Cancel, this);
        if (box.exec() != QMessageBox::Save || !save(m_file.isEmpty()) || m_file.isEmpty()) return;
    }
    const QString dir = desktopForTests.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) : desktopForTests;
    const QString name = QFileInfo(m_file).completeBaseName();
    const QString link = QDir(dir).filePath(name + platform::sys::shortcutExtension());
    if (QFileInfo::exists(link)
        && QMessageBox::question(this, T("Create Desktop Shortcut"),
                                 T("There's already a shortcut named “%1” on the desktop. Replace it?").arg(name))
               != QMessageBox::Yes)
        return;
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString program = appImage.isEmpty() ? QApplication::applicationFilePath() : appImage;
    QStringList args{QDir::toNativeSeparators(m_file), QStringLiteral("--play"), QStringLiteral("--quit"), QStringLiteral("--minimized")};
    if (appdata::usesOverride())   // portable: the same settings folder
        args << QStringLiteral("--data-folder") << (appdata::isTemporary() ? QStringLiteral("<none>") : QDir::toNativeSeparators(appdata::folder()));
    QString err;
    if (!m_backend->createShortcut(link, program, args, T("Plays the macro %1 with %2.").arg(name, QStringLiteral(APP_NAME)), &err)) {
        QMessageBox::warning(this, T("Create Desktop Shortcut"), T("The shortcut couldn't be made: %1").arg(err));
        return;
    }
    log(T("Desktop shortcut made: %1. Double-click it to play %2 (it plays in this window if the app is already "
                       "running).").arg(QDir::toNativeSeparators(link), QFileInfo(m_file).fileName()));
}

void MainWindow::checkForUpdates()
{
    if (m_checkingUpdates) return;
    m_checkingUpdates = true;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(updateUrlForTests.isEmpty() ? QStringLiteral("https://api.github.com/repos/cincottash/MacroRecorder/releases/latest")
                                                         : updateUrlForTests));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral(APP_NAME));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    log(T("Checking for updates…"));
    QNetworkReply* r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        m_checkingUpdates = false;
        const QString title = T("Check for Updates");
        const QString current = QStringLiteral(APP_VERSION);
        if (r->error() != QNetworkReply::NoError) {
            const QString why = r->error() == QNetworkReply::OperationCanceledError ? T("no answer within 15 seconds") : r->errorString();
            log(T("Couldn't check for updates: %1").arg(why));
            QMessageBox::warning(this, title, T("Couldn't check for updates: %1.\n\nThe releases are also listed on the "
                                                "project's website (Help > Project Website).").arg(why));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        const QString tag = o.value(QStringLiteral("tag_name")).toString();
        QString page = o.value(QStringLiteral("html_url")).toString();
        if (!page.startsWith(QLatin1String("https://github.com/"))) page = kRepoUrl + QStringLiteral("/releases/latest");
        const QVersionNumber latest = QVersionNumber::fromString(tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag);
        if (tag.isEmpty() || latest.isNull()) {
            log(T("Couldn't check for updates: GitHub's answer had no version in it."));
            QMessageBox::warning(this, title, T("Couldn't check for updates: GitHub's answer had no version in it."));
            return;
        }
        const QVersionNumber mine = QVersionNumber::fromString(current.section(QLatin1Char('-'), 0, 0));
        const bool dev = current.contains(QLatin1Char('-'));   // a local or test build, not a release
        const QString latestText = latest.toString();
        if (latest > mine || dev) {
            // A no-break space keeps the name and its version on one line
            const QString text = dev ? T("This is a test build (%1). The latest release is %2 %3.").arg(current, QStringLiteral(APP_NAME), latestText)
                                     : T("%1 %2 is available. This is %3.").arg(QStringLiteral(APP_NAME), latestText, current);
            log(text);
            QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Close, this);
            QPushButton* open = box.addButton(T("Open Download Page"), QMessageBox::AcceptRole);
            box.setDefaultButton(open);
            box.exec();
            if (box.clickedButton() == open) QDesktopServices::openUrl(QUrl(page));
            return;
        }
        const QString text = T("%1 %2 is the latest version.").arg(QStringLiteral(APP_NAME), current);
        log(text);
        QMessageBox::information(this, title, text);
    });
}

namespace {
QString aiBase(const QString& url)
{
    QString b = url.trimmed();
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    return b;
}
// What went wrong, in a sentence: no server, the server's own error message, or the network's
QString aiError(QNetworkReply* r, const QByteArray& body, const QString& base)
{
    if (r->error() == QNetworkReply::ConnectionRefusedError || r->error() == QNetworkReply::HostNotFoundError)
        return T("no AI server answered at %1. Start LM Studio's local server (its Developer tab), or set another "
                 "server in File > Settings > AI Server…").arg(base);
    if (r->error() == QNetworkReply::OperationCanceledError) return T("no answer from the AI server in time.");
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QString said = o.value(QStringLiteral("error")).isObject() ? o.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()
                                                               : o.value(QStringLiteral("error")).toString();
    if (!said.isEmpty()) return T("the AI server said: %1").arg(said);
    return r->errorString();
}
}  // namespace

void MainWindow::askAI(const QString& prompt, int timeoutMs)
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    const QString base = aiBase(m_s.aiUrl);
    QNetworkRequest req(QUrl(base + QStringLiteral("/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral(APP_NAME));
    req.setTransferTimeout(timeoutMs);
    QJsonObject body{{QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                                          {QStringLiteral("content"), prompt}}}},
                     {QStringLiteral("temperature"), 0.2},
                     {QStringLiteral("stream"), false}};
    if (!m_s.aiModel.trimmed().isEmpty()) body[QStringLiteral("model")] = m_s.aiModel.trimmed();
    log(T("Ask AI: waiting for the answer from %1…").arg(base));
    QNetworkReply* r = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_aiReply = r;
    connect(r, &QNetworkReply::finished, this, [this, r, base] {
        r->deleteLater();
        if (m_aiReply == r) m_aiReply = nullptr;
        const QByteArray data = r->readAll();
        if (r->error() != QNetworkReply::NoError) { m_player->answerAI(aiError(r, data, base), false); return; }
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        QString text = o.value(QStringLiteral("choices")).toArray().first().toObject().value(QStringLiteral("message")).toObject()
                           .value(QStringLiteral("content")).toString();
        // Models that think aloud put it in <think>…</think> first: only the answer is kept
        static const QRegularExpression think(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
        text.remove(think);
        text = text.trimmed();
        if (text.isEmpty()) { m_player->answerAI(T("the AI server's answer was empty."), false); return; }
        log(T("Ask AI: answered (%1 characters).").arg(text.size()), true);
        m_player->answerAI(text, true);
    });
}

void MainWindow::editAiServer()
{
    QDialog d(this);
    d.setWindowTitle(T("AI Server"));
    auto* lay = new QVBoxLayout(&d);
    auto* intro = new QLabel(T("Ask AI steps send their questions here. Free and private: LM Studio (lmstudio.ai) runs "
                                            "models on this PC; start its server in its Developer tab. Any server that speaks the "
                                            "OpenAI API works too, for example Ollama at http://localhost:11434/v1."));
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(12);
    auto* url = new QLineEdit(m_s.aiUrl);
    url->setPlaceholderText(QStringLiteral("http://localhost:1234/v1"));
    auto* model = new QComboBox;
    model->setEditable(true);
    model->addItem(m_s.aiModel);
    model->setCurrentText(m_s.aiModel);
    model->lineEdit()->setPlaceholderText(T("empty: the model the server has loaded"));
    auto* test = new QPushButton(T("List Models"));
    test->setToolTip(T("Asks the server which models it has (nothing is loaded or downloaded)."));
    auto* modelRow = new QWidget;
    auto* mh = new QHBoxLayout(modelRow);
    mh->setContentsMargins(0, 0, 0, 0);
    mh->addWidget(model, 1);
    mh->addWidget(test);
    form->addRow(T("Server Address:"), url);
    form->addRow(T("Model:"), modelRow);
    lay->addLayout(form);
    auto* status = new QLabel;
    status->setObjectName(QStringLiteral("muted"));
    status->setWordWrap(true);
    status->setProperty("testId", QStringLiteral("aiStatus"));
    lay->addWidget(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(test, &QPushButton::clicked, &d, [this, url, model, status, test] {
        if (!m_net) m_net = new QNetworkAccessManager(this);
        const QString base = aiBase(url->text());
        QNetworkRequest req(QUrl(base + QStringLiteral("/models")));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral(APP_NAME));
        req.setTransferTimeout(10000);
        status->setText(T("Asking %1…").arg(base));
        test->setEnabled(false);
        QNetworkReply* r = m_net->get(req);
        connect(r, &QNetworkReply::finished, status, [r, base, model, status, test] {
            r->deleteLater();
            test->setEnabled(true);
            const QByteArray data = r->readAll();
            if (r->error() != QNetworkReply::NoError) { status->setText(T("Couldn't list the models: %1").arg(aiError(r, data, base))); return; }
            QStringList ids;
            for (const QJsonValue& v : QJsonDocument::fromJson(data).object().value(QStringLiteral("data")).toArray())
                ids << v.toObject().value(QStringLiteral("id")).toString();
            ids.removeAll(QString());
            const QString keep = model->currentText();
            model->clear();
            model->addItem(QString());
            model->addItems(ids);
            model->setCurrentText(keep);
            status->setText(ids.isEmpty() ? T("The server answered, but has no models.")
                            : ids.size() == 1 ? T("The server has 1 model.")
                                              : T("The server has %1 models.").arg(ids.size()));
        });
    });
    d.resize(600, d.sizeHint().height());
    if (d.exec() != QDialog::Accepted) return;
    m_s.aiUrl = url->text().trimmed().isEmpty() ? Settings().aiUrl : url->text().trimmed();
    m_s.aiModel = model->currentText().trimmed();
    m_s.save(appdata::settingsFile());
    log(T("AI server: %1%2.").arg(m_s.aiUrl, m_s.aiModel.isEmpty() ? QString() : QStringLiteral(" (") + m_s.aiModel + QLatin1Char(')')));
}

QDateTime MainWindow::now() const { return clockForTests ? clockForTests() : QDateTime::currentDateTime(); }

void MainWindow::reschedule()
{
    const QDateTime t = now();
    m_due.clear();
    m_waitLogged.clear();
    for (const Schedule& s : std::as_const(m_s.schedules)) m_due << (s.enabled ? s.nextRun(t) : QDateTime());
}

void MainWindow::checkSchedules()
{
    if (!m_hotkeysEnabled) return;   // screenshot mode
    const QDateTime t = now();
    for (int i = 0; i < m_s.schedules.size() && i < m_due.size(); ++i) {
        if (!m_due[i].isValid() || t < m_due[i]) continue;
        Schedule& s = m_s.schedules[i];
        const QString name = QFileInfo(s.file).fileName();
        if (m_due[i].secsTo(t) > 15 * 60) {   // busy (or the computer asleep) for a long time: don't play it late
            log(T("Schedule: %1 wasn't played at %2 (the app was busy or the computer asleep).")
                    .arg(name, QLocale().toString(m_due[i].time(), QLocale::ShortFormat)));
            m_due[i] = s.nextRun(t);
            m_waitLogged.remove(i);
            continue;
        }
        const bool busy = m_state != State::Idle || QApplication::activeModalWidget();
        if (busy) {
            if (!m_waitLogged.contains(i)) {
                m_waitLogged.insert(i);
                log(T("Schedule: %1 is due; it plays when the app is free (finish recording or playing, or close "
                                   "the open window).").arg(name));
            }
            continue;
        }
        m_waitLogged.remove(i);
        s.lastRun = t;
        if (s.repeat == QLatin1String("once")) s.enabled = false;
        m_due[i] = s.enabled ? s.nextRun(t) : QDateTime();
        saveSettingsSoon();
        playFile(s.file, T("schedule: %1").arg(s.describe()));
        return;   // one at a time; the next waits for this one
    }
}

void MainWindow::editMacroHotkeys()
{
    MacroHotkeysDialog d(m_s.macroHotkeys, appHotkeys(), m_s.lastFolder, this);
    if (d.exec() != QDialog::Accepted || d.result() == m_s.macroHotkeys) return;
    m_s.macroHotkeys = d.result();
    m_s.save(appdata::settingsFile());
    registerHotkeys();
    log(T("Macro hotkeys: %1 set.").arg(m_s.macroHotkeys.size()));
}

void MainWindow::editSchedules()
{
    SchedulesDialog d(m_s.schedules, [this] { return now(); }, m_s.lastFolder, this);
    if (d.exec() != QDialog::Accepted || d.result() == m_s.schedules) return;
    m_s.schedules = d.result();
    m_s.save(appdata::settingsFile());
    reschedule();
    int on = 0;
    QDateTime first;
    for (const QDateTime& due : std::as_const(m_due))
        if (due.isValid()) { ++on; if (!first.isValid() || due < first) first = due; }
    log(on ? T("Schedules: %1 on; the next one plays %2.").arg(on).arg(autorun::whenText(first, now()).toLower())
           : T("Schedules: none on."));
}

// ------------------------------------------------------------------------------------------------ more editing

void MainWindow::combineSelected()
{
    const auto rows = selectedRows();
    if (rows.isEmpty() || m_state != State::Idle) return;
    QList<Action> acts = m_model.actions();
    const QSet<int> selected(rows.cbegin(), rows.cend());
    QList<int> drop;
    int combined = 0, drags = 0;
    for (int i : rows) {
        if (drop.contains(i)) continue;
        const Action& d = acts[i];
        const bool mouse = d.type == ActionType::MouseDown, key = d.type == ActionType::KeyDown;
        if (!mouse && !key) continue;
        // The matching release, with only (unlabeled) waits in between: those become the hold time.
        int hold = 0, j = i + 1;
        QList<int> waits;
        while (j < acts.size() && acts[j].type == ActionType::Wait && acts[j].label.isEmpty()) {
            hold += acts[j].delayMs + acts[j].ms;
            waits << j++;
        }
        if (j >= acts.size() || !selected.contains(j)) continue;
        const Action& u = acts[j];
        if (mouse ? (u.type != ActionType::MouseUp || u.button != d.button) : (u.type != ActionType::KeyUp || u.key != d.key)) continue;
        if (mouse && ((u.pos - d.pos).manhattanLength() > 4 || u.relativeToWindow != d.relativeToWindow)) { ++drags; continue; }
        hold += u.delayMs;
        Action c = d;
        c.type = mouse ? ActionType::MouseClick : ActionType::KeyPress;
        c.clicks = 1;
        c.holdMs = qMin(hold, 60000);
        if (c.comment.isEmpty()) c.comment = u.comment;
        if (!u.label.isEmpty() && c.label.isEmpty()) c.label = u.label;
        acts[i] = c;
        drop << waits << j;
        ++combined;
    }
    if (!combined) {
        log(drags ? T("Combine: those are drags (pressed and released at different places), so they stay as they are.")
                  : T("Combine: select a button down and its button up (or a key down and its key up)."));
        return;
    }
    std::sort(drop.begin(), drop.end(), std::greater<int>());
    for (int r : drop) acts.removeAt(r);
    snapshot();
    m_model.setActions(acts);
    selectRows(rows.first(), rows.first());
    setDirty(true);
    applyFilter();
    updateUi();
    log(combined == 1 ? T("Combined 1 pair into a single step.") : T("Combined %1 pairs into single steps.").arg(combined));
}

void MainWindow::findReplace()
{
    if (m_state != State::Idle) return;
    QDialog d(this);
    d.setWindowTitle(T("Find and Replace"));
    auto* lay = new QVBoxLayout(&d);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(12);
    auto* find = new QLineEdit;
    auto* repl = new QLineEdit;
    auto* matchCase = new QCheckBox(T("Match Case"));
    auto* status = muted(QString());
    find->setProperty("testId", QStringLiteral("findText"));
    repl->setProperty("testId", QStringLiteral("replaceText"));
    form->addRow(T("Find:"), find);
    form->addRow(T("Replace With:"), repl);
    form->addRow(QString(), matchCase);
    lay->addLayout(form);
    lay->addWidget(muted(T("Looks in what the list shows (Action, Value, Label, Comment). Replace changes typed text, "
                           "window titles and programs, file names, messages, labels and comments.")));
    lay->addWidget(status);
    auto* bb = new QDialogButtonBox;
    QPushButton* next = bb->addButton(T("Find Next"), QDialogButtonBox::ActionRole);
    QPushButton* all = bb->addButton(T("Replace All"), QDialogButtonBox::ActionRole);
    bb->addButton(QDialogButtonBox::Close);
    next->setProperty("testId", QStringLiteral("findNext"));
    all->setProperty("testId", QStringLiteral("replaceAll"));
    next->setDefault(true);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.setMinimumWidth(440);
    auto cs = [matchCase] { return matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive; };
    connect(next, &QPushButton::clicked, &d, [=, this] {
        const QString f = find->text();
        const int n = m_model.rowCount();
        if (f.isEmpty() || n == 0) return;
        const auto sel = selectedRows();
        const int start = sel.isEmpty() ? 0 : sel.last() + 1;
        for (int i = 0; i < n; ++i) {
            const int r = (start + i) % n;
            const Action& a = m_model.at(r);
            for (const QString& text : {a.typeName(), a.valueText(), a.label, a.comment}) {
                if (!text.contains(f, cs())) continue;
                selectRows(r, r);
                status->setText(m_table->isRowHidden(r) ? T("Found in step %1 (hidden by the Show filter).").arg(r + 1)
                                                        : T("Found in step %1.").arg(r + 1));
                return;
            }
        }
        status->setText(T("“%1” isn't in any step.").arg(f));
    });
    connect(all, &QPushButton::clicked, &d, [=, this] {
        const QString f = find->text();
        if (f.isEmpty()) return;
        QList<Action> acts = m_model.actions();
        int changed = 0;
        for (Action& a : acts) {
            Action b = a;
            for (QString* t : {&b.text, &b.title, &b.exe, &b.program, &b.arguments, &b.message, &b.file, &b.label, &b.comment, &b.target})
                t->replace(f, repl->text(), cs());
            if (b.text != a.text) b.strokes.clear();   // the recorded rhythm belonged to the old text
            if (b.toJson() != a.toJson()) { a = b; ++changed; }
        }
        if (!changed) { status->setText(T("Nothing to replace.")); return; }
        snapshot();
        m_model.setActions(acts);
        setDirty(true);
        applyFilter();
        updateUi();
        status->setText(changed == 1 ? T("Changed 1 step (Undo puts it back).") : T("Changed %1 steps (Undo puts them back).").arg(changed));
        log(T("Replaced “%1” with “%2” in %3 step(s).").arg(f, repl->text()).arg(changed));
    });
    d.exec();
}

void MainWindow::exportCsv()
{
    if (m_model.rowCount() == 0) { log(T("Nothing to export: the list is empty.")); return; }
    const QString base = m_file.isEmpty() ? T("Untitled") : QFileInfo(m_file).completeBaseName();
    const QString start = QDir(m_s.lastFolder.isEmpty() ? QDir::homePath() : m_s.lastFolder).filePath(base + QStringLiteral(".csv"));
    QString path = QFileDialog::getSaveFileName(this, T("Export to CSV"), start, T("CSV files (*.csv)"),
                                                nullptr, filedialogs::options());
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".csv");
    // Fields are separated the way the system's spreadsheets expect: "," in the US and UK, ";" in most of Europe,
    // where Excel otherwise puts every row into one column.
    const QChar sep = m_backend->listSeparator();
    auto cell = [sep](QString s) {
        if (s.contains(sep) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r')))
            s = QLatin1Char('"') + s.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
        return s;
    };
    QString out = QStringList{cell(T("Step")), cell(T("Action")), cell(T("Value")), cell(T("Label")),
                              cell(T("Comment")), cell(T("Enabled")), cell(T("Pause before (ms)"))}.join(sep)
                  + QStringLiteral("\r\n");
    for (int r = 0; r < m_model.rowCount(); ++r) {
        const Action& a = m_model.at(r);
        out += QStringList{QString::number(r + 1), cell(a.typeName()), cell(a.valueText()), cell(a.label), cell(a.comment),
                           a.enabled ? T("yes") : T("no"), QString::number(a.delayMs)}.join(sep)
               + QStringLiteral("\r\n");
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, T("Couldn't Export"), T("Couldn't write %1:\n%2").arg(path, f.errorString()));
        return;
    }
    f.write("\xEF\xBB\xBF");   // UTF-8 marker, so Excel shows accents and symbols correctly
    f.write(out.toUtf8());
    if (!f.commit()) {
        QMessageBox::warning(this, T("Couldn't Export"), T("Couldn't write %1:\n%2").arg(path, f.errorString()));
        return;
    }
    m_s.lastFolder = QFileInfo(path).absolutePath();
    log(T("Exported %1 steps to %2.").arg(m_model.rowCount()).arg(QFileInfo(path).fileName()));
}

void MainWindow::copyCommandLine()
{
    if (m_file.isEmpty() || m_dirty) {
        QMessageBox::information(this, T("Copy Command Line"),
                                 T("Save the macro first: the command line plays the saved file."));
        return;
    }
    // An AppImage runs from a temporary folder; $APPIMAGE is the file the user actually has.
    const QString exe = qEnvironmentVariableIsSet("APPIMAGE") ? qEnvironmentVariable("APPIMAGE") : QCoreApplication::applicationFilePath();
    const QString cmd = quotedArg(QDir::toNativeSeparators(exe)) + QLatin1Char(' ') + quotedArg(QDir::toNativeSeparators(m_file))
                        + QStringLiteral(" --play --quit");
    QApplication::clipboard()->setText(cmd);
    log(T("Copied the command line that plays %1 to the clipboard.").arg(QFileInfo(m_file).fileName()));
}

// ------------------------------------------------------------------------------------------------ playback extras

void MainWindow::applyWatch()
{
    const bool playing = m_state == State::Playing && !m_player->paused() && !m_msgBox && !m_askBox;
    const bool block = playing && m_s.blockKeyboard && m_backend->canBlockKeyboard();
    m_backend->setBlockKeyboard(block);
    m_backend->setCapture(playing && (m_s.stopOnKey || m_s.stopOnMouse || block) ? platform::CaptureMode::Watch : platform::CaptureMode::Off);
}

void MainWindow::showMessageBox(const QString& text)
{
    if (m_msgBox) m_msgBox->done(0);
    // Its own window, on top: the main window may be minimized while playing.
    auto* box = new QMessageBox(QMessageBox::Information, QStringLiteral(APP_NAME), text, QMessageBox::Ok);
    box->setWindowIcon(windowIcon());
    box->setWindowFlag(Qt::WindowStaysOnTopHint);
    box->setWindowModality(Qt::ApplicationModal);
    box->setAttribute(Qt::WA_DeleteOnClose);
    m_msgBox = box;
    applyWatch();   // pressing OK isn't "the user moved the mouse"
    log(T("Message: %1 (waiting for OK)").arg(text));
    connect(box, &QDialog::finished, this, [this, box] {
        if (m_msgBox == box) m_msgBox = nullptr;
        m_player->answerMessageBox();
        applyWatch();
    });
    box->show();
    box->raise();
    box->activateWindow();
}

void MainWindow::showAskBox(const QString& question, const QString& defaultText)
{
    if (m_askBox) m_askBox->done(QDialog::Rejected);
    // Its own window, on top (the main window may be minimized while playing), like a Message Box step
    auto* box = new QDialog(nullptr, Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    box->setWindowTitle(QStringLiteral(APP_NAME));
    box->setWindowIcon(windowIcon());
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setProperty("testId", QStringLiteral("askBox"));
    auto* lay = new QVBoxLayout(box);
    auto* label = new QLabel(question);
    label->setWordWrap(true);
    label->setMinimumWidth(320);
    auto* edit = new QLineEdit(defaultText);
    edit->selectAll();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setToolTip(T("Stops playback."));
    lay->addWidget(label);
    lay->addWidget(edit);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, box, &QDialog::reject);
    m_askBox = box;
    applyWatch();   // typing the answer isn't "the user pressed a key"
    log(T("Question: %1 (waiting for an answer)").arg(question));
    connect(box, &QDialog::finished, this, [this, box, edit](int r) {
        if (m_askBox == box) m_askBox = nullptr;
        m_player->answerAsk(edit->text(), r == QDialog::Accepted);
        applyWatch();
    });
    box->show();
    box->raise();
    box->activateWindow();
    edit->setFocus();
}

void MainWindow::runAfterPlayback()
{
    const QString what = m_s.afterPlayback;
    if (what == QLatin1String("nothing")) return;
    if (what == QLatin1String("quit")) {
        log(T("Closing (When Finished: Close App)."));
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    struct Kind { platform::SystemAction action; QString doing, button; };
    const QHash<QString, Kind> kinds = {   // not static: the text follows the language
        {QStringLiteral("lock"), {platform::SystemAction::Lock, T("Locking the computer"), T("Lock Now")}},
        {QStringLiteral("sleep"), {platform::SystemAction::Sleep, T("Going to sleep"), T("Sleep Now")}},
        {QStringLiteral("logoff"), {platform::SystemAction::LogOff, T("Logging off"), T("Log Off Now")}},
        {QStringLiteral("restart"), {platform::SystemAction::Restart, T("Restarting"), T("Restart Now")}},
        {QStringLiteral("shutdown"), {platform::SystemAction::ShutDown, T("Shutting down"), T("Shut Down Now")}},
    };
    if (!kinds.contains(what)) return;
    const Kind k = kinds.value(what);
    if (isMinimized()) showNormal();
    QMessageBox box(QMessageBox::Warning, T("When Finished"), QString(), QMessageBox::Cancel, this);
    QPushButton* now = box.addButton(k.button, QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    int left = 30;
    auto show = [&] { box.setText(T("The macro finished. %1 in %2 s…").arg(k.doing).arg(left)); };
    show();
    QTimer tick;
    tick.setInterval(1000);
    connect(&tick, &QTimer::timeout, &box, [&] {
        if (--left <= 0) { box.done(QDialog::Accepted + 100); return; }
        show();
    });
    tick.start();
    const int r = box.exec();
    tick.stop();
    if (box.clickedButton() != now && r != QDialog::Accepted + 100) {
        log(T("When Finished: cancelled."));
        return;
    }
    log(T("When Finished: %1.").arg(k.doing.toLower()));
    if (what == QLatin1String("restart") || what == QLatin1String("shutdown") || what == QLatin1String("logoff")) {
        m_s.geometry = saveGeometry();
        m_s.save(appdata::settingsFile());   // settings are safe even if the system closes the app abruptly
    }
    QString err;
    if (!m_backend->systemAction(k.action, &err)) log(T("Couldn't do that: %1").arg(err));
}

void MainWindow::updateMarker()
{
    if (!m_marker) return;
    QPoint at;
    bool show = false;
    if (m_s.showPosition && m_state == State::Idle && !isMinimized() && isVisible()
        && QGuiApplication::applicationState() == Qt::ApplicationActive && m_table) {
        const auto rows = selectedRows();
        if (rows.size() == 1) {
            const Action& a = m_model.at(rows.first());
            // Positions relative to a window depend on where that window is during playback: nothing to mark.
            if (hasScreenPosition(a) && !a.relativeToWindow) { at = a.pos; show = true; }
        }
    }
    if (!show) { m_marker->hide(); return; }
    m_marker->move(at - QPoint(m_marker->width() / 2, m_marker->height() / 2));
    m_marker->show();
    // Step positions are physical screen pixels; on scaled screens only the system can place a window that exactly.
    if (QGuiApplication::platformName() == QLatin1String("windows")) platform::sys::centerWindowAt(quintptr(m_marker->winId()), at);
}

void MainWindow::showPreview(int row)
{
    if (!m_preview) return;
    if (!m_s.showPreview || row < 0 || row >= m_model.rowCount() || !ActionPreview::canPreview(m_model.at(row)) || m_table->selecting()) {
        m_preview->stop();
        return;
    }
    // In the list's right-hand side, just below the step (above it near the bottom of the list).
    m_preview->showAction(m_model.at(row));
    const QWidget* vp = m_table->viewport();
    const int w = qBound(220, vp->width() * 38 / 100, 380);
    const int h = qMin(m_preview->heightForWidth(w), qMax(120, vp->height() - 16));
    m_preview->setFixedSize(w, h);
    const int rowTop = m_table->rowViewportPosition(row), rowBottom = rowTop + m_table->rowHeight(row);
    int y = rowBottom + 6;
    if (y + h > vp->height() - 8) y = rowTop - h - 6;
    y = qBound(8, y, qMax(8, vp->height() - h - 8));
    m_preview->move(vp->width() - w - 12, y);
}

void MainWindow::trigger(const QString& menuText)
{
    for (QAction* a : findChildren<QAction*>())
        if (!a->menu() && a->text() == menuText) { a->trigger(); return; }
}

int MainWindow::execDialog(ActionDialog& d)
{
    m_openDialog = &d;
    registerHotkeys();     // the Capture position hotkey, while this editor is open
    const int r = d.exec();
    m_openDialog = nullptr;
    registerHotkeys();
    return r;
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange || e->type() == QEvent::ActivationChange) updateMarker();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_state != State::Idle) stopAll();
    if (!maybeSave()) { e->ignore(); return; }
    m_s.geometry = saveGeometry();
    m_s.tableState = m_table->horizontalHeader()->saveState();
    m_saveTimer.stop();
    if (!m_startingOver) m_s.save(appdata::settingsFile());
    e->accept();
}

void MainWindow::startOver()
{
    if (m_state != State::Idle || !maybeSave()) return;
    const bool portable = appdata::usesOverride();
    QMessageBox box(QMessageBox::Question, T("Start Over"),
                    T("Start over as if %1 had just been installed?").arg(QStringLiteral(APP_NAME)),
                    QMessageBox::Yes | QMessageBox::Cancel, this);
    box.setInformativeText(
        T("Every setting goes back to its default: the language, the theme and imported themes, the layout, Window Opacity, "
          "hotkeys, macro hotkeys and schedules, playback and recording options, and recent files. "
          "Start at Login is turned off.")
        + QStringLiteral("\n\n")
        + (portable ? T("Then %1 starts again with the same app data folder.").arg(QStringLiteral(APP_NAME))
                    : T("Then %1 starts again and asks for the language and where to keep its settings.").arg(QStringLiteral(APP_NAME)))
        + QStringLiteral("\n\n") + T("Your saved macros aren't touched."));
    box.button(QMessageBox::Yes)->setText(T("Start Over"));
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) return;

    m_saveTimer.stop();
    QString err;
    if (m_backend->startsAtLogin() && !m_backend->setStartAtLogin(false, &err))
        log(T("Start at Login: %1").arg(err));
    // The new copy registers the hotkeys again; this one lets go of them first.
    for (int id = HkRecord; id <= HkCapture; ++id) m_backend->setHotkey(id, QKeySequence(), &err);
    QFile::remove(appdata::settingsFile());
    appdata::forgetChoice();
    m_startingOver = true;

    QStringList args;
    if (portable) args << QStringLiteral("--data-folder") << (appdata::isTemporary() ? QStringLiteral("<none>") : appdata::folder());
    if (!restartApp(args))
        QMessageBox::warning(this, T("Start Over"),
                             T("The settings are reset, but %1 couldn't start again by itself. "
                               "Open it again to continue.").arg(QStringLiteral(APP_NAME)));
    m_dirty = false;   // maybeSave() above already asked
    close();
}

bool MainWindow::restartApp(const QStringList& args)
{
    // An AppImage runs from a folder that's gone once it closes: start the AppImage file itself again.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString program = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    if (startAgain) return startAgain(program, args);
    restartAfterQuit() = {program, args};   // main() starts it once this copy has let go of its hotkeys
    return true;
}

void MainWindow::chooseLanguage(const QString& code)
{
    if (!language::isKnown(code) || code == m_s.language) return;
    m_s.language = code;
    m_s.save(appdata::settingsFile());
    if (code == language::current()) {   // back to the language showing now: nothing to restart
        log(T("Language: %1.").arg(language::nameOf(code)));
        return;
    }
    if (m_state != State::Idle) {
        log(T("Language: %1 from the next start.").arg(language::nameOf(code)));
        return;
    }
    QMessageBox box(QMessageBox::Question, T("Language"),
                    T("%1 shows in %2 once it starts again. Restart it now?").arg(QStringLiteral(APP_NAME), language::nameOf(code)),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.button(QMessageBox::Yes)->setText(T("Restart Now"));
    box.button(QMessageBox::No)->setText(T("Later"));
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() != QMessageBox::Yes || !maybeSave()) {
        log(T("Language: %1 from the next start.").arg(language::nameOf(code)));
        return;
    }
    QStringList args;
    if (appdata::usesOverride()) args << QStringLiteral("--data-folder") << (appdata::isTemporary() ? QStringLiteral("<none>") : appdata::folder());
    if (!m_file.isEmpty()) args << m_file;   // the open macro comes back
    if (!restartApp(args)) {
        log(T("%1 couldn't start again by itself: open it again to see it in %2.").arg(QStringLiteral(APP_NAME), language::nameOf(code)));
        return;
    }
    m_dirty = false;   // maybeSave() above already asked
    close();
}
