// Everything the app remembers between runs, saved as JSON in the app data folder.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// Macro > Macro Hotkeys: a hotkey that plays a saved macro without opening it.
struct MacroHotkey {
    QString file;
    QString key;   // QKeySequence portable text
    bool operator==(const MacroHotkey&) const = default;
};

// Macro > Schedules: a saved macro played at set times while the app runs.
struct Schedule {
    QString file;
    bool enabled = true;
    QString repeat = QStringLiteral("daily");   // once / daily / days / minutes
    QDateTime at;          // once: when; daily and days: the time of day (its date is ignored); minutes: the first run
    int days = 0x1F;       // days: Monday = 1, Tuesday = 2, Wednesday = 4, ... Sunday = 64
    int minutes = 60;      // minutes: how often
    QDateTime lastRun;
    bool operator==(const Schedule&) const = default;
    // The first time after `after` it's due; invalid if never again (a past "once", no days ticked)
    QDateTime nextRun(const QDateTime& after) const;
    QString describe() const;   // "Every day at 08:00", "Mon, Wed, Fri at 17:30", "Every 15 min"...
};

struct Settings {
    // View options (View > Reset view options)
    QString theme = QStringLiteral("dark");
    QStringList foldedCards;
    bool showLog = true;
    bool detailedLog = false;
    bool alwaysOnTop = false;
    int windowOpacity = 100;            // percent (View > Window Opacity)
    QByteArray geometry;
    QByteArray tableState;
    bool fitValueColumn = true;         // the step list's Value column takes a wide window's extra room (off once a column is dragged)

    // Playback
    int speedPct = 100;
    QString repeatMode = QStringLiteral("times");   // times / seconds / forever
    int repeatTimes = 1;
    int repeatSeconds = 60;
    QString pathStyle = QStringLiteral("recorded"); // recorded / linear / spline / human
    QString typing = QStringLiteral("recorded");    // recorded / instant
    bool countdown = true;
    int countdownSeconds = 3;
    bool stopOnKey = true;
    bool stopOnMouse = true;
    bool blockKeyboard = false;                     // Windows only
    bool restoreMouse = true;
    bool hideWhilePlaying = false;
    bool showNotifications = true;
    QString afterPlayback = QStringLiteral("nothing"); // nothing / quit / lock / sleep / logoff / restart / shutdown

    // Recording
    bool recordMoves = true;
    int minMovePx = 5;
    bool recordKeyboard = true;
    bool mergeTyping = true;
    int shortestPauseMs = 500;
    bool hideWhileRecording = false;
    bool relativePositions = false;   // record positions relative to the window
    bool smartClicks = false;         // keep a picture of what each click lands on; playing, the click finds it first
    int pathDetail = 100;             // % of mouse path points kept (100 = all)

    // Hotkeys (QKeySequence portable text; empty = none)
    QString hkRecord = QStringLiteral("F9");
    QString hkPlay = QStringLiteral("F10");
    QString hkStop = QStringLiteral("Esc");     // only active while recording or playing
    QString hkNewRecording;
    QString hkPlayFrom;                         // play from the selected step
    QString hkPlaySelection;
    QString hkCapture = QStringLiteral("F8");   // only active while a step editor with a position is open

    // App
    bool confirmDelete = false;
    bool startMinimized = false;
    bool showPosition = true;   // mark the selected step's position on the screen
    bool showPreview = true;    // animate the hovered step's path in the list
    bool scanCodes = false;     // Windows: send keys as hardware scan codes (for games)

    // Imported themes, one JSON text each (kept by both resets: they're the user's own files)
    QStringList customThemes;

    // Macros played without opening them (kept by both resets, like the themes)
    QList<MacroHotkey> macroHotkeys;
    QList<Schedule> schedules;

    // Ask AI steps: an OpenAI-compatible server. The default is LM Studio's local server (free, on this PC).
    QString aiUrl = QStringLiteral("http://localhost:1234/v1");
    QString aiModel;   // empty: the server's loaded model

    // Files
    QStringList recent;
    QString lastFolder;

    bool load(const QString& path);
    bool save(const QString& path) const;
    void resetView();
    void resetOptions();   // playback, recording and hotkeys
};
