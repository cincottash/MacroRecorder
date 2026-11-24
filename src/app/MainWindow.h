// The main window: settings cards on the left, the action list and activity log on the right, actions at the bottom.
#pragma once

#include "ActionModel.h"
#include "Settings.h"
#include "core/Player.h"
#include "core/Recorder.h"
#include "platform/Backend.h"

#include <functional>

#include <QDateTime>
#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QTimer>

class ActionDialog;
class ActionPreview;
class ActionTable;
class QAction;
class QActionGroup;
class QBoxLayout;
class QCheckBox;
class QComboBox;
class QFrame;
class QKeySequenceEdit;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QMessageBox;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTableView;
class QToolButton;
class QVBoxLayout;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Settings& s, bool hotkeys = true, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);
    // Command line: play the open macro now (optionally a number of times), then quit if asked.
    void playFromCommandLine(int repeat, bool quitWhenDone);
    // Command line: record now; when recording stops, save to `path` and quit.
    void recordToFileAndQuit(const QString& path);
    // View > Theme > Import Theme…, without the file dialog. Adds the theme, keeps it in the settings and switches
    // to it. False (with the reason in `error`) if the file isn't a usable theme.
    bool importThemeFile(const QString& path, QString* error = nullptr);
    // File > Settings > Start Over starts the app again with this. Tests replace it so nothing is started; the
    // default starts the program (the AppImage itself on Linux) detached.
    inline static std::function<bool(const QString& program, const QStringList& args)> startAgain;
    // Schedules: the clock they follow (tests set their own). Empty: the computer's clock.
    inline static std::function<QDateTime()> clockForTests;
    void checkSchedules();   // plays a schedule that's due; a timer calls it every second
    // Plays a saved macro without opening it (Macro Hotkeys, Schedules, another start of the app). False if it
    // can't start now (busy, a dialog open, a bad file); the reason is in the Activity log.
    bool playFile(const QString& path, const QString& why);

protected:
    bool eventFilter(QObject* watched, QEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    enum class State { Idle, Countdown, Recording, Playing };
    enum class PlayRange { All, Selection, FromSelected, UpToSelected, ThisStep };
    enum class RecordWhere { End, AfterSelection, New };
    struct Card { QFrame* frame; QWidget* body; QToolButton* toggle; };

    // building
    void buildMenus();
    void buildUi();
    QFrame* makeCard(const QString& title, QVBoxLayout** body, QWidget* headerExtra = nullptr);
    QWidget* buildPlaybackCard();
    QWidget* buildRecordingCard();
    QWidget* buildHotkeysCard();
    QWidget* buildActionsCard();
    QWidget* buildActivityCard();
    QMenu* buildAddMenu(QWidget* parent);
    void setCardOpen(const QString& name, bool open, bool save = true);
    void updateStretch();

    // state
    void setState(State s);
    void updateUi();
    void updateHeader();
    void setPill(const QString& text, const QString& tone);
    void log(const QString& text, bool detailed = false);
    void saveSettingsSoon();
    void applyTheme();
    void previewTheme(const QString& key);
    void endThemePreview();
    QAction* addThemeAction(QMenu* menu, const QString& text, const QString& key);
    void rebuildCustomThemeMenus();   // the Custom Themes and Remove Custom Theme submenus
    void importTheme();               // asks for the file
    void exportTheme();
    void removeCustomTheme(const QString& key);
    void applyOpacity();              // View > Window Opacity, on the window and whatever it has open
    bool followsOpacity(const QWidget* w) const;   // any other window of this app

    // file
    bool maybeSave();
    bool save(bool saveAs);
    void newMacro();
    void open();
    void addRecent(const QString& path);
    void rebuildRecentMenu();
    void setDirty(bool d);

    // editing
    void snapshot();
    void undo();
    void redo();
    QList<int> selectedRows() const;
    int insertRow() const;          // where new actions go: after the selection, or at the end
    void addAction(core::ActionType t, int variant = -1);   // variant: Find Image's kind (see buildAddMenu)
    int groupCount() const;
    void groupSelected();     // Edit > Group Selected Steps
    void ungroupSelected();
    void foldAll(bool fold);
    void editRow(int row);
    void deleteSelected(bool ask = true);
    void copySelected(bool cut);
    void paste();
    void moveSelected(int delta);
    void toggleEnabled();
    void cleanUp();
    void combineSelected();   // button down + up -> click, key down + up -> key press
    void findReplace();
    void exportCsv();
    void copyCommandLine();
    void applyFilter();
    void selectRows(int first, int last);
    QStringList labels() const;

    // record / play
    void startRecording(RecordWhere where = RecordWhere::End);
    void stopRecording();
    void startPlaying(PlayRange range, int repeatOverride = 0);
    void beginPlayback();
    void applyWatch();          // while playing: watch for the user's input / block the keyboard, as the options say
    void runAfterPlayback();    // "When finished": close, lock, sleep, log off, restart or shut down
    void showMessageBox(const QString& text);
    void showAskBox(const QString& question, const QString& defaultText);   // an Ask for Text step
    void updateMarker();        // the selected step's position, marked on the screen
    void showPreview(int row);  // -1 hides it
    int execDialog(ActionDialog& d);
    void trigger(const QString& menuText);   // runs the menu item with this text
    void stopAll(const QString& why = {});
    void togglePause();
    void registerHotkeys();
    void syncWidgetsFromSettings();   // after Reset settings: show the defaults right away
    void startOver();                 // every setting back to its default, then the app starts again (welcome window)
    void editMacroHotkeys();          // Macro > Macro Hotkeys…
    void editSchedules();             // Macro > Schedules…
    void reschedule();                // when each schedule is next due, from now on
    QDateTime now() const;
    QHash<QString, QString> appHotkeys() const;   // the Hotkeys card's keys, by name
    bool typingHotkey() const;        // a hotkey field has the keyboard: global hotkeys step aside so it gets the keys
    void onHotkey(int id);
    core::PlayOptions playOptions() const;
    core::RecordOptions recordOptions() const;

    Settings& m_s;
    platform::Backend* m_backend;
    core::Recorder m_recorder;
    core::Player* m_player;
    ActionModel m_model;
    State m_state = State::Idle;

    QString m_file;
    bool m_dirty = false;
    QList<QList<core::Action>> m_undo, m_redo;
    int m_recordInsert = 0;
    QList<core::Action> m_beforeRecording;

    // playback bookkeeping
    core::PlayOptions m_pending;
    int m_countdownLeft = 0;
    QTimer m_countdown;
    bool m_quitWhenDone = false;
    bool m_hotkeysEnabled = true;
    bool m_startingOver = false;   // Start Over: closing without saving the settings again
    QString m_playingOther;              // playing a macro file that isn't the open one (hotkey, schedule): its path
    QList<core::Action> m_otherActions;  // ...and its steps
    QTimer m_scheduleTimer;
    QList<QDateTime> m_due;              // per schedule: when it's next due (invalid: not again)
    QSet<int> m_waitLogged;              // schedules whose "waiting" line is already in the log
    int m_macroHotkeysRegistered = 0;
    bool m_hotkeysPaused = false;        // a hotkey field has the keyboard (see typingHotkey)
    QString m_recordTo;
    QMessageBox* m_msgBox = nullptr;       // a Message box step waiting for OK
    class QDialog* m_askBox = nullptr;     // an Ask for Text step waiting for an answer
    ActionDialog* m_openDialog = nullptr;  // a step editor that's open (for the Capture position hotkey)
    QWidget* m_marker = nullptr;
    QHash<int, bool> m_effectsDefault;   // the system's menu/drop-down/tooltip animations (Qt::UIEffect), at start
    class QSystemTrayIcon* m_tray = nullptr;    // created for the first notification

    // widgets
    QLabel *m_fileLabel = nullptr, *m_pill = nullptr;
    QToolButton* m_notKept = nullptr;   // header link while Don't Keep App Data is on
    QToolButton* m_limits = nullptr;    // header link where the input method reaches only some programs (Wayland)
    QHash<QString, Card> m_cards;
    QVBoxLayout* m_rightCol = nullptr;
    QWidget *m_actionsCard = nullptr, *m_activityCard = nullptr;
    ActionTable* m_table = nullptr;
    ActionPreview* m_preview = nullptr;   // the hovered step's path or position, drawn in the list's right-hand side
    QPlainTextEdit* m_log = nullptr;
    QPushButton *m_recordBtn = nullptr, *m_playBtn = nullptr, *m_pauseBtn = nullptr, *m_stopBtn = nullptr, *m_playSelBtn = nullptr;
    QToolButton *m_addBtn = nullptr, *m_editBtn = nullptr, *m_deleteBtn = nullptr, *m_upBtn = nullptr, *m_downBtn = nullptr;
    QHash<QString, QCheckBox*> m_filters;
    QSpinBox *m_speed = nullptr, *m_repeatTimes = nullptr, *m_repeatSecs = nullptr, *m_countdownSecs = nullptr;
    QRadioButton *m_rTimes = nullptr, *m_rSecs = nullptr, *m_rForever = nullptr;
    QComboBox *m_pathStyle = nullptr, *m_typing = nullptr;
    QLabel* m_timesUnit = nullptr;
    QComboBox* m_after = nullptr;
    QCheckBox *m_countdownCheck = nullptr, *m_stopOnKey = nullptr, *m_stopOnMouse = nullptr, *m_blockKeys = nullptr;
    QCheckBox *m_restoreMouse = nullptr, *m_hidePlay = nullptr, *m_showNotif = nullptr;
    QCheckBox *m_recMoves = nullptr, *m_recKeys = nullptr, *m_mergeTyping = nullptr, *m_hideRec = nullptr, *m_relPos = nullptr;
    QSpinBox *m_minMove = nullptr, *m_shortestPause = nullptr;
    QSlider* m_pathDetail = nullptr;
    QLabel* m_pathDetailValue = nullptr;
    QKeySequenceEdit *m_hkRecord = nullptr, *m_hkPlay = nullptr, *m_hkStop = nullptr;
    QKeySequenceEdit *m_hkNewRec = nullptr, *m_hkPlayFrom = nullptr, *m_hkPlaySel = nullptr, *m_hkCapture = nullptr;
    QLabel* m_hkError = nullptr;

    // menus
    QMenu* m_recentMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QAction *m_undoAct = nullptr, *m_redoAct = nullptr, *m_saveAct = nullptr, *m_logAct = nullptr;
    QAction *m_confirmDeleteAct = nullptr, *m_startMinAct = nullptr, *m_loginAct = nullptr, *m_scanAct = nullptr, *m_markerAct = nullptr;
    QAction* m_previewAct = nullptr;
    QList<QPair<QAction*, QPushButton*>> m_buttonActions;
    QList<QAction*> m_editActs;                  // need a selection and Idle
    QActionGroup* m_themeGroup = nullptr;
    QMenu *m_customThemeMenu = nullptr, *m_removeThemeMenu = nullptr;
    QSlider* m_opacitySlider = nullptr;   // View > Window Opacity
    QString m_themePreviewing;
    QTimer m_themePreviewTimer;
    QTimer m_saveTimer;
};
