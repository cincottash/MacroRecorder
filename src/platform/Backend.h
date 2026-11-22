// The operating-system layer: captures input while recording, injects it while playing, and handles global
// hotkeys. Backend::create() picks the right implementation for this computer at run time.
#pragma once

#include "core/Input.h"
#include "System.h"

#include <QKeySequence>
#include <QObject>

namespace platform {

enum class CaptureMode {
    Off,
    Record,  // report every input event (except on our own windows and our hotkeys)
    Watch,   // playing: report when the user moves the mouse or presses a key themselves (and block keys if asked)
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
    // While watching (playback), swallow the user's key presses so they can't disturb the macro. Hotkeys still work.
    virtual bool canBlockKeyboard() const { return false; }
    virtual void setBlockKeyboard(bool on) { Q_UNUSED(on) }

    // Operating-system actions (see System.h); virtual so tests can replace them.
    virtual bool systemAction(SystemAction a, QString* error) { return sys::run(a, error); }
    virtual bool startsAtLogin() const { return sys::startsAtLogin(); }
    virtual bool setStartAtLogin(bool on, QString* error) { return sys::setStartAtLogin(on, error); }
    virtual bool isElevated() const { return sys::isElevated(); }
    virtual bool restartElevated(const QStringList& args, QString* error) { return sys::restartElevated(args, error); }
    virtual void beep() { sys::beep(); }
    static qint64 now();                                     // ms on the same clock as RawEvent::t
    static void earlyInit();                                 // call first thing in main(), before QApplication

signals:
    void rawEvent(const core::RawEvent& e);
    void userInput(bool key);  // Watch mode: the user pressed a key (true) or used the mouse (false)
    void hotkeyPressed(int id);
};

}  // namespace platform
