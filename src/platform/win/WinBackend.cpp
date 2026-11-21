// Windows: low-level mouse/keyboard hooks for recording, SendInput + SetCursorPos for playback,
// RegisterHotKey for global hotkeys. Hooks and hotkeys live on one dedicated thread with its own message loop,
// so the GUI being busy never delays (or drops) input.
#include "platform/Backend.h"
#include "WinKeys.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QSet>

#include <windows.h>
#include <dwmapi.h>
#include <mmsystem.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace platform {

void Backend::earlyInit() {}

qint64 Backend::now()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

namespace {

constexpr ULONG_PTR kMagic = 0x4D52'4543;   // marks input we inject, so watching can tell it from the user's
constexpr UINT WM_RUN = WM_APP + 1;          // run a std::function on the hook thread

class WinBackend;
WinBackend* g_self = nullptr;

QString exeOf(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    static QHash<DWORD, QString> cache;   // hook thread only
    if (auto it = cache.constFind(pid); it != cache.constEnd()) return *it;
    QString name;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) name = QFileInfo(QString::fromWCharArray(buf, int(n))).fileName();
        CloseHandle(h);
    }
    if (cache.size() > 500) cache.clear();
    cache.insert(pid, name);
    return name;
}

QString titleOf(HWND hwnd)
{
    wchar_t buf[512];
    const int n = GetWindowTextW(hwnd, buf, 512);
    return QString::fromWCharArray(buf, n);
}

bool isOwnProcess(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

std::atomic<int> g_blocked{0};   // SendInput calls Windows refused (e.g. a window running as administrator)

void sendInputs(UINT n, INPUT* in)
{
    if (SendInput(n, in, sizeof(INPUT)) != n) ++g_blocked;
}

void sendKey(WORD vk, bool down)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (platform::win::isExtendedKey(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
    in.ki.dwExtraInfo = kMagic;
    sendInputs(1, &in);
}

class WinInjector : public core::Injector {
public:
    std::atomic<LONG> lastX{INT_MIN}, lastY{INT_MIN};   // where we last put the cursor (not the user's doing)

    QPoint cursor() override
    {
        POINT p;
        GetCursorPos(&p);
        return {p.x, p.y};
    }
    void moveTo(QPoint p) override
    {
        lastX = p.x();
        lastY = p.y();
        SetCursorPos(p.x(), p.y());   // exact pixel, on every monitor and scale (the app is per-monitor DPI aware)
    }
    void button(core::MouseButton b, bool down) override
    {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwExtraInfo = kMagic;
        switch (b) {
        case core::MouseButton::Left: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
        case core::MouseButton::Right: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
        case core::MouseButton::Middle: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
        case core::MouseButton::X1:
        case core::MouseButton::X2:
            in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            in.mi.mouseData = b == core::MouseButton::X1 ? XBUTTON1 : XBUTTON2;
            break;
        }
        sendInputs(1, &in);
    }
    void wheel(int notches, bool horizontal) override
    {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
        in.mi.mouseData = DWORD(notches * WHEEL_DELTA);
        in.mi.dwExtraInfo = kMagic;
        sendInputs(1, &in);
    }
    bool key(const QString& name, bool down) override
    {
        const unsigned vk = platform::win::keyCode(name);
        if (!vk) return false;
        sendKey(WORD(vk), down);
        return true;
    }
    void typeText(const QString& ch) override
    {
        // Prefer real key presses (with Shift / AltGr as the layout needs): every program understands them,
        // including games that ignore "Unicode" input. Fall back to Unicode for characters the layout can't type.
        if (ch.size() == 1) {
            const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
            const SHORT v = VkKeyScanExW(ch.at(0).unicode(), layout);
            const bool capsOn = GetKeyState(VK_CAPITAL) & 1;
            if (v != -1 && !(capsOn && ch.at(0).isLetter())) {
                const WORD vk = LOBYTE(v);
                const BYTE state = HIBYTE(v);   // 1 Shift, 2 Ctrl, 4 Alt (Ctrl+Alt = AltGr)
                if (state <= 7 && !(state & 8)) {
                    if (state & 1) sendKey(VK_SHIFT, true);
                    if (state & 2) sendKey(VK_CONTROL, true);
                    if (state & 4) sendKey(VK_MENU, true);
                    sendKey(vk, true);
                    sendKey(vk, false);
                    if (state & 4) sendKey(VK_MENU, false);
                    if (state & 2) sendKey(VK_CONTROL, false);
                    if (state & 1) sendKey(VK_SHIFT, false);
                    return;
                }
            }
        }
        for (QChar c : ch) {
            INPUT in[2]{};
            for (int i = 0; i < 2; ++i) {
                in[i].type = INPUT_KEYBOARD;
                in[i].ki.wScan = c.unicode();
                in[i].ki.dwFlags = KEYEVENTF_UNICODE | (i ? KEYEVENTF_KEYUP : 0);
                in[i].ki.dwExtraInfo = kMagic;
            }
            sendInputs(2, in);
        }
    }
    bool focusWindow(const QString& exe, const QString& title, core::TitleMatch match) override
    {
        struct Ctx { QString exe, title; core::TitleMatch match; HWND found = nullptr; } ctx{exe, title, match};
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) || isOwnProcess(h)) return TRUE;
            const QString t = titleOf(h);
            if (t.isEmpty()) return TRUE;
            bool ok = true;
            switch (c->match) {
            case core::TitleMatch::Exact: ok = t == c->title; break;
            case core::TitleMatch::StartsWith: ok = t.startsWith(c->title, Qt::CaseInsensitive); break;
            case core::TitleMatch::Contains: ok = t.contains(c->title, Qt::CaseInsensitive); break;
            case core::TitleMatch::Any: break;
            }
            if (ok && !c->exe.isEmpty()) ok = exeOfAnyThread(h).compare(c->exe, Qt::CaseInsensitive) == 0;
            if (ok) { c->found = h; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        if (!ctx.found) return false;
        if (GetForegroundWindow() == ctx.found) return true;
        if (IsIconic(ctx.found)) ShowWindow(ctx.found, SW_RESTORE);
        // Windows only lets the program that last received input change the foreground window; a harmless
        // Alt tap counts as input from us.
        sendKey(VK_MENU, true);
        sendKey(VK_MENU, false);
        SetForegroundWindow(ctx.found);
        BringWindowToTop(ctx.found);
        return GetForegroundWindow() == ctx.found;
    }
    QPoint activeWindowOrigin() override
    {
        HWND h = GetForegroundWindow();
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(h, &r);
        return {r.left, r.top};
    }
    QString pixelColor(QPoint p) override
    {
        HDC dc = GetDC(nullptr);
        const COLORREF c = GetPixel(dc, p.x(), p.y());
        ReleaseDC(nullptr, dc);
        if (c == CLR_INVALID) return {};
        return QStringLiteral("#%1%2%3").arg(GetRValue(c), 2, 16, QLatin1Char('0')).arg(GetGValue(c), 2, 16, QLatin1Char('0'))
            .arg(GetBValue(c), 2, 16, QLatin1Char('0')).toUpper();
    }
    bool isKeyDown(const QString& name) override
    {
        const unsigned vk = platform::win::keyCode(name);
        return vk && (GetAsyncKeyState(int(vk)) & 0x8000);
    }
    void setPreciseTiming(bool on) override
    {
        if (on) timeBeginPeriod(1); else timeEndPeriod(1);
    }
    void keyRepeat(int* delayMs, int* intervalMs) override
    {
        // Control Panel > Keyboard: delay 0-3 = 250-1000 ms; speed 0-31 = about 2.5-30 repeats per second.
        int delay = 1, speed = 31;
        SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0);
        SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0);
        *delayMs = (delay + 1) * 250;
        *intervalMs = int(1000.0 / (2.5 + speed * (30.0 - 2.5) / 31.0));
    }
    QString takeProblem() override
    {
        if (g_blocked.exchange(0) == 0) return {};
        return QStringLiteral("Windows blocked some of the clicks or keys. This happens when the window in front runs as "
                              "administrator: run " APP_NAME " as administrator too to control it.");
    }

    static QString exeOfAnyThread(HWND h)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        QString name;
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            wchar_t buf[MAX_PATH];
            DWORD n = MAX_PATH;
            if (QueryFullProcessImageNameW(p, 0, buf, &n)) name = QFileInfo(QString::fromWCharArray(buf, int(n))).fileName();
            CloseHandle(p);
        }
        return name;
    }
};

class WinBackend : public Backend {
public:
    explicit WinBackend(QObject* parent) : Backend(parent)
    {
        qRegisterMetaType<core::RawEvent>();
        g_self = this;
        std::atomic<bool> ready{false};
        m_thread = std::thread([this, &ready] {
            MSG msg;
            PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // create this thread's message queue
            m_threadId = GetCurrentThreadId();
            ready = true;
            loop();
        });
        while (!ready) std::this_thread::yield();
    }
    ~WinBackend() override
    {
        runOnHookThread([this] { removeHooks(); for (auto it = m_hotkeys.cbegin(); it != m_hotkeys.cend(); ++it) UnregisterHotKey(nullptr, it.key()); });
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
        if (m_thread.joinable()) m_thread.join();
        g_self = nullptr;
    }

    QString name() const override { return QStringLiteral("Windows"); }
    core::Injector* injector() override { return &m_inj; }
    int doubleClickMs() const override { return int(GetDoubleClickTime()); }

    void setCapture(CaptureMode m) override
    {
        m_mode = m;
        runOnHookThread([this, m] {
            if (m == CaptureMode::Off) removeHooks(); else installHooks();
        });
    }

    bool setHotkey(int id, const QKeySequence& seq, QString* error) override
    {
        unsigned vk = 0, mods = 0;
        const bool clear = seq.isEmpty();
        if (!clear && !win::hotkeyFromSequence(seq, &vk, &mods)) {
            if (error) *error = QStringLiteral("“%1” can't be used as a hotkey.").arg(seq.toString(QKeySequence::NativeText));
            return false;
        }
        DWORD err = 0;
        bool ok = true;
        runOnHookThread([&] {
            if (m_hotkeys.contains(id)) { UnregisterHotKey(nullptr, id); m_hotkeys.remove(id); }
            if (clear) return;
            ok = RegisterHotKey(nullptr, id, mods | MOD_NOREPEAT, vk);
            if (ok) m_hotkeys.insert(id, {vk, mods}); else err = GetLastError();
        }, true);
        QMutexLocker lock(&m_hkMutex);
        m_hkCopy = m_hotkeys;
        if (!ok && error)
            *error = err == ERROR_HOTKEY_ALREADY_REGISTERED
                         ? QStringLiteral("%1 is already used by another program.").arg(seq.toString(QKeySequence::NativeText))
                         : QStringLiteral("Couldn't register %1 (error %2).").arg(seq.toString(QKeySequence::NativeText)).arg(err);
        return ok;
    }

    // ---- hook thread ----
    static LRESULT CALLBACK mouseProc(int code, WPARAM w, LPARAM l)
    {
        if (code == HC_ACTION && g_self) g_self->onMouse(w, *reinterpret_cast<MSLLHOOKSTRUCT*>(l));
        return CallNextHookEx(nullptr, code, w, l);
    }
    static LRESULT CALLBACK keyProc(int code, WPARAM w, LPARAM l)
    {
        if (code == HC_ACTION && g_self) g_self->onKey(w, *reinterpret_cast<KBDLLHOOKSTRUCT*>(l));
        return CallNextHookEx(nullptr, code, w, l);
    }

private:
    struct Hotkey { unsigned vk, mods; };

    void runOnHookThread(std::function<void()> fn, bool wait = false)
    {
        if (GetCurrentThreadId() == m_threadId) { fn(); return; }
        if (!wait) {
            PostThreadMessageW(m_threadId, WM_RUN, 0, reinterpret_cast<LPARAM>(new std::function<void()>(std::move(fn))));
            return;
        }
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        auto* f = new std::function<void()>([&] { fn(); SetEvent(done); });
        PostThreadMessageW(m_threadId, WM_RUN, 0, reinterpret_cast<LPARAM>(f));
        WaitForSingleObject(done, 5000);
        CloseHandle(done);
    }

    void loop()
    {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_RUN) {
                auto* f = reinterpret_cast<std::function<void()>*>(msg.lParam);
                (*f)();
                delete f;
            } else if (msg.message == WM_HOTKEY) {
                const int id = int(msg.wParam);
                QMetaObject::invokeMethod(this, [this, id] { emit hotkeyPressed(id); }, Qt::QueuedConnection);
            }
        }
    }

    void installHooks()
    {
        if (!m_mouseHook) m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
        if (!m_keyHook) m_keyHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyProc, GetModuleHandleW(nullptr), 0);
    }
    void removeHooks()
    {
        if (m_mouseHook) { UnhookWindowsHookEx(m_mouseHook); m_mouseHook = nullptr; }
        if (m_keyHook) { UnhookWindowsHookEx(m_keyHook); m_keyHook = nullptr; }
    }

    void post(const core::RawEvent& e)
    {
        QMetaObject::invokeMethod(this, [this, e] { emit rawEvent(e); }, Qt::QueuedConnection);
    }
    void userDidSomething()
    {
        if (m_userSignalled.exchange(true)) return;  // once per playback
        QMetaObject::invokeMethod(this, [this] { m_userSignalled = false; emit userInput(); }, Qt::QueuedConnection);
    }

    bool isHotkey(unsigned vk)
    {
        unsigned mods = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
        if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
        if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
        QMutexLocker lock(&m_hkMutex);
        for (const Hotkey& h : std::as_const(m_hkCopy))
            if (h.vk == vk && h.mods == mods) return true;
        return false;
    }

    void onMouse(WPARAM msg, const MSLLHOOKSTRUCT& m)
    {
        const bool ours = m.dwExtraInfo == kMagic;
        const QPoint pos(m.pt.x, m.pt.y);
        if (m_mode == CaptureMode::Watch) {
            if (ours) return;
            if (msg == WM_MOUSEMOVE && m.pt.x == m_inj.lastX && m.pt.y == m_inj.lastY) return;  // our SetCursorPos
            userDidSomething();
            return;
        }
        if (m_mode != CaptureMode::Record || ours) return;
        HWND under = WindowFromPoint(m.pt);
        if (under && isOwnProcess(under)) return;   // clicks on our own window are never recorded
        core::RawEvent e;
        e.t = now();
        e.pos = pos;
        switch (msg) {
        case WM_MOUSEMOVE: e.kind = core::RawEvent::Move; break;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: e.button = core::MouseButton::Left; break;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: e.button = core::MouseButton::Right; break;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: e.button = core::MouseButton::Middle; break;
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
            e.button = HIWORD(m.mouseData) == XBUTTON1 ? core::MouseButton::X1 : core::MouseButton::X2; break;
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            e.kind = core::RawEvent::Wheel;
            e.wheelDelta = short(HIWORD(m.mouseData));
            e.horizontal = msg == WM_MOUSEHWHEEL;
            break;
        default: return;
        }
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN) {
            e.kind = core::RawEvent::Down;
            HWND top = GetAncestor(under, GA_ROOT);
            e.windowId = quint64(reinterpret_cast<quintptr>(top));
            e.exe = exeOf(top);
            e.title = titleOf(top);
        } else if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP || msg == WM_XBUTTONUP) {
            e.kind = core::RawEvent::Up;
        }
        post(e);
    }

    void onKey(WPARAM msg, const KBDLLHOOKSTRUCT& k)
    {
        const bool ours = k.dwExtraInfo == kMagic;
        const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if (m_mode == CaptureMode::Watch) {
            if (!ours && down && !isHotkey(k.vkCode)) userDidSomething();
            return;
        }
        if (m_mode != CaptureMode::Record || ours) return;
        HWND fg = GetForegroundWindow();
        if (fg && isOwnProcess(fg)) return;          // typing into our own window
        if (isHotkey(k.vkCode)) { m_swallowUp.insert(k.vkCode); return; }
        if (!down && m_swallowUp.remove(k.vkCode)) return;
        core::RawEvent e;
        e.t = now();
        e.kind = down ? core::RawEvent::KeyDown : core::RawEvent::KeyUp;
        e.key = win::keyName(k.vkCode);
        e.isModifier = win::isModifierKey(k.vkCode);
        POINT p;
        GetCursorPos(&p);
        e.pos = {p.x, p.y};
        if (down) {
            e.windowId = quint64(reinterpret_cast<quintptr>(fg));
            e.exe = exeOf(fg);
            e.title = titleOf(fg);
            if (!e.isModifier) {
                // The character this key types with the foreground window's keyboard layout and the current
                // Shift/Ctrl/Alt/Caps Lock state. Flag 4: don't disturb dead-key state (Windows 10 1607+).
                BYTE ks[256] = {};
                auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) ? BYTE(0x80) : BYTE(0); };
                ks[VK_SHIFT] = held(VK_SHIFT);
                ks[VK_CONTROL] = held(VK_CONTROL);
                ks[VK_MENU] = held(VK_MENU);
                ks[VK_CAPITAL] = (GetKeyState(VK_CAPITAL) & 1) ? 1 : 0;
                const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(fg, nullptr));
                wchar_t buf[8];
                const int n = ToUnicodeEx(k.vkCode, k.scanCode, ks, buf, 8, 4, layout);
                if (n > 0) {
                    const QString s = QString::fromWCharArray(buf, n);
                    if (!s.isEmpty() && s.at(0).isPrint()) e.ch = s;
                }
            }
        }
        post(e);
    }

    std::thread m_thread;
    DWORD m_threadId = 0;
    std::atomic<CaptureMode> m_mode{CaptureMode::Off};
    HHOOK m_mouseHook = nullptr, m_keyHook = nullptr;
    QHash<int, Hotkey> m_hotkeys;          // hook thread
    QHash<int, Hotkey> m_hkCopy;           // guarded by m_hkMutex, read by the hooks
    QMutex m_hkMutex;
    QSet<unsigned> m_swallowUp;            // hook thread
    std::atomic<bool> m_userSignalled{false};
    WinInjector m_inj;
};

}  // namespace

Backend* createWindowsBackend(QObject* parent) { return new WinBackend(parent); }

}  // namespace platform
