// The operating-system layer: captures input while recording, injects it while playing, and handles global
// hotkeys. Backend::create() picks the right implementation for this computer at run time.
#pragma once

#include "core/Input.h"

#include <QKeySequence>
#include <QObject>

namespace platform {

enum class CaptureMode {
    Off,
    Record,  // report every input event (except on our own windows and our hotkeys)
    Watch,   // playing: report only when the user moves the mouse or presses a key themselves
};

class Backend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    static Backend* create(QObject* parent);
    // Tests only: make create() return a simulated backend instead of the real one.
    static void setFactoryForTests(Backend* (*factory)(QObject* parent));

    virtual QString name() const = 0;                       // e.g. "Windows", "X11", "Wayland (uinput)"
    virtual QString unavailableReason() const { return {}; } // non-empty: playback (and recording) can't work here
    virtual QString recordUnavailableReason() const { return unavailableReason(); }  // non-empty: recording can't work
    virtual QString notes() const { return {}; }             // limitations worth telling the user at start-up
    virtual void setCapture(CaptureMode m) = 0;
    virtual core::Injector* injector() = 0;
    // Register a global hotkey (empty sequence clears it). False with a reason if another program has it.
    virtual bool setHotkey(int id, const QKeySequence& seq, QString* error) = 0;
    virtual int doubleClickMs() const { return 500; }
    static qint64 now();                                     // ms on the same clock as RawEvent::t
    static void earlyInit();                                 // call first thing in main(), before QApplication

signals:
    void rawEvent(const core::RawEvent& e);
    void userInput();          // Watch mode: the user moved the mouse or pressed a key
    void hotkeyPressed(int id);
};

}  // namespace platform
