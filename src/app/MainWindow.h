// The main window: settings cards on the left, the action list and activity log on the right, actions at the bottom.
#pragma once

#include "ActionModel.h"
#include "Settings.h"
#include "core/Player.h"
#include "core/Recorder.h"
#include "platform/Backend.h"

#include <QHash>
#include <QMainWindow>
#include <QTimer>

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
class QRadioButton;
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

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    enum class State { Idle, Countdown, Recording, Playing };
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
    void addAction(core::ActionType t);
    void editRow(int row);
    void deleteSelected();
    void copySelected(bool cut);
    void paste();
    void moveSelected(int delta);
    void toggleEnabled();
    void cleanUp();
    void applyFilter();
    void selectRows(int first, int last);
    QStringList labels() const;

    // record / play
    void startRecording();
    void stopRecording();
    void startPlaying(bool selectionOnly, bool fromSelection, int repeatOverride = 0);
    void beginPlayback();
    void stopAll(const QString& why = {});
    void togglePause();
    void registerHotkeys();
    void syncWidgetsFromSettings();   // after Reset settings: show the defaults right away
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
    QString m_recordTo;

    // widgets
    QLabel *m_fileLabel = nullptr, *m_pill = nullptr;
    QHash<QString, Card> m_cards;
    QVBoxLayout* m_rightCol = nullptr;
    QWidget *m_actionsCard = nullptr, *m_activityCard = nullptr;
    QTableView* m_table = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QPushButton *m_recordBtn = nullptr, *m_playBtn = nullptr, *m_pauseBtn = nullptr, *m_stopBtn = nullptr, *m_playSelBtn = nullptr;
    QToolButton *m_addBtn = nullptr, *m_editBtn = nullptr, *m_deleteBtn = nullptr, *m_upBtn = nullptr, *m_downBtn = nullptr;
    QHash<QString, QCheckBox*> m_filters;
    QSpinBox *m_speed = nullptr, *m_repeatTimes = nullptr, *m_repeatSecs = nullptr, *m_countdownSecs = nullptr;
    QRadioButton *m_rTimes = nullptr, *m_rSecs = nullptr, *m_rForever = nullptr;
    QComboBox *m_pathStyle = nullptr, *m_typing = nullptr;
    QCheckBox *m_countdownCheck = nullptr, *m_stopOnInput = nullptr, *m_restoreMouse = nullptr, *m_hidePlay = nullptr;
    QCheckBox *m_recMoves = nullptr, *m_recKeys = nullptr, *m_mergeTyping = nullptr, *m_hideRec = nullptr;
    QSpinBox *m_minMove = nullptr, *m_shortestPause = nullptr;
    QKeySequenceEdit *m_hkRecord = nullptr, *m_hkPlay = nullptr, *m_hkStop = nullptr;
    QLabel* m_hkError = nullptr;

    // menus
    QMenu* m_recentMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QAction *m_undoAct = nullptr, *m_redoAct = nullptr, *m_saveAct = nullptr, *m_logAct = nullptr;
    QList<QPair<QAction*, QPushButton*>> m_buttonActions;
    QList<QAction*> m_editActs;                  // need a selection and Idle
    QActionGroup* m_themeGroup = nullptr;
    QString m_themePreviewing;
    QTimer m_themePreviewTimer;
    QTimer m_saveTimer;
};
