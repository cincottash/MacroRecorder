// Windows.Media.Ocr through its binary interface. MinGW has no headers for it, so the few interfaces needed are
// called by their slot in the method table, with the interface ids and method order taken from Windows' own
// metadata (Windows.Media.Ocr.winmd, Windows.Graphics.Imaging, Windows.Foundation):
//   IOcrEngineStatics  {5bffa85a-...}: 6 MaxImageDimension, 10 TryCreateFromUserProfileLanguages
//   IOcrEngine: 6 RecognizeAsync          IOcrResult: 6 Lines        IOcrLine: 6 Words      IOcrWord: 6 BoundingRect, 7 Text
//   IVectorView<T>: 6 GetAt, 7 Size       IAsyncOperation<T>: 8 GetResults               IAsyncInfo: 7 Status, 9 Cancel
//   ISoftwareBitmapFactory: 6 Create      ISoftwareBitmap: 15 LockBuffer                 IBitmapBuffer: 7 GetPlaneDescription
//   IMemoryBuffer: 6 CreateReference      IMemoryBufferByteAccess (plain COM): 3 GetBuffer   IClosable: 6 Close
// (slots 0-5 are IUnknown's and IInspectable's).
#include "WinOcr.h"
#include "core/Tr.h"

#include <QElapsedTimer>

#include <windows.h>
#include <roapi.h>
#include <winstring.h>

#include <thread>

namespace platform::win {

namespace {

const GUID kOcrEngineStatics = {0x5bffa85a, 0x3384, 0x3540, {0x99, 0x40, 0x69, 0x91, 0x20, 0xd4, 0x28, 0xa8}};
const GUID kSoftwareBitmapFactory = {0xc99feb69, 0x2d62, 0x4d47, {0xa6, 0xb3, 0x4f, 0xdb, 0x6a, 0x07, 0xfd, 0xf8}};
const GUID kMemoryBuffer = {0xfbc4dd2a, 0x245b, 0x11e4, {0xaf, 0x98, 0x68, 0x94, 0x23, 0x26, 0x0c, 0xf8}};
const GUID kMemoryBufferByteAccess = {0x5b0d3235, 0x4dba, 0x4d44, {0x86, 0x5e, 0x8f, 0x1d, 0x0e, 0x4f, 0xd0, 0x4d}};
const GUID kAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kClosable = {0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e}};
constexpr INT32 kBgra8 = 87;   // BitmapPixelFormat
constexpr INT32 kWrite = 2;    // BitmapBufferAccessMode

// A COM pointer released at the end of its scope
struct Ptr {
    IUnknown* p = nullptr;
    Ptr() = default;
    Ptr(const Ptr&) = delete;
    ~Ptr() { if (p) p->Release(); }
    void** out() { return reinterpret_cast<void**>(&p); }
};

// The method in slot `slot` of the object's method table
template <typename... A>
HRESULT call(IUnknown* obj, int slot, A... args)
{
    using Fn = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(obj))[slot])(obj, args...);
}

struct HStr {
    HSTRING h = nullptr;
    explicit HStr(const wchar_t* s) { WindowsCreateString(s, UINT32(wcslen(s)), &h); }
    ~HStr() { if (h) WindowsDeleteString(h); }
};

QString fromH(HSTRING h)
{
    UINT32 n = 0;
    const wchar_t* s = WindowsGetStringRawBuffer(h, &n);
    return QString::fromWCharArray(s, int(n));
}

struct RectF { float x, y, w, h; };
struct Plane { INT32 start, width, height, stride; };

void close(IUnknown* obj)
{
    Ptr c;
    if (obj && SUCCEEDED(obj->QueryInterface(kClosable, c.out())) && c.p) call(c.p, 6);
}

// One piece of the picture (at most the engine's size). Line numbers continue from lineBase.
bool readPiece(IUnknown* engine, IUnknown* factory, const QImage& img, QVector<core::TextWord>* words, int lineBase, int* lines, QString* error)
{
    Ptr bmp;
    if (FAILED(call(factory, 6, kBgra8, INT32(img.width()), INT32(img.height()), bmp.out())) || !bmp.p) {
        *error = T("the picture couldn't be handed to the text recognition.");
        return false;
    }
    {
        Ptr buffer, mem, ref, bytes;
        Plane pd{};
        BYTE* data = nullptr;
        UINT32 cap = 0;
        if (FAILED(call(bmp.p, 15, kWrite, buffer.out())) || !buffer.p || FAILED(call(buffer.p, 7, INT32(0), &pd))
            || FAILED(buffer.p->QueryInterface(kMemoryBuffer, mem.out())) || FAILED(call(mem.p, 6, ref.out())) || !ref.p
            || FAILED(ref.p->QueryInterface(kMemoryBufferByteAccess, bytes.out())) || FAILED(call(bytes.p, 3, &data, &cap)) || !data) {
            *error = T("the picture couldn't be copied for the text recognition.");
            return false;
        }
        const QImage src = img.convertToFormat(QImage::Format_RGB32);   // B, G, R, 255 in memory: Bgra8
        const qsizetype row = qsizetype(src.width()) * 4;
        for (int y = 0; y < src.height(); ++y) {
            const qsizetype at = qsizetype(pd.start) + qsizetype(y) * pd.stride;
            if (at + row > qsizetype(cap)) break;
            memcpy(data + at, src.constScanLine(y), size_t(row));
        }
        close(ref.p);
        close(buffer.p);
    }
    Ptr op, info, result, lineList;
    if (FAILED(call(engine, 6, bmp.p, op.out())) || !op.p || FAILED(op.p->QueryInterface(kAsyncInfo, info.out()))) {
        *error = T("the text recognition didn't start.");
        return false;
    }
    INT32 status = 0;   // 0 Started, 1 Completed, 2 Canceled, 3 Error
    QElapsedTimer t;
    t.start();
    while (SUCCEEDED(call(info.p, 7, &status)) && status == 0) {
        if (t.elapsed() > 20000) {
            call(info.p, 9);
            *error = T("the text recognition took too long.");
            return false;
        }
        Sleep(3);
    }
    if (status != 1 || FAILED(call(op.p, 8, result.out())) || !result.p || FAILED(call(result.p, 6, lineList.out())) || !lineList.p) {
        *error = T("the text recognition failed.");
        return false;
    }
    UINT32 n = 0;
    call(lineList.p, 7, &n);
    for (UINT32 i = 0; i < n; ++i) {
        Ptr line, wordList;
        if (FAILED(call(lineList.p, 6, i, line.out())) || !line.p || FAILED(call(line.p, 6, wordList.out())) || !wordList.p) continue;
        UINT32 m = 0;
        call(wordList.p, 7, &m);
        for (UINT32 j = 0; j < m; ++j) {
            Ptr word;
            if (FAILED(call(wordList.p, 6, j, word.out())) || !word.p) continue;
            HSTRING h = nullptr;
            RectF r{};
            call(word.p, 7, &h);
            call(word.p, 6, &r);
            words->append({fromH(h), QRect(qRound(r.x), qRound(r.y), qMax(1, qRound(r.w)), qMax(1, qRound(r.h))), lineBase + int(i)});
            if (h) WindowsDeleteString(h);
        }
    }
    *lines = int(n);
    return true;
}

bool recognize(const QImage& img, QVector<core::TextWord>* words, QString* error)
{
    Ptr statics, engine, factory;
    HStr ocrClass(L"Windows.Media.Ocr.OcrEngine"), bitmapClass(L"Windows.Graphics.Imaging.SoftwareBitmap");
    if (FAILED(RoGetActivationFactory(ocrClass.h, kOcrEngineStatics, statics.out())) || !statics.p) {
        *error = T("Windows' text recognition isn't available (it needs Windows 10 or later).");
        return false;
    }
    if (FAILED(call(statics.p, 10, engine.out())) || !engine.p) {   // TryCreateFromUserProfileLanguages
        *error = T("Windows has no text recognition for the display languages set up (Settings > Time & "
                                "Language > Language: add the language's optional features).");
        return false;
    }
    if (FAILED(RoGetActivationFactory(bitmapClass.h, kSoftwareBitmapFactory, factory.out())) || !factory.p) {
        *error = T("Windows' imaging isn't available.");
        return false;
    }
    UINT32 maxDim = 0;
    call(statics.p, 6, &maxDim);
    if (maxDim < 64) maxDim = 2048;
    // Small screen text is read better twice as large; anything larger than the engine takes is read in pieces
    // that overlap, and a word belongs to the piece that holds its middle.
    const int scale = (img.width() * 2 <= int(maxDim) && img.height() * 2 <= int(maxDim)) ? 2 : 1;
    const QImage big = scale == 2 ? img.scaled(img.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : img;
    const int piece = int(maxDim), overlap = qMin(200, piece / 4), step = piece - overlap;
    int lineBase = 0;
    for (int y0 = 0; y0 < big.height(); y0 += step) {
        for (int x0 = 0; x0 < big.width(); x0 += step) {
            const QRect part = QRect(x0, y0, piece, piece).intersected(big.rect());
            QVector<core::TextWord> found;
            int lines = 0;
            if (!readPiece(engine.p, factory.p, big.copy(part), &found, lineBase, &lines, error)) return false;
            lineBase += lines;
            // This piece's own part: from the middle of the overlap on each side that has a neighbour
            const QRect own(x0 == 0 ? 0 : x0 + overlap / 2, y0 == 0 ? 0 : y0 + overlap / 2,
                            (part.right() + 1 >= big.width() ? big.width() : x0 + step + overlap / 2) - (x0 == 0 ? 0 : x0 + overlap / 2),
                            (part.bottom() + 1 >= big.height() ? big.height() : y0 + step + overlap / 2) - (y0 == 0 ? 0 : y0 + overlap / 2));
            for (core::TextWord w : std::as_const(found)) {
                w.box.translate(part.topLeft());
                if (!own.contains(w.box.center())) continue;
                if (scale == 2) w.box = QRect(w.box.x() / 2, w.box.y() / 2, qMax(1, w.box.width() / 2), qMax(1, w.box.height() / 2));
                words->append(w);
            }
            if (part.right() + 1 >= big.width()) break;
        }
        if (QRect(0, y0, 1, piece).intersected(big.rect()).bottom() + 1 >= big.height()) break;
    }
    return true;
}

}  // namespace

bool recognizeText(const QImage& img, QVector<core::TextWord>* words, QString* error)
{
    if (img.isNull() || img.width() < 8 || img.height() < 8) {
        if (error) *error = T("the picture is too small to read.");
        return false;
    }
    // On a thread of its own in the multithreaded apartment: the caller may be a window's thread (single-threaded
    // apartment) that mustn't be blocked while the engine works
    bool ok = false;
    QString err;
    QVector<core::TextWord> out;
    std::thread worker([&] {
        const HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
        ok = recognize(img, &out, &err);
        if (SUCCEEDED(init)) RoUninitialize();
    });
    worker.join();
    if (ok) *words = out;
    else if (error) *error = err;
    return ok;
}

}  // namespace platform::win
