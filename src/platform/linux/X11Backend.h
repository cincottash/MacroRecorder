// The X11 backend (also used on Wayland for programs that run through XWayland).
#pragma once

#include "platform/Backend.h"

#include <memory>

namespace platform {

class X11Backend : public Backend {
public:
    // playbackInjector: use this for playback instead of XTest (e.g. /dev/uinput on Wayland); not owned.
    explicit X11Backend(QObject* parent, core::Injector* playbackInjector = nullptr);
    ~X11Backend() override;
    void setNotes(const QString& notes);
    void setDisplayName(const QString& name);   // e.g. "Wayland (XWayland)"

    QString name() const override;
    QString unavailableReason() const override;
    QString notes() const override;
    void setCapture(CaptureMode m) override;
    core::Injector* injector() override;
    bool setHotkey(int id, const QKeySequence& seq, QString* error) override;
    int doubleClickMs() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace platform
