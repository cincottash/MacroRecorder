// Everything the app remembers between runs, saved as JSON in the app data folder.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

struct Settings {
    // View options (View > Reset view options)
    QString theme = QStringLiteral("dark");
    QStringList foldedCards;
    bool showLog = true;
    bool detailedLog = false;
    bool alwaysOnTop = false;
    int windowOpacity = 100;          // percent
    bool seeThroughBusy = false;      // View > Window Opacity > Fade While Recording or Playing
    bool seeThroughInactive = false;  // ... > Fade While Another Program Is in Front
    int seeThroughOpacity = 50;       // the Faded level (percent), while one of the two above applies
    QByteArray geometry;
    QByteArray tableState;

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

    // Files
    QStringList recent;
    QString lastFolder;

    bool load(const QString& path);
    bool save(const QString& path) const;
    void resetView();
    void resetOptions();   // playback, recording and hotkeys
};
