#include "MainWindow.h"

#include "ActionDialog.h"
#include "AppData.h"
#include "Themes.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using core::Action;
using core::ActionType;

namespace {

const QString kRepoUrl = QStringLiteral("https://github.com/cincottash/MacroRecorder");
const QString kMime = QStringLiteral("application/x-" APP_NAME "-actions");
const QString kExt = QStringLiteral("macro");
enum HotkeyId { HkRecord = 1, HkPlay = 2, HkStop = 3 };

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
    if (ms < 60000) return QString::number(ms / 1000.0, 'f', 1) + QStringLiteral(" s");
    const int s = ms / 1000;
    return QStringLiteral("%1 min %2 s").arg(s / 60).arg(s % 60);
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
    case ActionType::WindowFocus: return QStringLiteral("windows");
    default: return QStringLiteral("other");
    }
}

}  // namespace

MainWindow::MainWindow(Settings& s, bool hotkeys, QWidget* parent)
    : QMainWindow(parent), m_s(s), m_backend(platform::Backend::create(this)), m_hotkeysEnabled(hotkeys)
{
    m_player = new core::Player(m_backend->injector(), this);
    setWindowIcon(QIcon(QStringLiteral(":/icon.svg")));
    buildMenus();
    buildUi();
    applyTheme();

    connect(m_backend, &platform::Backend::rawEvent, &m_recorder, &core::Recorder::feed);
    connect(m_backend, &platform::Backend::hotkeyPressed, this, &MainWindow::onHotkey);
    connect(m_backend, &platform::Backend::userInput, this, [this] {
        if (m_state == State::Playing && m_s.stopOnUserInput)
            stopAll(QStringLiteral("Stopped: you moved the mouse or pressed a key (Stop if I move the mouse is on)."));
    });
    connect(&m_recorder, &core::Recorder::actionAdded, this, [this](const Action& a) {
        m_model.insert(m_recordInsert++, a);
        m_table->scrollTo(m_model.index(m_recordInsert - 1, 0));
        log(QStringLiteral("Recorded: %1 · %2").arg(a.typeName(), a.valueText()), true);
        updateHeader();
    });
    connect(&m_recorder, &core::Recorder::lastActionChanged, this, [this](const Action& a) {
        m_model.replace(m_recordInsert - 1, a);
        updateHeader();
    });
    connect(m_player, &core::Player::step, this, [this](int index, int round, int rounds) {
        m_model.setPlayingRow(index);
        if (m_s.detailedLog) log(QStringLiteral("Step %1: %2").arg(index + 1).arg(m_model.at(index).typeName()), true);
        const QString r = rounds > 0 ? QStringLiteral("%1 of %2").arg(round).arg(rounds) : QStringLiteral("round %1").arg(round);
        setPill(QStringLiteral("▶ Playing %1 · step %2 of %3").arg(r).arg(index + 1).arg(m_model.rowCount()), QStringLiteral("ok"));
    });
    connect(m_player, &core::Player::message, this, [this](const QString& t, bool) { log(t); });
    connect(m_player, &core::Player::notify, this, [this](const QString& t) {
        log(QStringLiteral("Notification: %1").arg(t));
        static QSystemTrayIcon* tray = nullptr;
        if (!tray) {
            tray = new QSystemTrayIcon(windowIcon(), this);
        }
        tray->show();
        tray->showMessage(QStringLiteral(APP_NAME), t, QSystemTrayIcon::Information, 5000);
    });
    connect(m_player, &core::Player::finished, this, [this](bool stopped, const QString& why) {
        m_backend->setCapture(platform::CaptureMode::Off);
        m_model.setPlayingRow(-1);
        if (!why.isEmpty()) log(QStringLiteral("Stopped: %1").arg(why));
        else log(stopped ? QStringLiteral("Playback stopped.") : QStringLiteral("Playback finished."));
        setState(State::Idle);
        registerHotkeys();   // releases the Stop hotkey (Esc) so it works normally in other programs again
        if (isMinimized() && m_s.hideWhilePlaying) showNormal();
        if (m_quitWhenDone) qApp->quit();
    });

    m_countdown.setInterval(1000);
    connect(&m_countdown, &QTimer::timeout, this, [this] {
        if (--m_countdownLeft > 0) {
            setPill(QStringLiteral("Starting in %1 s…").arg(m_countdownLeft), QStringLiteral("warn"));
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
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { m_s.save(appdata::settingsFile()); });
    connect(&m_model, &ActionModel::edited, this, [this] { setDirty(true); });

    // Size: remembered, or a comfortable size that fits the screen.
    if (m_s.geometry.isEmpty() || !restoreGeometry(m_s.geometry)) {
        const QRect avail = screen()->availableGeometry();
        resize(qMin(1320, int(avail.width() * 0.9)), qMin(860, int(avail.height() * 0.9)));
        move(avail.center() - rect().center());
    }
    if (m_s.alwaysOnTop) setWindowFlag(Qt::WindowStaysOnTopHint, true);

    registerHotkeys();
    const QString why = m_backend->unavailableReason();
    log(why.isEmpty() ? QStringLiteral("Ready. Input method: %1.").arg(m_backend->name()) : why);
    setState(State::Idle);
}

MainWindow::~MainWindow()
{
    m_player->stop();
}

// ------------------------------------------------------------------------------------------------ building

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("&New"), QKeySequence::New, this, [this] { newMacro(); });
    file->addAction(QStringLiteral("&Open…"), QKeySequence::Open, this, [this] { open(); });
    m_recentMenu = file->addMenu(QStringLiteral("Open &recent"));
    rebuildRecentMenu();
    file->addSeparator();
    m_saveAct = file->addAction(QStringLiteral("&Save"), QKeySequence::Save, this, [this] { save(false); });
    file->addAction(QStringLiteral("Save &as…"), QKeySequence::SaveAs, this, [this] { save(true); });
    file->addSeparator();
    QMenu* settings = file->addMenu(QStringLiteral("Se&ttings"));
    settings->addAction(QStringLiteral("Change app data folder…"), this, [this] {
        QString msg;
        if (appdata::change(this, &msg)) { m_s.save(appdata::settingsFile()); log(msg); }
        else if (!msg.isEmpty()) QMessageBox::warning(this, QStringLiteral("App data folder"), msg);
    });
    settings->addAction(QStringLiteral("Reset settings to defaults…"), this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Reset settings"),
                                  QStringLiteral("Reset playback, recording and hotkey settings to their defaults?\n"
                                                 "Your theme, layout and recent files are kept.")) != QMessageBox::Yes)
            return;
        m_s.resetOptions();
        m_s.save(appdata::settingsFile());
        syncWidgetsFromSettings();
        registerHotkeys();
        log(QStringLiteral("Settings reset to defaults."));
    });
    file->addSeparator();
    file->addAction(QStringLiteral("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* macro = menuBar()->addMenu(QStringLiteral("&Macro"));
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
    m_buttonActions << qMakePair(mirror(QStringLiteral("&Record"), [this] { return m_recordBtn; }), static_cast<QPushButton*>(nullptr));
    m_buttonActions << qMakePair(mirror(QStringLiteral("&Play"), [this] { return m_playBtn; }), static_cast<QPushButton*>(nullptr));
    m_buttonActions << qMakePair(mirror(QStringLiteral("Play &selection"), [this] { return m_playSelBtn; }), static_cast<QPushButton*>(nullptr));
    QAction* fromHere = macro->addAction(QStringLiteral("Play from selected &step"), this, [this] { startPlaying(false, true); });
    m_editActs << fromHere;
    m_buttonActions << qMakePair(mirror(QStringLiteral("P&ause / resume"), [this] { return m_pauseBtn; }), static_cast<QPushButton*>(nullptr));
    m_buttonActions << qMakePair(mirror(QStringLiteral("S&top"), [this] { return m_stopBtn; }), static_cast<QPushButton*>(nullptr));
    macro->addSeparator();
    QMenu* add = buildAddMenu(this);
    add->setTitle(QStringLiteral("&Add action"));
    macro->addMenu(add);
    macro->addAction(QStringLiteral("&Clean up"), this, [this] { cleanUp(); })
        ->setToolTip(QStringLiteral("Merges back-to-back waits and moves, and removes empty ones."));

    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    m_undoAct = edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, this, [this] { undo(); });
    m_redoAct = edit->addAction(QStringLiteral("&Redo"), QKeySequence::Redo, this, [this] { redo(); });
    edit->addSeparator();
    m_editActs << edit->addAction(QStringLiteral("Cu&t"), QKeySequence::Cut, this, [this] { copySelected(true); });
    m_editActs << edit->addAction(QStringLiteral("&Copy"), QKeySequence::Copy, this, [this] { copySelected(false); });
    edit->addAction(QStringLiteral("&Paste"), QKeySequence::Paste, this, [this] { paste(); });
    m_editActs << edit->addAction(QStringLiteral("&Delete"), QKeySequence::Delete, this, [this] { deleteSelected(); });
    edit->addAction(QStringLiteral("Select &all"), QKeySequence::SelectAll, this, [this] { m_table->selectAll(); });
    edit->addSeparator();
    m_editActs << edit->addAction(QStringLiteral("&Edit step…"), QKeySequence(Qt::Key_F2), this, [this] {
        const auto rows = selectedRows();
        if (!rows.isEmpty()) editRow(rows.first());
    });
    m_editActs << edit->addAction(QStringLiteral("E&nable / disable"), QKeySequence(Qt::CTRL | Qt::Key_E), this, [this] { toggleEnabled(); });
    m_editActs << edit->addAction(QStringLiteral("Move &up"), QKeySequence(Qt::ALT | Qt::Key_Up), this, [this] { moveSelected(-1); });
    m_editActs << edit->addAction(QStringLiteral("Move do&wn"), QKeySequence(Qt::ALT | Qt::Key_Down), this, [this] { moveSelected(1); });

    m_viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    QMenu* theme = m_viewMenu->addMenu(QStringLiteral("&Theme"));
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    auto themeAction = [&](QMenu* menu, const QString& text, const QString& key) {
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
    };
    themeAction(theme, QStringLiteral("Match operating system"), QStringLiteral("system"));
    for (const QString& g : themes::groups()) {
        QMenu* sub = theme->addMenu(g);
        QList<const themes::Theme*> list;
        for (const themes::Theme& t : themes::all())
            if (t.group == g) list << &t;
        std::sort(list.begin(), list.end(), [](auto* a, auto* b) { return a->name.compare(b->name, Qt::CaseInsensitive) < 0; });
        for (auto* t : list) themeAction(sub, t->name, t->key);
    }
    connect(m_viewMenu, &QMenu::aboutToHide, this, [this] { endThemePreview(); });
    connect(theme, &QMenu::aboutToHide, this, [this] { endThemePreview(); });
    m_viewMenu->addSeparator();
    m_logAct = m_viewMenu->addAction(QStringLiteral("Show activity &log"));
    m_logAct->setCheckable(true);
    m_logAct->setChecked(m_s.showLog);
    connect(m_logAct, &QAction::toggled, this, [this](bool on) { setCardOpen(QStringLiteral("ACTIVITY"), on); });
    QAction* detail = m_viewMenu->addAction(QStringLiteral("&Detailed activity log"));
    detail->setCheckable(true);
    detail->setChecked(m_s.detailedLog);
    detail->setToolTip(QStringLiteral("Also logs every recorded action and every step played."));
    connect(detail, &QAction::toggled, this, [this](bool on) { m_s.detailedLog = on; saveSettingsSoon(); });
    QAction* top = m_viewMenu->addAction(QStringLiteral("Always on &top"));
    top->setCheckable(true);
    top->setChecked(m_s.alwaysOnTop);
    connect(top, &QAction::toggled, this, [this](bool on) {
        m_s.alwaysOnTop = on;
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        show();
        saveSettingsSoon();
    });
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(QStringLiteral("&Reset view options"), this, [this] {
        m_s.resetView();
        for (QAction* a : m_themeGroup->actions()) a->setChecked(a->data().toString() == m_s.theme);
        for (auto it = m_cards.cbegin(); it != m_cards.cend(); ++it) setCardOpen(it.key(), true, false);
        m_logAct->setChecked(true);
        setWindowFlag(Qt::WindowStaysOnTopHint, false);
        show();
        m_table->horizontalHeader()->resizeSections(QHeaderView::Interactive);
        applyTheme();
        saveSettingsSoon();
    });

    QMenu* help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("Project &website"), this, [] { QDesktopServices::openUrl(QUrl(kRepoUrl)); });
    help->addAction(QStringLiteral("Open app &data folder"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(appdata::folder()));
    });
    help->addAction(QStringLiteral("&Report a problem"), this, [] { QDesktopServices::openUrl(QUrl(kRepoUrl + QStringLiteral("/issues"))); });
    help->addSeparator();
    help->addAction(QStringLiteral("&About " APP_NAME), this, [this] {
        QMessageBox::about(this, QStringLiteral("About " APP_NAME),
                           QStringLiteral("<b>" APP_NAME " " APP_VERSION "</b><br>Records and plays back mouse and keyboard actions."
                                          "<br><br>Copyright © 2026 cincottash<br>Licensed under the GNU GPL v3.0."
                                          "<br><br>Input method: %1<br><a href=\"%2\">%2</a>").arg(m_backend->name(), kRepoUrl));
    });
}

QMenu* MainWindow::buildAddMenu(QWidget* parent)
{
    auto* menu = new QMenu(parent);
    auto add = [&](QMenu* m, const QString& text, ActionType t) {
        QAction* a = m->addAction(text);
        connect(a, &QAction::triggered, this, [this, t] { addAction(t); });
    };
    QMenu* mouse = menu->addMenu(QStringLiteral("Mouse"));
    add(mouse, QStringLiteral("Click"), ActionType::MouseClick);
    add(mouse, QStringLiteral("Move"), ActionType::MouseMove);
    add(mouse, QStringLiteral("Scroll"), ActionType::Wheel);
    add(mouse, QStringLiteral("Button down (start a drag)"), ActionType::MouseDown);
    add(mouse, QStringLiteral("Button up (end a drag)"), ActionType::MouseUp);
    QMenu* keys = menu->addMenu(QStringLiteral("Keyboard"));
    add(keys, QStringLiteral("Key press / shortcut"), ActionType::KeyPress);
    add(keys, QStringLiteral("Text"), ActionType::Text);
    add(keys, QStringLiteral("Key down (hold)"), ActionType::KeyDown);
    add(keys, QStringLiteral("Key up (release)"), ActionType::KeyUp);
    QMenu* wait = menu->addMenu(QStringLiteral("Wait"));
    add(wait, QStringLiteral("Wait"), ActionType::Wait);
    add(wait, QStringLiteral("Wait for pixel color"), ActionType::WaitPixel);
    add(wait, QStringLiteral("Wait for key"), ActionType::WaitKey);
    QMenu* flow = menu->addMenu(QStringLiteral("Flow"));
    add(flow, QStringLiteral("Repeat"), ActionType::Repeat);
    add(flow, QStringLiteral("Go to"), ActionType::Goto);
    QMenu* other = menu->addMenu(QStringLiteral("Programs and windows"));
    add(other, QStringLiteral("Window focus"), ActionType::WindowFocus);
    add(other, QStringLiteral("Run program"), ActionType::RunProgram);
    add(other, QStringLiteral("Notification"), ActionType::Notify);
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
    auto* t = new QLabel(title);
    t->setObjectName(QStringLiteral("cardTitle"));
    head->addWidget(t);
    head->addStretch();
    if (headerExtra) head->addWidget(headerExtra);
    auto* toggle = linkButton(QStringLiteral("Hide ▾"));
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
    c.toggle->setText(open ? QStringLiteral("Hide ▾") : QStringLiteral("Show ▴"));
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
    m_playSelBtn = new QPushButton(QStringLiteral("▶ Play selection"));
    m_recordBtn = new QPushButton(QStringLiteral("● Record"));
    m_recordBtn->setObjectName(QStringLiteral("record"));
    m_playBtn = new QPushButton(QStringLiteral("▶ Play"));
    m_playBtn->setObjectName(QStringLiteral("primary"));
    m_pauseBtn = new QPushButton(QStringLiteral("❚❚ Pause"));
    m_stopBtn = new QPushButton(QStringLiteral("■ Stop"));
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
    connect(m_playBtn, &QPushButton::clicked, this, [this] { startPlaying(false, false); });
    connect(m_playSelBtn, &QPushButton::clicked, this, [this] { startPlaying(true, false); });
    connect(m_pauseBtn, &QPushButton::clicked, this, [this] { togglePause(); });
    connect(m_stopBtn, &QPushButton::clicked, this, [this] { stopAll(); });

    // Hidden names so automated tests can find each control (not shown anywhere).
    const QList<QPair<QObject*, const char*>> ids = {
        {m_recordBtn, "record"}, {m_playBtn, "play"}, {m_pauseBtn, "pause"}, {m_stopBtn, "stop"}, {m_playSelBtn, "playSel"},
        {m_addBtn, "add"}, {m_editBtn, "edit"}, {m_deleteBtn, "delete"}, {m_upBtn, "up"}, {m_downBtn, "down"},
        {m_speed, "speed"}, {m_rTimes, "rTimes"}, {m_rSecs, "rSecs"}, {m_rForever, "rForever"}, {m_repeatTimes, "repeatTimes"},
        {m_repeatSecs, "repeatSecs"}, {m_pathStyle, "pathStyle"}, {m_typing, "typing"}, {m_countdownCheck, "countdown"},
        {m_countdownSecs, "countdownSecs"}, {m_stopOnInput, "stopOnInput"}, {m_restoreMouse, "restoreMouse"},
        {m_hidePlay, "hidePlay"}, {m_recMoves, "recMoves"}, {m_minMove, "minMove"}, {m_recKeys, "recKeys"},
        {m_mergeTyping, "mergeTyping"}, {m_shortestPause, "shortestPause"}, {m_hideRec, "hideRec"},
        {m_hkRecord, "hkRecord"}, {m_hkPlay, "hkPlay"}, {m_hkStop, "hkStop"}, {m_hkError, "hkError"},
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
    m_speed->setToolTip(QStringLiteral("100 % plays at the recorded speed; 200 % is twice as fast."));
    grid->addWidget(new QLabel(QStringLiteral("Speed")), r, 0);
    grid->addWidget(m_speed, r++, 1);

    m_rTimes = new QRadioButton;
    m_rSecs = new QRadioButton;
    m_rForever = new QRadioButton(QStringLiteral("until stopped"));
    m_repeatTimes = new QSpinBox;
    m_repeatTimes->setRange(1, 1000000);
    m_repeatTimes->setSuffix(QStringLiteral(" times"));
    m_repeatTimes->setValue(m_s.repeatTimes);
    m_repeatSecs = new QSpinBox;
    m_repeatSecs->setRange(1, 864000);
    m_repeatSecs->setPrefix(QStringLiteral("for "));
    m_repeatSecs->setSuffix(QStringLiteral(" s"));
    m_repeatSecs->setValue(m_s.repeatSeconds);
    auto radioRow = [](QRadioButton* rb, QWidget* w) {
        auto* x = new QWidget;
        auto* h = new QHBoxLayout(x);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(rb);
        if (w) h->addWidget(w);
        h->addStretch();
        return x;
    };
    auto* repeatGroup = new QButtonGroup(card);   // one choice, even though each option sits in its own row
    for (QRadioButton* rb : {m_rTimes, m_rSecs, m_rForever}) repeatGroup->addButton(rb);
    grid->addWidget(new QLabel(QStringLiteral("Repeat")), r, 0);
    grid->addWidget(radioRow(m_rTimes, m_repeatTimes), r++, 1);
    grid->addWidget(radioRow(m_rSecs, m_repeatSecs), r++, 1);
    grid->addWidget(radioRow(m_rForever, nullptr), r++, 1);
    (m_s.repeatMode == QLatin1String("seconds") ? m_rSecs : m_s.repeatMode == QLatin1String("forever") ? m_rForever : m_rTimes)->setChecked(true);

    m_pathStyle = new QComboBox;
    m_pathStyle->addItem(QStringLiteral("As recorded"), QStringLiteral("recorded"));
    m_pathStyle->addItem(QStringLiteral("Straight line"), QStringLiteral("linear"));
    m_pathStyle->addItem(QStringLiteral("Smooth curve"), QStringLiteral("spline"));
    m_pathStyle->addItem(QStringLiteral("Human-like"), QStringLiteral("human"));
    m_pathStyle->setCurrentIndex(qMax(0, m_pathStyle->findData(m_s.pathStyle)));
    m_pathStyle->setToolTip(QStringLiteral("How the mouse travels between points. Human-like wobbles and slows down near the target, "
                                           "for programs that notice robotic movement."));
    grid->addWidget(new QLabel(QStringLiteral("Mouse path")), r, 0);
    grid->addWidget(m_pathStyle, r++, 1);

    m_typing = new QComboBox;
    m_typing->addItem(QStringLiteral("As recorded"), QStringLiteral("recorded"));
    m_typing->addItem(QStringLiteral("Instant"), QStringLiteral("instant"));
    m_typing->setCurrentIndex(qMax(0, m_typing->findData(m_s.typing)));
    m_typing->setToolTip(QStringLiteral("As recorded keeps your typing rhythm (some programs miss keys typed too fast)."));
    grid->addWidget(new QLabel(QStringLiteral("Typing")), r, 0);
    grid->addWidget(m_typing, r++, 1);
    grid->setColumnStretch(1, 1);
    b->addLayout(grid);

    m_countdownCheck = new QCheckBox(QStringLiteral("Countdown before playing"));
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
    m_stopOnInput = new QCheckBox(QStringLiteral("Stop if I move the mouse or press a key"));
    m_stopOnInput->setChecked(m_s.stopOnUserInput);
    m_restoreMouse = new QCheckBox(QStringLiteral("Put the mouse back where it was afterwards"));
    m_restoreMouse->setChecked(m_s.restoreMouse);
    m_hidePlay = new QCheckBox(QStringLiteral("Minimize this window while playing"));
    m_hidePlay->setChecked(m_s.hideWhilePlaying);
    b->addWidget(m_stopOnInput);
    b->addWidget(m_restoreMouse);
    b->addWidget(m_hidePlay);

    auto changed = [this] {
        m_s.speedPct = m_speed->value();
        m_s.repeatMode = m_rSecs->isChecked() ? QStringLiteral("seconds") : m_rForever->isChecked() ? QStringLiteral("forever") : QStringLiteral("times");
        m_s.repeatTimes = m_repeatTimes->value();
        m_s.repeatSeconds = m_repeatSecs->value();
        m_s.pathStyle = m_pathStyle->currentData().toString();
        m_s.typing = m_typing->currentData().toString();
        m_s.countdown = m_countdownCheck->isChecked();
        m_s.countdownSeconds = m_countdownSecs->value();
        m_s.stopOnUserInput = m_stopOnInput->isChecked();
        m_s.restoreMouse = m_restoreMouse->isChecked();
        m_s.hideWhilePlaying = m_hidePlay->isChecked();
        m_countdownSecs->setEnabled(m_s.countdown);
        saveSettingsSoon();
    };
    for (QSpinBox* s : {m_speed, m_repeatTimes, m_repeatSecs, m_countdownSecs}) connect(s, &QSpinBox::valueChanged, this, changed);
    for (QRadioButton* rb : {m_rTimes, m_rSecs, m_rForever}) connect(rb, &QRadioButton::toggled, this, changed);
    for (QComboBox* c : {m_pathStyle, m_typing}) connect(c, &QComboBox::currentIndexChanged, this, changed);
    for (QCheckBox* c : {m_countdownCheck, m_stopOnInput, m_restoreMouse, m_hidePlay}) connect(c, &QCheckBox::toggled, this, changed);
    // Typing a number selects its repeat option.
    connect(m_repeatTimes, &QSpinBox::valueChanged, this, [this](int v) {
        m_rTimes->setChecked(true);
        m_repeatTimes->setSuffix(v == 1 ? QStringLiteral(" time") : QStringLiteral(" times"));
    });
    m_repeatTimes->setSuffix(m_repeatTimes->value() == 1 ? QStringLiteral(" time") : QStringLiteral(" times"));
    connect(m_repeatSecs, &QSpinBox::valueChanged, this, [this] { m_rSecs->setChecked(true); });
    m_countdownSecs->setEnabled(m_s.countdown);
    return card;
}

QWidget* MainWindow::buildRecordingCard()
{
    QVBoxLayout* b;
    QFrame* card = makeCard(QStringLiteral("RECORDING"), &b);
    m_recMoves = new QCheckBox(QStringLiteral("Mouse moves"));
    m_recMoves->setChecked(m_s.recordMoves);
    m_recMoves->setToolTip(QStringLiteral("Off: only clicks and scrolls are recorded; the mouse jumps straight to each click."));
    m_minMove = new QSpinBox;
    m_minMove->setRange(1, 100);
    m_minMove->setPrefix(QStringLiteral("smallest "));
    m_minMove->setSuffix(QStringLiteral(" px"));
    m_minMove->setValue(m_s.minMovePx);
    m_minMove->setToolTip(QStringLiteral("Movements smaller than this are left out of recorded paths."));
    auto* row = new QHBoxLayout;
    row->addWidget(m_recMoves);
    row->addWidget(m_minMove);
    row->addStretch();
    b->addLayout(row);
    m_recKeys = new QCheckBox(QStringLiteral("Keyboard"));
    m_recKeys->setChecked(m_s.recordKeyboard);
    m_mergeTyping = new QCheckBox(QStringLiteral("Merge typing into text"));
    m_mergeTyping->setChecked(m_s.mergeTyping);
    m_mergeTyping->setToolTip(QStringLiteral("Typed characters become one Text step instead of one step per key."));
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
    m_shortestPause->setToolTip(QStringLiteral("Pauses at least this long become their own Wait steps you can edit. "
                                               "Shorter pauses are kept with the next step, so timing stays exact."));
    auto* row3 = new QHBoxLayout;
    row3->addWidget(new QLabel(QStringLiteral("Show pauses from")));
    row3->addWidget(m_shortestPause);
    row3->addStretch();
    b->addLayout(row3);
    m_hideRec = new QCheckBox(QStringLiteral("Minimize this window while recording"));
    m_hideRec->setChecked(m_s.hideWhileRecording);
    b->addWidget(m_hideRec);
    auto changed = [this] {
        m_s.recordMoves = m_recMoves->isChecked();
        m_s.minMovePx = m_minMove->value();
        m_s.recordKeyboard = m_recKeys->isChecked();
        m_s.mergeTyping = m_mergeTyping->isChecked();
        m_s.shortestPauseMs = m_shortestPause->value();
        m_s.hideWhileRecording = m_hideRec->isChecked();
        m_mergeTyping->setEnabled(m_s.recordKeyboard);
        saveSettingsSoon();
    };
    for (QCheckBox* c : {m_recMoves, m_recKeys, m_mergeTyping, m_hideRec}) connect(c, &QCheckBox::toggled, this, changed);
    for (QSpinBox* s : {m_minMove, m_shortestPause}) connect(s, &QSpinBox::valueChanged, this, changed);
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
    m_hkRecord = make(QStringLiteral("Record / stop"), m_s.hkRecord, QStringLiteral("Works from any program."), 0);
    m_hkPlay = make(QStringLiteral("Play / pause"), m_s.hkPlay, QStringLiteral("Works from any program."), 1);
    m_hkStop = make(QStringLiteral("Stop"), m_s.hkStop, QStringLiteral("Active only while recording or playing, so the key keeps working normally otherwise."), 2);
    grid->setColumnStretch(1, 1);
    b->addLayout(grid);
    m_hkError = new QLabel;
    m_hkError->setProperty("tone", QStringLiteral("warn"));
    m_hkError->setWordWrap(true);
    m_hkError->hide();
    b->addWidget(m_hkError);
    b->addWidget(muted(QStringLiteral("Hotkeys are never recorded into macros.")));
    for (QKeySequenceEdit* e : {m_hkRecord, m_hkPlay, m_hkStop}) {
        connect(e, &QKeySequenceEdit::editingFinished, this, [this] {
            m_s.hkRecord = m_hkRecord->keySequence().toString(QKeySequence::PortableText);
            m_s.hkPlay = m_hkPlay->keySequence().toString(QKeySequence::PortableText);
            m_s.hkStop = m_hkStop->keySequence().toString(QKeySequence::PortableText);
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
    m_addBtn = toolButton(QStringLiteral("+ Add"), QStringLiteral("Add a step after the selected one (or at the end)."));
    m_addBtn->setMenu(buildAddMenu(m_addBtn));
    m_addBtn->setPopupMode(QToolButton::InstantPopup);
    QVBoxLayout* b;
    QFrame* card = makeCard(QStringLiteral("ACTIONS"), &b);
    auto* bar = new QHBoxLayout;
    m_editBtn = toolButton(QStringLiteral("Edit"), QStringLiteral("Edit the selected step (or double-click it)."));
    m_deleteBtn = toolButton(QStringLiteral("Delete"), QStringLiteral("Delete the selected steps."));
    m_upBtn = toolButton(QStringLiteral("↑"), QStringLiteral("Move up (Alt+Up)"));
    m_downBtn = toolButton(QStringLiteral("↓"), QStringLiteral("Move down (Alt+Down)"));
    for (QToolButton* t : {m_addBtn, m_editBtn, m_deleteBtn, m_upBtn, m_downBtn}) bar->addWidget(t);
    bar->addSpacing(12);
    bar->addWidget(new QLabel(QStringLiteral("Show:")));
    const QList<QPair<QString, QString>> filters = {{QStringLiteral("moves"), QStringLiteral("Moves")},
                                                    {QStringLiteral("clicks"), QStringLiteral("Clicks")},
                                                    {QStringLiteral("keys"), QStringLiteral("Keys")},
                                                    {QStringLiteral("waits"), QStringLiteral("Waits")},
                                                    {QStringLiteral("windows"), QStringLiteral("Windows")}};
    for (const auto& [key, text] : filters) {
        auto* c = new QCheckBox(text);
        c->setChecked(true);
        c->setToolTip(QStringLiteral("Hidden steps still play."));
        m_filters.insert(key, c);
        bar->addWidget(c);
        connect(c, &QCheckBox::toggled, this, [this] { applyFilter(); });
    }
    bar->addStretch();
    b->addLayout(bar);

    m_table = new QTableView;
    m_table->setModel(&m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
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
    if (!m_s.tableState.isEmpty()) h->restoreState(m_s.tableState);
    connect(h, &QHeaderView::sectionResized, this, [this] { m_s.tableState = m_table->horizontalHeader()->saveState(); saveSettingsSoon(); });
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& i) {
        if (i.column() != ActionModel::ColLabel && i.column() != ActionModel::ColComment) editRow(i.row());
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateUi(); });
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
    case State::Idle: setPill(why.isEmpty() ? QStringLiteral("● Ready") : QStringLiteral("● Can't record here"), why.isEmpty() ? QStringLiteral("idle") : QStringLiteral("warn")); break;
    case State::Countdown: break;
    case State::Recording: setPill(QStringLiteral("● Recording"), QStringLiteral("err")); break;
    case State::Playing: setPill(QStringLiteral("▶ Playing"), QStringLiteral("ok")); break;
    }
    updateUi();
}

void MainWindow::updateUi()
{
    const bool idle = m_state == State::Idle;
    const bool busy = m_state == State::Playing || m_state == State::Countdown;
    const QString why = m_backend->unavailableReason();
    const bool canRun = why.isEmpty();
    const bool hasActions = m_model.rowCount() > 0;
    const bool hasSel = !selectedRows().isEmpty();

    m_recordBtn->setText(m_state == State::Recording ? QStringLiteral("■ Stop recording") : QStringLiteral("● Record"));
    m_recordBtn->setEnabled(canRun && (idle || m_state == State::Recording));
    m_recordBtn->setToolTip(!canRun ? why
                            : m_state == State::Recording ? QStringLiteral("Stop recording (%1)").arg(m_s.hkStop)
                            : busy ? QStringLiteral("Stop playback first.")
                                   : QStringLiteral("Record your mouse and keyboard, added to the end of the list (%1).").arg(m_s.hkRecord));
    m_playBtn->setEnabled(canRun && idle && hasActions);
    m_playBtn->setToolTip(!canRun ? why : !hasActions ? QStringLiteral("Record or add some steps first.")
                                        : !idle ? QStringLiteral("Already busy.") : QStringLiteral("Play the whole macro (%1).").arg(m_s.hkPlay));
    m_playSelBtn->setEnabled(canRun && idle && hasSel);
    m_playSelBtn->setToolTip(!canRun ? why : !hasSel ? QStringLiteral("Select the steps to play first.") : QStringLiteral("Play only the selected steps."));
    m_pauseBtn->setEnabled(m_state == State::Playing);
    m_pauseBtn->setText(m_player->paused() ? QStringLiteral("▶ Resume") : QStringLiteral("❚❚ Pause"));
    m_pauseBtn->setToolTip(m_state == State::Playing ? QStringLiteral("Pause or resume playback (%1).").arg(m_s.hkPlay) : QStringLiteral("Only while playing."));
    m_stopBtn->setEnabled(!idle);
    m_stopBtn->setToolTip(idle ? QStringLiteral("Nothing is running.") : QStringLiteral("Stop (%1).").arg(m_s.hkStop));

    for (QToolButton* t : {m_addBtn}) t->setEnabled(idle);
    for (QToolButton* t : {m_editBtn, m_deleteBtn, m_upBtn, m_downBtn}) {
        t->setEnabled(idle && hasSel);
    }
    for (QAction* a : std::as_const(m_editActs)) a->setEnabled(idle && hasSel);
    m_undoAct->setEnabled(idle && !m_undo.isEmpty());
    m_redoAct->setEnabled(idle && !m_redo.isEmpty());
    m_table->setEditTriggers(idle ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed : QAbstractItemView::NoEditTriggers);
    for (QWidget* w : {static_cast<QWidget*>(m_speed), static_cast<QWidget*>(m_pathStyle), static_cast<QWidget*>(m_typing),
                       static_cast<QWidget*>(m_rTimes), static_cast<QWidget*>(m_rSecs), static_cast<QWidget*>(m_rForever),
                       static_cast<QWidget*>(m_repeatTimes), static_cast<QWidget*>(m_repeatSecs)}) {
        if (!w->property("baseTip").isValid()) w->setProperty("baseTip", w->toolTip());
        w->setEnabled(!busy);
        w->setToolTip(busy ? QStringLiteral("Can't be changed while playing. Stop first.") : w->property("baseTip").toString());
    }
    updateHeader();
}

void MainWindow::updateHeader()
{
    const QString name = m_file.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_file).fileName();
    core::Macro m;
    m.actions = m_model.actions();
    const int n = m_model.rowCount();
    m_fileLabel->setText(QStringLiteral("%1%2  ·  %3 action%4  ·  %5")
                             .arg(name, m_dirty ? QStringLiteral("*") : QString())
                             .arg(QLocale(QLocale::English).toString(n), n == 1 ? QString() : QStringLiteral("s"), seconds(m.totalMs())));
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
    const auto r = QMessageBox::question(this, QStringLiteral("Unsaved changes"),
                                         QStringLiteral("Save the changes to %1?").arg(m_file.isEmpty() ? QStringLiteral("this macro") : QFileInfo(m_file).fileName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save(false);
    return true;
}

bool MainWindow::save(bool saveAs)
{
    QString path = m_file;
    if (saveAs || path.isEmpty()) {
        const QString start = m_file.isEmpty() ? QDir(m_s.lastFolder.isEmpty() ? QDir::homePath() : m_s.lastFolder).filePath(QStringLiteral("Untitled.") + kExt) : m_file;
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save macro"), start, QStringLiteral("Macros (*.%1)").arg(kExt));
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += QLatin1Char('.') + kExt;
    }
    core::Macro m;
    m.actions = m_model.actions();
    QString err;
    if (!m.save(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Couldn't save"), QStringLiteral("Couldn't save %1:\n%2").arg(path, err));
        return false;
    }
    m_file = path;
    m_s.lastFolder = QFileInfo(path).absolutePath();
    addRecent(path);
    setDirty(false);
    log(QStringLiteral("Saved %1 (%2 actions).").arg(QFileInfo(path).fileName()).arg(m.actions.size()));
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
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open macro"),
                                                      m_s.lastFolder.isEmpty() ? QDir::homePath() : m_s.lastFolder,
                                                      QStringLiteral("Macros (*.%1);;All files (*)").arg(kExt));
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("Not found"), QStringLiteral("%1 doesn't exist any more.").arg(path));
        m_s.recent.removeAll(path);
        rebuildRecentMenu();
        return false;
    }
    core::Macro m;
    QString err;
    if (!core::Macro::load(path, &m, &err)) {
        QMessageBox::warning(this, QStringLiteral("Couldn't open"), QStringLiteral("Couldn't open %1:\n%2").arg(path, err));
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
    log(QStringLiteral("Opened %1 (%2 actions).").arg(QFileInfo(path).fileName()).arg(m.actions.size()));
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

void MainWindow::addAction(ActionType t)
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
    case ActionType::WaitPixel: a.ms = 30000; break;
    case ActionType::Repeat: a.count = 2; break;
    case ActionType::KeyPress: case ActionType::KeyDown: case ActionType::KeyUp: case ActionType::WaitKey: a.key = QStringLiteral("Enter"); break;
    default: break;
    }
    ActionDialog d(a, labels(), m_backend->injector(), this);
    d.setWindowTitle(QStringLiteral("Add: %1").arg(a.typeName()));
    if (d.exec() != QDialog::Accepted) return;
    snapshot();
    const int row = insertRow();
    m_model.insert(row, d.result());
    selectRows(row, row);
    setDirty(true);
    applyFilter();
    updateUi();
}

void MainWindow::editRow(int row)
{
    if (m_state != State::Idle || row < 0 || row >= m_model.rowCount()) return;
    ActionDialog d(m_model.at(row), labels(), m_backend->injector(), this);
    if (d.exec() != QDialog::Accepted) return;
    snapshot();
    m_model.replace(row, d.result());
    setDirty(true);
    updateUi();
}

void MainWindow::deleteSelected()
{
    const auto rows = selectedRows();
    if (rows.isEmpty() || m_state != State::Idle) return;
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
    if (cut) deleteSelected();
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
    if (list.isEmpty()) { log(QStringLiteral("Nothing to paste: copy some steps first.")); return; }
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
    if (last - first + 1 != rows.size()) { log(QStringLiteral("Select a single block of steps to move them.")); return; }
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
    if (!merged && !removed) { log(QStringLiteral("Clean up: nothing to tidy.")); return; }
    snapshot();
    m_model.setActions(out);
    setDirty(true);
    applyFilter();
    updateUi();
    log(QStringLiteral("Clean up: merged %1 step(s), removed %2 empty step(s).").arg(merged).arg(removed));
}

void MainWindow::applyFilter()
{
    for (int r = 0; r < m_model.rowCount(); ++r) {
        const QString k = filterKey(m_model.at(r).type);
        const bool show = !m_filters.contains(k) || m_filters[k]->isChecked();
        m_table->setRowHidden(r, !show);
    }
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
                                 m_typing, m_countdownCheck, m_stopOnInput, m_restoreMouse, m_hidePlay, m_recMoves, m_minMove,
                                 m_recKeys, m_mergeTyping, m_shortestPause, m_hideRec, m_hkRecord, m_hkPlay, m_hkStop};
    for (QWidget* w : all) w->blockSignals(true);
    m_speed->setValue(m_s.speedPct);
    m_repeatTimes->setValue(m_s.repeatTimes);
    m_repeatTimes->setSuffix(m_s.repeatTimes == 1 ? QStringLiteral(" time") : QStringLiteral(" times"));
    m_repeatSecs->setValue(m_s.repeatSeconds);
    (m_s.repeatMode == QLatin1String("seconds") ? m_rSecs : m_s.repeatMode == QLatin1String("forever") ? m_rForever : m_rTimes)->setChecked(true);
    m_pathStyle->setCurrentIndex(qMax(0, m_pathStyle->findData(m_s.pathStyle)));
    m_typing->setCurrentIndex(qMax(0, m_typing->findData(m_s.typing)));
    m_countdownCheck->setChecked(m_s.countdown);
    m_countdownSecs->setValue(m_s.countdownSeconds);
    m_countdownSecs->setEnabled(m_s.countdown);
    m_stopOnInput->setChecked(m_s.stopOnUserInput);
    m_restoreMouse->setChecked(m_s.restoreMouse);
    m_hidePlay->setChecked(m_s.hideWhilePlaying);
    m_recMoves->setChecked(m_s.recordMoves);
    m_minMove->setValue(m_s.minMovePx);
    m_recKeys->setChecked(m_s.recordKeyboard);
    m_mergeTyping->setChecked(m_s.mergeTyping);
    m_mergeTyping->setEnabled(m_s.recordKeyboard);
    m_shortestPause->setValue(m_s.shortestPauseMs);
    m_hideRec->setChecked(m_s.hideWhileRecording);
    m_hkRecord->setKeySequence(QKeySequence(m_s.hkRecord));
    m_hkPlay->setKeySequence(QKeySequence(m_s.hkPlay));
    m_hkStop->setKeySequence(QKeySequence(m_s.hkStop));
    for (QWidget* w : all) w->blockSignals(false);
}

void MainWindow::registerHotkeys()
{
    if (!m_hotkeysEnabled) return;   // screenshot / test mode: show the hotkeys, never register them
    QStringList problems;
    QString err;
    if (!m_backend->setHotkey(HkRecord, QKeySequence(m_s.hkRecord), &err) && !m_s.hkRecord.isEmpty()) problems << QStringLiteral("Record / stop: ") + err;
    if (!m_backend->setHotkey(HkPlay, QKeySequence(m_s.hkPlay), &err) && !m_s.hkPlay.isEmpty()) problems << QStringLiteral("Play / pause: ") + err;
    // Stop is registered only while something runs, so the key works normally in other programs otherwise.
    m_backend->setHotkey(HkStop, (m_state == State::Idle) ? QKeySequence() : QKeySequence(m_s.hkStop), &err);
    if (m_hkError) {
        m_hkError->setText(problems.join(QLatin1Char('\n')));
        m_hkError->setVisible(!problems.isEmpty());
    }
    if (m_s.hkStop.isEmpty() && m_hkError) {
        m_hkError->setText(m_hkError->text() + (problems.isEmpty() ? QString() : QStringLiteral("\n"))
                           + QStringLiteral("No Stop hotkey: you'll need to click Stop (or move the mouse) to stop playback."));
        m_hkError->show();
    }
    if (m_recordBtn) updateUi();
}

void MainWindow::onHotkey(int id)
{
    if (id == HkStop) { stopAll(); return; }
    if (id == HkRecord) {
        if (m_state == State::Recording) stopRecording();
        else if (m_state == State::Idle) startRecording();
        return;
    }
    if (id == HkPlay) {
        if (m_state == State::Playing) togglePause();
        else if (m_state == State::Idle) startPlaying(false, false);
    }
}

void MainWindow::startRecording()
{
    if (m_state != State::Idle) return;
    const QString why = m_backend->unavailableReason();
    if (!why.isEmpty()) { QMessageBox::information(this, QStringLiteral("Can't record"), why); return; }
    m_beforeRecording = m_model.actions();
    m_recordInsert = m_model.rowCount();
    m_recorder.start(recordOptions(), platform::Backend::now(), m_backend->injector()->cursor());
    m_backend->setCapture(platform::CaptureMode::Record);
    setState(State::Recording);
    registerHotkeys();
    log(QStringLiteral("Recording… press %1 or click Stop recording to finish. Clicks on this window aren't recorded.")
            .arg(m_s.hkStop.isEmpty() ? m_s.hkRecord : m_s.hkStop));
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
    m.actions = m_model.actions().mid(m_beforeRecording.size());
    log(QStringLiteral("Recording stopped · %1 action%2 · %3.").arg(added).arg(added == 1 ? QString() : QStringLiteral("s")).arg(seconds(m.totalMs())));
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}

void MainWindow::startPlaying(bool selectionOnly, bool fromSelection, int repeatOverride)
{
    if (m_state != State::Idle || m_model.rowCount() == 0) return;
    const QString why = m_backend->unavailableReason();
    if (!why.isEmpty()) { QMessageBox::information(this, QStringLiteral("Can't play"), why); return; }
    m_pending = playOptions();
    if (repeatOverride > 0) { m_pending.repeatMode = core::RepeatMode::Times; m_pending.repeatCount = repeatOverride; }
    const auto rows = selectedRows();
    if ((selectionOnly || fromSelection) && rows.isEmpty()) return;
    if (selectionOnly) { m_pending.first = rows.first(); m_pending.last = rows.last(); }
    else if (fromSelection) { m_pending.first = rows.first(); m_pending.last = -1; }
    if (m_s.countdown && !m_quitWhenDone) {
        m_countdownLeft = m_s.countdownSeconds;
        setState(State::Countdown);
        registerHotkeys();
        setPill(QStringLiteral("Starting in %1 s…").arg(m_countdownLeft), QStringLiteral("warn"));
        log(QStringLiteral("Playing in %1 s. Switch to the window the macro works in.").arg(m_countdownLeft));
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
    if (m_s.stopOnUserInput) m_backend->setCapture(platform::CaptureMode::Watch);
    const QString how = m_pending.repeatMode == core::RepeatMode::Times
                            ? QStringLiteral("%1 time%2").arg(m_pending.repeatCount).arg(m_pending.repeatCount == 1 ? QString() : QStringLiteral("s"))
                        : m_pending.repeatMode == core::RepeatMode::Seconds ? QStringLiteral("for %1 s").arg(m_pending.repeatCount)
                                                                            : QStringLiteral("until stopped");
    log(QStringLiteral("Playing %1 at %2 %3.").arg(how).arg(m_s.speedPct).arg(QStringLiteral("%")));
    if (m_s.hideWhilePlaying) showMinimized();
    m_player->start(m, m_pending);
}

void MainWindow::togglePause()
{
    if (m_state != State::Playing) return;
    m_player->setPaused(!m_player->paused());
    if (m_player->paused()) {
        m_backend->setCapture(platform::CaptureMode::Off);  // the user may use the mouse while paused
        setPill(QStringLiteral("❚❚ Paused"), QStringLiteral("warn"));
        log(QStringLiteral("Paused. Press %1 or Resume to continue.").arg(m_s.hkPlay));
    } else {
        if (m_s.stopOnUserInput) m_backend->setCapture(platform::CaptureMode::Watch);
        setPill(QStringLiteral("▶ Playing"), QStringLiteral("ok"));
        log(QStringLiteral("Resumed."));
    }
    updateUi();
}

void MainWindow::stopAll(const QString& why)
{
    switch (m_state) {
    case State::Recording: stopRecording(); break;
    case State::Countdown:
        m_countdown.stop();
        log(QStringLiteral("Playback cancelled."));
        setState(State::Idle);
        registerHotkeys();
        break;
    case State::Playing:
        if (!why.isEmpty()) log(why);
        m_backend->setCapture(platform::CaptureMode::Off);
        m_player->stop();
        m_player->setPaused(false);
        break;  // finished() resets the state
    case State::Idle: break;
    }
}

void MainWindow::playFromCommandLine(int repeat, bool quitWhenDone)
{
    m_quitWhenDone = quitWhenDone;
    startPlaying(false, false, repeat);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_state != State::Idle) stopAll();
    if (!maybeSave()) { e->ignore(); return; }
    m_s.geometry = saveGeometry();
    m_s.tableState = m_table->horizontalHeader()->saveState();
    m_saveTimer.stop();
    m_s.save(appdata::settingsFile());
    e->accept();
}
