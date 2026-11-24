#include "ImageFind.h"

#include <QVector>

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace core {

namespace {

inline int gray(QRgb c) { return (qRed(c) * 77 + qGreen(c) * 150 + qBlue(c) * 29) >> 8; }

struct Sample { int x, y, g; };

// Up to `count` points of the needle where its brightness changes most (edges of text and icons), spread over the
// picture, plus its corners and center: a few of them rule out almost every wrong position at once.
QVector<Sample> pickSamples(const QImage& n, int count)
{
    const int w = n.width(), h = n.height();
    struct Cand { int x, y, contrast; };
    QVector<Cand> cands;
    cands.reserve(w * h);
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(n.constScanLine(y));
        const QRgb* below = reinterpret_cast<const QRgb*>(n.constScanLine(qMin(y + 1, h - 1)));
        for (int x = 0; x < w; ++x) {
            const int g = gray(row[x]);
            const int c = std::abs(g - gray(row[qMin(x + 1, w - 1)])) + std::abs(g - gray(below[x]));
            cands.append({x, y, c});
        }
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.contrast > b.contrast; });
    QVector<Sample> out;
    auto add = [&](int x, int y) {
        for (const Sample& s : out)
            if (s.x == x && s.y == y) return;
        out.append({x, y, gray(reinterpret_cast<const QRgb*>(n.constScanLine(y))[x])});
    };
    // Spread: skip candidates too close to one already taken
    const int minGap = qMax(1, qMin(w, h) / 8);
    for (const Cand& c : cands) {
        if (out.size() >= count) break;
        bool close = false;
        for (const Sample& s : out)
            if (std::abs(s.x - c.x) < minGap && std::abs(s.y - c.y) < minGap) { close = true; break; }
        if (!close) add(c.x, c.y);
    }
    add(w / 2, h / 2);
    add(0, 0);
    add(w - 1, 0);
    add(0, h - 1);
    add(w - 1, h - 1);
    return out;
}

}  // namespace

ImageMatch findImage(const QImage& haystackIn, const QImage& needleIn, int tolerancePct)
{
    ImageMatch best;
    if (haystackIn.isNull() || needleIn.isNull()) return best;
    const QImage hay = haystackIn.convertToFormat(QImage::Format_RGB32);
    const QImage nd = needleIn.convertToFormat(QImage::Format_RGB32);
    const int W = hay.width(), H = hay.height(), w = nd.width(), h = nd.height();
    if (w > W || h > H || w <= 0 || h <= 0) return best;

    const double maxMean = qBound(0, tolerancePct, 100) * 255.0 / 100.0;
    const qint64 channels = qint64(w) * h * 3;
    qint64 limit = qint64(maxMean * double(channels));          // full comparison: the sum of differences allowed
    const QVector<Sample> samples = pickSamples(nd, 40);
    // The sampled check is looser (samples sit on edges, where anti-aliasing differs most): it must never rule out
    // a position the full comparison would accept.
    const int sampleLimit = int(qMin(255.0, maxMean * 3 + (tolerancePct > 0 ? 12 : 0)));
    const qint64 sampleSumLimit = qint64(sampleLimit) * samples.size();

    // The haystack's brightness, once
    QVector<quint8> hg(qsizetype(W) * H);
    for (int y = 0; y < H; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(hay.constScanLine(y));
        quint8* out = hg.data() + qsizetype(y) * W;
        for (int x = 0; x < W; ++x) out[x] = quint8(gray(row[x]));
    }

    qint64 bestSum = limit + 1;
    for (int y = 0; y <= H - h; ++y) {
        for (int x = 0; x <= W - w; ++x) {
            qint64 s = 0;
            bool pass = true;
            for (const Sample& sm : samples) {
                const int d = std::abs(int(hg[qsizetype(y + sm.y) * W + x + sm.x]) - sm.g);
                if (d > sampleLimit) { pass = false; break; }
                s += d;
            }
            if (!pass || s > sampleSumLimit) continue;
            // Full comparison in color, stopping as soon as it's worse than the best so far
            qint64 sum = 0;
            for (int yy = 0; yy < h && sum < bestSum; ++yy) {
                const QRgb* a = reinterpret_cast<const QRgb*>(hay.constScanLine(y + yy)) + x;
                const QRgb* b = reinterpret_cast<const QRgb*>(nd.constScanLine(yy));
                for (int xx = 0; xx < w; ++xx) {
                    const QRgb p = a[xx], q = b[xx];
                    sum += std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q));
                }
            }
            if (sum < bestSum) {
                bestSum = sum;
                best.found = true;
                best.topLeft = QPoint(x, y);
                best.score = double(sum) / double(channels);
                if (sum == 0) return best;   // can't do better
            }
        }
    }
    return best;
}

namespace {
QString squash(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s)
        if (!c.isSpace()) out += c.toCaseFolded();
    return out;
}
QVector<TextWord> readingOrder(const QVector<TextWord>& words)
{
    QVector<TextWord> w = words;
    std::stable_sort(w.begin(), w.end(), [](const TextWord& a, const TextWord& b) {
        return a.line != b.line ? a.line < b.line : a.box.x() < b.box.x();
    });
    return w;
}
}  // namespace

QRect findPhrase(const QVector<TextWord>& words, const QString& phrase)
{
    const QString want = squash(phrase);
    if (want.isEmpty()) return {};
    const QVector<TextWord> w = readingOrder(words);
    for (int i = 0; i < w.size(); ++i) {
        const int first = int(squash(w[i].text).size());
        QString run;
        for (int j = i; j < w.size() && j < i + 40; ++j) {
            run += squash(w[j].text);
            const int at = int(run.indexOf(want));
            if (at >= 0) {
                if (at >= first) break;   // it starts in a later word: found from that word instead
                QRect box = w[i].box;
                for (int k = i + 1; k <= j; ++k) box = box.united(w[k].box);
                return box;
            }
            if (run.size() > want.size() + first) break;   // longer than the phrase can reach from this word
        }
    }
    return {};
}

QString joinText(const QVector<TextWord>& words)
{
    QString out;
    int line = INT_MIN;
    for (const TextWord& t : readingOrder(words)) {
        if (line != INT_MIN) out += t.line == line ? QStringLiteral(" ") : QStringLiteral("\n");
        out += t.text;
        line = t.line;
    }
    return out;
}

}  // namespace core
