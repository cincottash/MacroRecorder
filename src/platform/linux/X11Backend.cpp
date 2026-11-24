// X11: XRecord to record input, XTest to play it back, XGrabKey for global hotkeys, EWMH to find and focus windows.
// Each job has its own X connection (Xlib connections must not be shared between threads without locking).
#include "X11Backend.h"
#include "LinuxKeys.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QStyleHints>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include <poll.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/record.h>

namespace platform {

namespace {

// ---------------------------------------------------------------- helpers (any connection)

Atom atom(Display* d, const char* name) { return XInternAtom(d, name, False); }

bool getProp(Display* d, Window w, Atom prop, Atom type, std::vector<unsigned char>* out, int* format = nullptr, unsigned long* items = nullptr)
{
    Atom actual = 0;
    int fmt = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, prop, 0, 1 << 16, False, type, &actual, &fmt, &n, &after, &data) != Success || !data) return false;
    const size_t bytes = fmt == 32 ? n * sizeof(long) : n * size_t(fmt / 8);
    out->assign(data, data + bytes);
    XFree(data);
    if (format) *format = fmt;
    if (items) *items = n;
    return actual != 0;
}

long getCardinal(Display* d, Window w, const char* prop, long fallback = 0)
{
    std::vector<unsigned char> v;
    int fmt = 0;
    unsigned long n = 0;
    if (!getProp(d, w, atom(d, prop), AnyPropertyType, &v, &fmt, &n) || fmt != 32 || n < 1) return fallback;
    return reinterpret_cast<long*>(v.data())[0];
}

QString windowTitle(Display* d, Window w)
{
    std::vector<unsigned char> v;
    if (getProp(d, w, atom(d, "_NET_WM_NAME"), atom(d, "UTF8_STRING"), &v) && !v.empty())
        return QString::fromUtf8(reinterpret_cast<const char*>(v.data()), int(v.size()));
    char* name = nullptr;
    if (XFetchName(d, w, &name) && name) {
        const QString s = QString::fromLocal8Bit(name);
        XFree(name);
        return s;
    }
    return {};
}

QString exeOfPid(long pid)
{
    if (pid <= 0) return {};
    return QFileInfo(QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget()).fileName();
}

bool hasWmState(Display* d, Window w)
{
    std::vector<unsigned char> v;
    return getProp(d, w, atom(d, "WM_STATE"), AnyPropertyType, &v);
}

// The application window inside a window-manager frame (the one with WM_STATE).
Window clientOf(Display* d, Window w, int depth = 0)
{
    if (!w || depth > 4) return 0;
    if (hasWmState(d, w)) return w;
    Window root = 0, parent = 0, *kids = nullptr;
    unsigned n = 0;
    if (!XQueryTree(d, w, &root, &parent, &kids, &n)) return 0;
    Window found = 0;
    for (unsigned i = n; i-- > 0 && !found;) found = clientOf(d, kids[i], depth + 1);
    if (kids) XFree(kids);
    return found;
}

Window activeWindow(Display* d)
{
    std::vector<unsigned char> v;
    int fmt = 0;
    unsigned long n = 0;
    if (getProp(d, DefaultRootWindow(d), atom(d, "_NET_ACTIVE_WINDOW"), XA_WINDOW, &v, &fmt, &n) && fmt == 32 && n >= 1)
        if (const Window w = Window(reinterpret_cast<long*>(v.data())[0])) return w;
    Window focus = 0;
    int revert = 0;
    XGetInputFocus(d, &focus, &revert);
    return focus == PointerRoot ? 0 : focus;
}

// A window's outer top-left corner, frame included (as on Windows).
QPoint windowOrigin(Display* d, Window w)
{
    if (!w) return {INT_MIN, INT_MIN};
    int x = 0, y = 0;
    Window child;
    XTranslateCoordinates(d, w, DefaultRootWindow(d), 0, 0, &x, &y, &child);
    std::vector<unsigned char> v;   // _NET_FRAME_EXTENTS: left, right, top, bottom
    int fmt = 0;
    unsigned long n = 0;
    if (getProp(d, w, atom(d, "_NET_FRAME_EXTENTS"), XA_CARDINAL, &v, &fmt, &n) && fmt == 32 && n == 4) {
        const long* e = reinterpret_cast<long*>(v.data());
        x -= int(e[0]);
        y -= int(e[2]);
    }
    return {x, y};
}

bool isOwn(Display* d, Window w) { return w && getCardinal(d, w, "_NET_WM_PID") == long(getpid()); }

std::atomic<int> g_xerror{0};
int errorHandler(Display*, XErrorEvent* e)
{
    g_xerror = e->error_code;
    return 0;   // never abort the app on a failed X request (e.g. a hotkey another program grabbed)
}

// What we injected recently, so watching for the user's own input can ignore it (XRecord sees both alike).
struct Expected { int type; int detail; int x, y; qint64 t; };

class X11Injector : public core::Injector {
public:
    explicit X11Injector(std::function<void(const Expected&)> expect) : m_expect(std::move(expect)) { m_d = XOpenDisplay(nullptr); }
    ~X11Injector() override { if (m_d) XCloseDisplay(m_d); }
    bool ok() const { return m_d != nullptr; }

    QPoint cursor() override
    {
        QMutexLocker l(&m_mx);
        Window r, c;
        int rx = 0, ry = 0, wx, wy;
        unsigned m;
        XQueryPointer(m_d, DefaultRootWindow(m_d), &r, &c, &rx, &ry, &wx, &wy, &m);
        return {rx, ry};
    }
    void moveTo(QPoint p) override
    {
        QMutexLocker l(&m_mx);
        m_expect({MotionNotify, 0, p.x(), p.y(), Backend::now()});
        XTestFakeMotionEvent(m_d, -1, p.x(), p.y(), CurrentTime);
        XFlush(m_d);
    }
    void button(core::MouseButton b, bool down) override
    {
        static const unsigned kMap[] = {1, 3, 2, 8, 9};   // Left, Right, Middle, X1 (back), X2 (forward)
        fakeButton(kMap[int(b)], down);
    }
    void wheel(int notches, bool horizontal) override
    {
        const unsigned b = horizontal ? (notches > 0 ? 7 : 6) : (notches > 0 ? 4 : 5);
        fakeButton(b, true);
        fakeButton(b, false);
    }
    bool key(const QString& name, bool down) override
    {
        const unsigned long ks = lnx::keysymFromName(name);
        if (!ks) return false;
        QMutexLocker l(&m_mx);
        const KeyCode kc = XKeysymToKeycode(m_d, ks);
        if (!kc) return false;
        fakeKey(kc, down);
        return true;
    }
    void typeText(const QString& ch) override
    {
        const QList<uint> cps = ch.toUcs4();
        if (cps.isEmpty()) return;
        const unsigned long ks = lnx::keysymFromChar(cps.first());
        QMutexLocker l(&m_mx);
        int minK = 8, maxK = 255;
        XDisplayKeycodes(m_d, &minK, &maxK);
        // Find a key and shift level that types it with the current layout: 0 plain, 1 Shift, 2 AltGr, 3 AltGr+Shift.
        for (int kc = minK; kc <= maxK; ++kc)
            for (int level = 0; level < 4; ++level)
                if (XkbKeycodeToKeysym(m_d, KeyCode(kc), 0, level) == ks) {
                    const KeyCode shift = XKeysymToKeycode(m_d, XK_Shift_L), altgr = XKeysymToKeycode(m_d, XK_ISO_Level3_Shift);
                    if ((level & 2) && !altgr) continue;
                    if (level & 1) fakeKey(shift, true);
                    if (level & 2) fakeKey(altgr, true);
                    fakeKey(KeyCode(kc), true);
                    fakeKey(KeyCode(kc), false);
                    if (level & 2) fakeKey(altgr, false);
                    if (level & 1) fakeKey(shift, false);
                    return;
                }
        // Not on the layout: put it on an unused key for a moment (the usual xdotool approach).
        int perCode = 0;
        KeySym* map = XGetKeyboardMapping(m_d, KeyCode(minK), maxK - minK + 1, &perCode);
        int spare = 0;
        for (int kc = maxK; kc >= minK && !spare; --kc) {
            bool empty = true;
            for (int i = 0; i < perCode; ++i) empty &= map[(kc - minK) * perCode + i] == NoSymbol;
            if (empty) spare = kc;
        }
        if (map) XFree(map);
        if (!spare) { m_problem = QStringLiteral("Some characters couldn't be typed: the keyboard layout has no free key to use."); return; }
        KeySym both[2] = {KeySym(ks), KeySym(ks)};
        XChangeKeyboardMapping(m_d, spare, 2, both, 1);
        XSync(m_d, False);
        usleep(20000);   // let programs see the new mapping
        fakeKey(KeyCode(spare), true);
        fakeKey(KeyCode(spare), false);
        XSync(m_d, False);
        usleep(20000);
        KeySym none[2] = {NoSymbol, NoSymbol};
        XChangeKeyboardMapping(m_d, spare, 2, none, 1);
        XSync(m_d, False);
    }
    // The first application window that matches (m_mx held).
    Window findClient(const QString& exe, const QString& title, core::TitleMatch match)
    {
        const Window root = DefaultRootWindow(m_d);
        std::vector<unsigned char> v;
        int fmt = 0;
        unsigned long n = 0;
        QList<Window> list;
        if (getProp(m_d, root, atom(m_d, "_NET_CLIENT_LIST"), XA_WINDOW, &v, &fmt, &n) && fmt == 32)
            for (unsigned long i = 0; i < n; ++i) list << Window(reinterpret_cast<long*>(v.data())[i]);
        else {  // no window manager list: top-level windows
            Window r, p, *kids = nullptr;
            unsigned k = 0;
            if (XQueryTree(m_d, root, &r, &p, &kids, &k)) {
                for (unsigned i = 0; i < k; ++i) if (Window c = clientOf(m_d, kids[i])) list << c;
                if (kids) XFree(kids);
            }
        }
        for (Window w : list) {
            if (isOwn(m_d, w)) continue;
            const QString t = windowTitle(m_d, w);
            bool ok = true;
            switch (match) {
            case core::TitleMatch::Exact: ok = t == title; break;
            case core::TitleMatch::StartsWith: ok = t.startsWith(title, Qt::CaseInsensitive); break;
            case core::TitleMatch::Contains: ok = t.contains(title, Qt::CaseInsensitive); break;
            case core::TitleMatch::Any: break;
            }
            if (ok && !exe.isEmpty()) ok = exeOfPid(getCardinal(m_d, w, "_NET_WM_PID")).compare(exe, Qt::CaseInsensitive) == 0;
            if (ok) return w;
        }
        return 0;
    }

    // _NET_FRAME_EXTENTS: left, right, top, bottom (zero without a window manager)
    void frameExtents(Window w, long ext[4])
    {
        ext[0] = ext[1] = ext[2] = ext[3] = 0;
        std::vector<unsigned char> v;
        int fmt = 0;
        unsigned long n = 0;
        if (getProp(m_d, w, atom(m_d, "_NET_FRAME_EXTENTS"), XA_CARDINAL, &v, &fmt, &n) && fmt == 32 && n == 4)
            for (int i = 0; i < 4; ++i) ext[i] = reinterpret_cast<long*>(v.data())[i];
    }

    void sendState(Window w, long action, const char* a1, const char* a2)
    {
        XEvent e{};
        e.xclient.type = ClientMessage;
        e.xclient.window = w;
        e.xclient.message_type = atom(m_d, "_NET_WM_STATE");
        e.xclient.format = 32;
        e.xclient.data.l[0] = action;   // 0 remove, 1 add
        e.xclient.data.l[1] = long(atom(m_d, a1));
        e.xclient.data.l[2] = a2 ? long(atom(m_d, a2)) : 0;
        e.xclient.data.l[3] = 2;        // from a pager
        XSendEvent(m_d, DefaultRootWindow(m_d), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    }

    bool moveWindow(const QString& exe, const QString& title, core::TitleMatch match, core::WindowState state, const QRect& r) override
    {
        QMutexLocker l(&m_mx);
        const bool active = exe.isEmpty() && (match == core::TitleMatch::Any || title.isEmpty());
        const Window w = active ? activeWindow(m_d) : findClient(exe, title, match);
        if (!w || isOwn(m_d, w)) return false;
        switch (state) {
        case core::WindowState::Maximized:
            XMapWindow(m_d, w);
            sendState(w, 1, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
            break;
        case core::WindowState::Minimized: XIconifyWindow(m_d, w, DefaultScreen(m_d)); break;
        case core::WindowState::Restore:
            sendState(w, 0, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
            XMapWindow(m_d, w);
            break;
        case core::WindowState::Normal: {
            sendState(w, 0, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
            XMapWindow(m_d, w);
            long ext[4];
            frameExtents(w, ext);   // the rectangle is the window as seen, frame included
            const int cw = qMax(1, r.width() - int(ext[0] + ext[1])), ch = qMax(1, r.height() - int(ext[2] + ext[3]));
            XMoveResizeWindow(m_d, w, r.x(), r.y(), unsigned(cw), unsigned(ch));
            break;
        }
        }
        XSync(m_d, False);
        return true;
    }
    bool windowAt(QPoint, QString* exe, QString* title, QRect* frame) override
    {
        QMutexLocker l(&m_mx);
        const Window root = DefaultRootWindow(m_d);
        Window r = 0, child = 0;
        int rx = 0, ry = 0, wx = 0, wy = 0;
        unsigned m = 0;
        XQueryPointer(m_d, root, &r, &child, &rx, &ry, &wx, &wy, &m);
        const Window w = clientOf(m_d, child);
        if (!w || isOwn(m_d, w)) return false;
        XWindowAttributes at{};
        if (!XGetWindowAttributes(m_d, w, &at)) return false;
        long ext[4];
        frameExtents(w, ext);
        const QPoint o = windowOrigin(m_d, w);
        if (exe) *exe = exeOfPid(getCardinal(m_d, w, "_NET_WM_PID"));
        if (title) *title = windowTitle(m_d, w);
        if (frame) *frame = QRect(o, QSize(at.width + int(ext[0] + ext[1]), at.height + int(ext[2] + ext[3])));
        return true;
    }
    QRect screenBounds() override
    {
        QMutexLocker l(&m_mx);
        return QRect(0, 0, DisplayWidth(m_d, DefaultScreen(m_d)), DisplayHeight(m_d, DefaultScreen(m_d)));
    }
    QImage grabScreen(const QRect& rect) override
    {
        QMutexLocker l(&m_mx);
        const QRect all(0, 0, DisplayWidth(m_d, DefaultScreen(m_d)), DisplayHeight(m_d, DefaultScreen(m_d)));
        const QRect part = rect.intersected(all);
        if (rect.isEmpty()) return {};
        QImage out(rect.size(), QImage::Format_RGB32);
        out.fill(Qt::black);
        if (part.isEmpty()) return out;
        XImage* img = XGetImage(m_d, DefaultRootWindow(m_d), part.x(), part.y(), unsigned(part.width()), unsigned(part.height()), AllPlanes, ZPixmap);
        if (!img) return {};
        // Works out each pixel from the image's color masks (any depth); 24-bit color is the usual case
        auto shiftOf = [](unsigned long mask) { int s = 0; while (mask && !(mask & 1)) { mask >>= 1; ++s; } return s; };
        auto bitsOf = [](unsigned long mask) { int b = 0; while (mask) { b += int(mask & 1); mask >>= 1; } return b; };
        const int rs = shiftOf(img->red_mask), gs = shiftOf(img->green_mask), bs = shiftOf(img->blue_mask);
        const int rb = bitsOf(img->red_mask), gb = bitsOf(img->green_mask), bb = bitsOf(img->blue_mask);
        auto scale = [](unsigned long v, int bits) { return bits >= 8 ? int(v >> (bits - 8)) : bits > 0 ? int(v * 255 / ((1ul << bits) - 1)) : 0; };
        for (int y = 0; y < part.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y + part.y() - rect.y())) + (part.x() - rect.x());
            for (int x = 0; x < part.width(); ++x) {
                const unsigned long p = XGetPixel(img, x, y);
                line[x] = qRgb(scale((p & img->red_mask) >> rs, rb), scale((p & img->green_mask) >> gs, gb), scale((p & img->blue_mask) >> bs, bb));
            }
        }
        XDestroyImage(img);
        return out;
    }

    bool focusWindow(const QString& exe, const QString& title, core::TitleMatch match) override
    {
        QMutexLocker l(&m_mx);
        const Window root = DefaultRootWindow(m_d);
        const Window found = findClient(exe, title, match);
        if (!found) return false;
        if (activeWindow(m_d) == found) return true;
        XEvent e{};
        e.xclient.type = ClientMessage;
        e.xclient.window = found;
        e.xclient.message_type = atom(m_d, "_NET_ACTIVE_WINDOW");
        e.xclient.format = 32;
        e.xclient.data.l[0] = 2;   // "from a pager": window managers honour it
        e.xclient.data.l[1] = CurrentTime;
        XSendEvent(m_d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
        XMapRaised(m_d, found);
        XSync(m_d, False);
        if (activeWindow(m_d) != found) {   // no window manager: set the focus ourselves
            XSetInputFocus(m_d, found, RevertToParent, CurrentTime);
            XSync(m_d, False);
        }
        return activeWindow(m_d) == found;
    }
    QPoint activeWindowOrigin() override
    {
        QMutexLocker l(&m_mx);
        const QPoint p = windowOrigin(m_d, activeWindow(m_d));
        return p.x() == INT_MIN ? QPoint() : p;
    }
    QString pixelColor(QPoint p) override
    {
        QMutexLocker l(&m_mx);
        XImage* img = XGetImage(m_d, DefaultRootWindow(m_d), p.x(), p.y(), 1, 1, AllPlanes, ZPixmap);
        if (!img) return {};
        XColor c{};
        c.pixel = XGetPixel(img, 0, 0);
        XDestroyImage(img);
        XQueryColor(m_d, DefaultColormap(m_d, DefaultScreen(m_d)), &c);
        return QStringLiteral("#%1%2%3").arg(c.red >> 8, 2, 16, QLatin1Char('0')).arg(c.green >> 8, 2, 16, QLatin1Char('0'))
            .arg(c.blue >> 8, 2, 16, QLatin1Char('0')).toUpper();
    }
    bool isKeyDown(const QString& name) override
    {
        const unsigned long ks = lnx::keysymFromName(name);
        QMutexLocker l(&m_mx);
        const KeyCode kc = ks ? XKeysymToKeycode(m_d, ks) : 0;
        if (!kc) return false;
        char keys[32];
        XQueryKeymap(m_d, keys);
        return keys[kc / 8] & (1 << (kc % 8));
    }
    void keyRepeat(int* delayMs, int* intervalMs) override
    {
        QMutexLocker l(&m_mx);
        unsigned delay = 500, interval = 33;
        if (XkbGetAutoRepeatRate(m_d, XkbUseCoreKbd, &delay, &interval)) { *delayMs = int(delay); *intervalMs = int(interval); }
    }
    QString takeProblem() override
    {
        QMutexLocker l(&m_mx);
        QString p = m_problem;
        m_problem.clear();
        return p;
    }

private:
    void fakeButton(unsigned b, bool down)
    {
        QMutexLocker l(&m_mx);
        m_expect({down ? ButtonPress : ButtonRelease, int(b), 0, 0, Backend::now()});
        XTestFakeButtonEvent(m_d, b, down, CurrentTime);
        XFlush(m_d);
    }
    void fakeKey(KeyCode kc, bool down)   // m_mx held
    {
        m_expect({down ? KeyPress : KeyRelease, kc, 0, 0, Backend::now()});
        XTestFakeKeyEvent(m_d, kc, down, CurrentTime);
        XFlush(m_d);
    }

    Display* m_d = nullptr;
    QRecursiveMutex m_mx;
    QString m_problem;
    std::function<void(const Expected&)> m_expect;
};

}  // namespace

// ---------------------------------------------------------------- the backend

struct X11Backend::Impl {
    X11Backend* q;
    Display* ctrl = nullptr;   // XRecord control connection
    Display* data = nullptr;   // XRecord data connection (blocks in its own thread)
    Display* query = nullptr;  // window lookups from the record thread
    Display* hk = nullptr;     // hotkeys, with their own event thread
    XRecordContext ctx = 0;
    std::thread recordThread, hotkeyThread;
    std::atomic<CaptureMode> mode{CaptureMode::Off};
    std::unique_ptr<X11Injector> xinj;
    core::Injector* inj = nullptr;
    std::mutex expMx;
    std::deque<Expected> expected;
    std::atomic<bool> keySignalled{false}, mouseSignalled{false};
    QString unavailable, notes, name = QStringLiteral("X11");
    QString limited;   // short tag for the status label, when only some programs can be reached

    // hotkeys: id -> (keycode, modifiers)
    struct Hk { int kc; unsigned mods; };
    QHash<int, Hk> hotkeys;
    QMutex hkMx;
    int wake[2] = {-1, -1};
    std::mutex cmdMx;
    std::deque<std::function<void()>> cmds;
    std::atomic<bool> quit{false};
    QSet<int> swallowUp;

    void expect(const Expected& e)
    {
        std::lock_guard<std::mutex> l(expMx);
        expected.push_back(e);
        while (expected.size() > 512) expected.pop_front();
    }
    bool wasOurs(int type, int detail, int x, int y)
    {
        std::lock_guard<std::mutex> l(expMx);
        const qint64 now = Backend::now();
        while (!expected.empty() && now - expected.front().t > 1000) expected.pop_front();
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            const bool same = it->type == type && (type == MotionNotify ? (it->x == x && it->y == y) : it->detail == detail);
            if (same) { expected.erase(it); return true; }
        }
        return false;
    }
    bool isHotkey(int kc, unsigned state)
    {
        QMutexLocker l(&hkMx);
        const unsigned m = state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
        for (const Hk& h : std::as_const(hotkeys))
            if (h.kc == kc && h.mods == m) return true;
        return false;
    }
    void runOnHotkeyThread(std::function<void()> fn)
    {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        {
            std::lock_guard<std::mutex> l(cmdMx);
            cmds.push_back([&] { fn(); std::lock_guard<std::mutex> g(m); done = true; cv.notify_one(); });
        }
        const char c = 1;
        (void)!write(wake[1], &c, 1);
        std::unique_lock<std::mutex> l(m);
        cv.wait_for(l, std::chrono::seconds(3), [&] { return done; });
    }
    void hotkeyLoop()
    {
        pollfd fds[2] = {{ConnectionNumber(hk), POLLIN, 0}, {wake[0], POLLIN, 0}};
        while (!quit) {
            while (XPending(hk)) {
                XEvent e;
                XNextEvent(hk, &e);
                if (e.type != KeyPress) continue;
                const unsigned m = e.xkey.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
                QMutexLocker l(&hkMx);
                for (auto it = hotkeys.cbegin(); it != hotkeys.cend(); ++it)
                    if (it->kc == int(e.xkey.keycode) && it->mods == m) {
                        const int id = it.key();
                        QMetaObject::invokeMethod(q, [this, id] { emit q->hotkeyPressed(id); }, Qt::QueuedConnection);
                    }
            }
            if (poll(fds, 2, 200) > 0 && (fds[1].revents & POLLIN)) {
                char buf[16];
                (void)!read(wake[0], buf, sizeof buf);
            }
            std::deque<std::function<void()>> todo;
            {
                std::lock_guard<std::mutex> l(cmdMx);
                todo.swap(cmds);
            }
            for (auto& f : todo) f();
        }
    }

    static void recordCallback(XPointer priv, XRecordInterceptData* d)
    {
        auto* self = reinterpret_cast<Impl*>(priv);
        if (d->category == XRecordFromServer && d->data && d->data_len >= 8) self->onEvent(reinterpret_cast<const xEvent*>(d->data));
        XRecordFreeData(d);
    }
    void userDidSomething(bool key)
    {
        std::atomic<bool>& flag = key ? keySignalled : mouseSignalled;
        if (flag.exchange(true)) return;
        QMetaObject::invokeMethod(q, [this, key, &flag] { flag = false; emit q->userInput(key); }, Qt::QueuedConnection);
    }
    void onEvent(const xEvent* ev)
    {
        const int type = ev->u.u.type & 0x7f;
        const int detail = ev->u.u.detail;
        const int x = ev->u.keyButtonPointer.rootX, y = ev->u.keyButtonPointer.rootY;
        const unsigned state = ev->u.keyButtonPointer.state;
        const bool ours = wasOurs(type, detail, x, y);
        if (mode == CaptureMode::Watch) {
            if (ours) return;
            if ((type == KeyPress) && isHotkey(detail, state)) return;
            if (type == KeyPress) userDidSomething(true);
            else if (type == ButtonPress || type == MotionNotify) userDidSomething(false);
            return;
        }
        if (mode != CaptureMode::Record || ours) return;
        core::RawEvent e;
        e.t = Backend::now();
        e.pos = {x, y};
        if (type == MotionNotify || type == ButtonPress || type == ButtonRelease) {
            Window r, child = 0;
            int rx, ry, wx, wy;
            unsigned m;
            XQueryPointer(query, DefaultRootWindow(query), &r, &child, &rx, &ry, &wx, &wy, &m);
            const Window client = clientOf(query, child);
            if (isOwn(query, client)) return;   // our own window is never recorded
            // presses and releases belong to the window under the pointer; moves and scrolling to the active one
            const bool button = (type == ButtonPress || type == ButtonRelease) && !(detail >= 4 && detail <= 7);
            e.windowOrigin = windowOrigin(query, button ? client : activeWindow(query));
            if (type == MotionNotify) e.kind = core::RawEvent::Move;
            else if (detail >= 4 && detail <= 7) {
                if (type == ButtonRelease) return;
                e.kind = core::RawEvent::Wheel;
                e.horizontal = detail >= 6;
                e.wheelDelta = (detail == 4 || detail == 7) ? 120 : -120;
            } else {
                static const QHash<int, core::MouseButton> kButtons = {{1, core::MouseButton::Left}, {2, core::MouseButton::Middle},
                    {3, core::MouseButton::Right}, {8, core::MouseButton::X1}, {9, core::MouseButton::X2}};
                if (!kButtons.contains(detail)) return;
                e.button = kButtons.value(detail);
                e.kind = type == ButtonPress ? core::RawEvent::Down : core::RawEvent::Up;
                if (type == ButtonPress) {
                    e.windowId = client;
                    e.exe = exeOfPid(getCardinal(query, client, "_NET_WM_PID"));
                    e.title = windowTitle(query, client);
                }
            }
        } else if (type == KeyPress || type == KeyRelease) {
            const Window active = activeWindow(query);
            if (isOwn(query, clientOf(query, active) ? clientOf(query, active) : active)) return;   // typing into our window
            e.windowOrigin = windowOrigin(query, active);
            if (isHotkey(detail, state)) { swallowUp.insert(detail); return; }
            if (type == KeyRelease && swallowUp.remove(detail)) return;
            const unsigned long ks = XkbKeycodeToKeysym(query, KeyCode(detail), 0, 0);
            e.kind = type == KeyPress ? core::RawEvent::KeyDown : core::RawEvent::KeyUp;
            e.key = lnx::nameFromKeysym(ks);
            e.isModifier = !lnx::modifierFromKeysym(ks).isEmpty();
            if (e.isModifier) e.key = lnx::modifierFromKeysym(ks);
            if (e.key.isEmpty()) return;
            if (type == KeyPress) {
                e.windowId = active;
                e.exe = exeOfPid(getCardinal(query, active, "_NET_WM_PID"));
                e.title = windowTitle(query, active);
                if (!e.isModifier) {
                    unsigned mods = 0;
                    KeySym typed = 0;
                    if (XkbLookupKeySym(query, KeyCode(detail), state, &mods, &typed))
                        if (const uint cp = lnx::charFromKeysym(typed); cp >= 0x20) e.ch = QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
                }
            }
        } else {
            return;
        }
        QMetaObject::invokeMethod(q, [this, e] { emit q->rawEvent(e); }, Qt::QueuedConnection);
    }
    void startRecording()
    {
        if (recordThread.joinable()) return;
        XRecordClientSpec clients = XRecordAllClients;
        XRecordRange* range = XRecordAllocRange();
        range->device_events.first = KeyPress;
        range->device_events.last = MotionNotify;
        ctx = XRecordCreateContext(ctrl, 0, &clients, 1, &range, 1);
        XFree(range);
        XSync(ctrl, False);
        recordThread = std::thread([this] { XRecordEnableContext(data, ctx, &Impl::recordCallback, reinterpret_cast<XPointer>(this)); });
    }
    void stopRecording()
    {
        if (!recordThread.joinable()) return;
        XRecordDisableContext(ctrl, ctx);
        XSync(ctrl, False);
        recordThread.join();
        XRecordFreeContext(ctrl, ctx);
        XSync(ctrl, False);
        ctx = 0;
    }
};

X11Backend::X11Backend(QObject* parent, core::Injector* playbackInjector) : Backend(parent), d(new Impl)
{
    qRegisterMetaType<core::RawEvent>();
    d->q = this;
    XSetErrorHandler(errorHandler);
    d->ctrl = XOpenDisplay(nullptr);
    d->data = XOpenDisplay(nullptr);
    d->query = XOpenDisplay(nullptr);
    d->hk = XOpenDisplay(nullptr);
    int major = 0, minor = 0, ev = 0, err = 0;
    if (!d->ctrl || !d->data || !d->query || !d->hk)
        d->unavailable = QStringLiteral("Couldn't connect to the X11 display.");
    else if (!XTestQueryExtension(d->ctrl, &ev, &err, &major, &minor))
        d->unavailable = QStringLiteral("The X server doesn't support XTest, which playback needs.");
    else if (!XRecordQueryVersion(d->ctrl, &major, &minor))
        d->unavailable = QStringLiteral("The X server doesn't support XRecord, which recording needs.");
    d->xinj = std::make_unique<X11Injector>([this](const Expected& e) { d->expect(e); });
    d->inj = playbackInjector ? playbackInjector : d->xinj.get();
    if (d->hk && pipe(d->wake) == 0) d->hotkeyThread = std::thread([this] { d->hotkeyLoop(); });
}

X11Backend::~X11Backend()
{
    d->stopRecording();
    if (d->hotkeyThread.joinable()) {
        d->runOnHotkeyThread([this] {
            QMutexLocker l(&d->hkMx);
            for (const auto& h : std::as_const(d->hotkeys)) XUngrabKey(d->hk, h.kc, AnyModifier, DefaultRootWindow(d->hk));
            XSync(d->hk, False);
        });
        d->quit = true;
        const char c = 1;
        (void)!write(d->wake[1], &c, 1);
        d->hotkeyThread.join();
    }
    for (Display* x : {d->ctrl, d->data, d->query, d->hk}) if (x) XCloseDisplay(x);
    if (d->wake[0] >= 0) { close(d->wake[0]); close(d->wake[1]); }
}

void X11Backend::setNotes(const QString& n) { d->notes = n; }
void X11Backend::setDisplayName(const QString& n) { d->name = n; }
void X11Backend::setLimitedTag(const QString& t) { d->limited = t; }
QString X11Backend::limitedTag() const { return d->limited; }
QString X11Backend::name() const { return d->name; }
QString X11Backend::unavailableReason() const { return d->unavailable; }
QString X11Backend::notes() const { return d->notes; }
core::Injector* X11Backend::injector() { return d->inj; }
int X11Backend::doubleClickMs() const { return QGuiApplication::styleHints()->mouseDoubleClickInterval(); }

void X11Backend::setCapture(CaptureMode m)
{
    if (!d->unavailable.isEmpty()) return;
    d->mode = m;
    if (m == CaptureMode::Off) d->stopRecording(); else d->startRecording();
}

bool X11Backend::setHotkey(int id, const QKeySequence& seq, QString* error)
{
    unsigned long ks = 0;
    unsigned mods = 0;
    const bool clear = seq.isEmpty();
    if (!clear && !lnx::keysymFromQt(seq[0], &ks, &mods)) {
        if (error) *error = QStringLiteral("“%1” can't be used as a hotkey.").arg(seq.toString(QKeySequence::NativeText));
        return false;
    }
    if (!d->hk || !d->hotkeyThread.joinable()) {
        if (error && !clear) *error = QStringLiteral("Global hotkeys aren't available.");
        return clear;
    }
    bool ok = true;
    d->runOnHotkeyThread([&] {
        const Window root = DefaultRootWindow(d->hk);
        static const unsigned kIgnore[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};   // Caps Lock / Num Lock on or off
        QMutexLocker l(&d->hkMx);
        if (d->hotkeys.contains(id)) {
            const auto h = d->hotkeys.take(id);
            for (unsigned ig : kIgnore) XUngrabKey(d->hk, h.kc, h.mods | ig, root);
        }
        if (clear) { XSync(d->hk, False); return; }
        const int kc = XKeysymToKeycode(d->hk, ks);
        if (!kc) { ok = false; return; }
        g_xerror = 0;
        for (unsigned ig : kIgnore) XGrabKey(d->hk, kc, mods | ig, root, False, GrabModeAsync, GrabModeAsync);
        XSync(d->hk, False);
        if (g_xerror) {   // BadAccess: another program has this key
            for (unsigned ig : kIgnore) XUngrabKey(d->hk, kc, mods | ig, root);
            XSync(d->hk, False);
            ok = false;
            return;
        }
        d->hotkeys.insert(id, {kc, mods});
    });
    if (!ok && error) *error = QStringLiteral("%1 is already used by another program.").arg(seq.toString(QKeySequence::NativeText));
    return ok;
}

}  // namespace platform
