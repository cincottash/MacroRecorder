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
    bool stopOnUserInput = true;
    bool restoreMouse = true;
    bool hideWhilePlaying = false;

    // Recording
    bool recordMoves = true;
    int minMovePx = 5;
    bool recordKeyboard = true;
    bool mergeTyping = true;
    int shortestPauseMs = 500;
    bool hideWhileRecording = false;

    // Hotkeys (QKeySequence portable text; empty = none)
    QString hkRecord = QStringLiteral("F9");
    QString hkPlay = QStringLiteral("F10");
    QString hkStop = QStringLiteral("Esc");     // only active while recording or playing

    // Files
    QStringList recent;
    QString lastFolder;

    bool load(const QString& path);
    bool save(const QString& path) const;
    void resetView();
    void resetOptions();   // playback, recording and hotkeys
};
