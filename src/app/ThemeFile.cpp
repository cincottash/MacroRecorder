#include "ThemeFile.h"
#include "core/Tr.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace themefile {

namespace {

constexpr int kFormatVersion = 1;
constexpr qint64 kMaxFileBytes = 2 * 1024 * 1024;
constexpr int kMaxNameLength = 40;

// VS Code theme files are "JSON with comments": // and /* */ comments and trailing commas. Remove them, leaving
// strings (which may contain "//", as in a "$schema" address) untouched.
QByteArray stripJsonc(const QByteArray& in)
{
    QByteArray out;
    out.reserve(in.size());
    bool inString = false;
    for (qsizetype i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (inString) {
            out += c;
            if (c == '\\' && i + 1 < in.size()) out += in[++i];
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; out += c; continue; }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n') ++i;
            out += '\n';
            continue;
        }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) ++i;
            ++i;
            continue;
        }
        if (c == ',') {   // a trailing comma: the next thing that isn't white space closes an object or array
            qsizetype j = i + 1;
            while (j < in.size() && QChar::isSpace(uchar(in[j]))) ++j;
            if (j < in.size() && (in[j] == '}' || in[j] == ']')) continue;
        }
        out += c;
    }
    return out;
}

// "#RGB", "#RGBA", "#RRGGBB" or "#RRGGBBAA" (CSS order, as VS Code writes them). Invalid: an invalid QColor.
QColor parseColor(const QString& text)
{
    QString h = text.trimmed();
    if (!h.startsWith(QLatin1Char('#'))) return {};
    h = h.mid(1);
    if (h.size() == 3 || h.size() == 4) {
        QString longer;
        for (QChar c : h) longer += QString(2, c);
        h = longer;
    }
    if (h.size() != 6 && h.size() != 8) return {};
    bool ok = false;
    const uint v = h.toUInt(&ok, 16);
    if (!ok) return {};
    if (h.size() == 6) return QColor((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return QColor((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

QColor mix(const QColor& a, const QColor& b, double f)   // f = 0: a, 1: b
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * f), float(a.greenF() + (b.greenF() - a.greenF()) * f),
                            float(a.blueF() + (b.blueF() - a.blueF()) * f));
}

QColor over(const QColor& c, const QColor& bg)   // a color with transparency, as it looks on the background
{
    return c.alpha() == 255 ? c : mix(bg, QColor(c.red(), c.green(), c.blue()), c.alphaF());
}

double luminance(const QColor& c)
{
    auto f = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * f(c.redF()) + 0.7152 * f(c.greenF()) + 0.0722 * f(c.blueF());
}

double contrast(const QColor& a, const QColor& b)
{
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

QString hex(const QColor& c) { return c.name(QColor::HexRgb).toUpper(); }

// Fills in every color the file left out, from the ones it has (and the built-in Dark or Light theme for the
// status colors), so a theme needs only a background and a text color.
QHash<QString, QString> complete(const QHash<QString, QColor>& given, bool dark)
{
    const themes::Theme& base = themes::forChoice(dark ? QStringLiteral("dark") : QStringLiteral("light"));
    auto baseColor = [&](const char* k) { return QColor(base.c(QString::fromLatin1(k))); };
    QHash<QString, QColor> c = given;
    const QColor bg = c.value(QStringLiteral("bg"));
    auto has = [&](const char* k) { return c.contains(QString::fromLatin1(k)); };
    auto set = [&](const char* k, const QColor& v) { if (!has(k)) c.insert(QString::fromLatin1(k), v); };
    set("card", dark ? bg.lighter(118) : bg.lighter(104));
    const QColor text = c.value(QStringLiteral("text")), card = c.value(QStringLiteral("card"));
    set("accent", baseColor("accent"));
    const QColor accent = c.value(QStringLiteral("accent"));
    set("border", mix(card, text, 0.16));
    set("muted", mix(card, text, 0.62));
    set("input", dark ? bg.darker(110) : card);
    set("log_bg", c.value(QStringLiteral("input")));
    set("track", mix(card, text, 0.1));
    set("accent_hover", dark ? accent.lighter(120) : accent.darker(115));
    set("accent_text", contrast(accent, Qt::white) >= contrast(accent, Qt::black) ? QColor(Qt::white) : QColor(Qt::black));
    for (const char* k : {"ok", "warn", "err"}) set(k, baseColor(k));
    set("ok_bg", mix(card, c.value(QStringLiteral("ok")), 0.16));
    set("warn_bg", mix(card, c.value(QStringLiteral("warn")), 0.16));
    set("err_bg", mix(card, c.value(QStringLiteral("err")), 0.16));
    set("warn_border", mix(card, c.value(QStringLiteral("warn")), 0.55));
    QHash<QString, QString> out;
    for (const QString& k : colorKeys()) out.insert(k, hex(c.value(k)));
    return out;
}

}  // namespace

const QStringList& colorKeys()
{
    static const QStringList keys = {
        QStringLiteral("bg"),     QStringLiteral("card"),    QStringLiteral("border"),       QStringLiteral("text"),
        QStringLiteral("muted"),  QStringLiteral("accent"),  QStringLiteral("accent_hover"), QStringLiteral("accent_text"),
        QStringLiteral("input"),  QStringLiteral("log_bg"),  QStringLiteral("track"),        QStringLiteral("ok"),
        QStringLiteral("ok_bg"),  QStringLiteral("warn"),    QStringLiteral("warn_bg"),      QStringLiteral("warn_border"),
        QStringLiteral("err"),    QStringLiteral("err_bg"),
    };
    return keys;
}

QString keyFor(const QString& name)
{
    QString k;
    for (QChar ch : name.toLower())
        k += ch.isLetterOrNumber() ? ch : QLatin1Char('_');
    return QStringLiteral("custom_") + k;
}

Result parse(const QByteArray& data, const QString& fallbackName)
{
    Result r;
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (doc.isNull()) doc = QJsonDocument::fromJson(stripJsonc(data), &pe);
    if (!doc.isObject()) {
        r.error = T("It isn't a theme file (it isn't valid JSON: %1).").arg(pe.errorString());
        return r;
    }
    const QJsonObject o = doc.object();
    const QJsonObject colors = o.value(QStringLiteral("colors")).toObject();
    QString name = o.value(QStringLiteral("name")).toString().simplified();
    QHash<QString, QColor> given;
    bool dark = true, darkKnown = false;
    QStringList bad;

    if (colors.contains(QStringLiteral("bg"))) {
        // The app's own format: the same color names the built-in themes use.
        for (auto it = colors.begin(); it != colors.end(); ++it) {
            if (!colorKeys().contains(it.key())) continue;
            const QColor c = parseColor(it.value().toString());
            if (c.isValid()) given.insert(it.key(), c);
            else bad << it.key();
        }
        const QString base = o.value(QStringLiteral("base")).toString().toLower();
        if (base == QLatin1String("dark") || base == QLatin1String("light")) {
            dark = base == QLatin1String("dark");
            darkKnown = true;
        }
    } else if (colors.contains(QStringLiteral("editor.background"))) {
        // A VS Code color theme: take the colors that play the same part in its window.
        const QColor bg = parseColor(colors.value(QStringLiteral("editor.background")).toString());
        if (!bg.isValid()) {
            r.error = T("Its background color (\"editor.background\") isn't a color.");
            return r;
        }
        const QColor solidBg(bg.red(), bg.green(), bg.blue());
        given.insert(QStringLiteral("bg"), solidBg);
        const QList<std::pair<const char*, QStringList>> map = {
            {"text", {QStringLiteral("editor.foreground"), QStringLiteral("foreground")}},
            {"card", {QStringLiteral("sideBar.background"), QStringLiteral("editorWidget.background"), QStringLiteral("panel.background")}},
            {"muted", {QStringLiteral("descriptionForeground"), QStringLiteral("editorLineNumber.foreground")}},
            {"accent", {QStringLiteral("button.background"), QStringLiteral("focusBorder"), QStringLiteral("activityBarBadge.background")}},
            {"accent_text", {QStringLiteral("button.foreground"), QStringLiteral("activityBarBadge.foreground")}},
            {"input", {QStringLiteral("input.background")}},
            {"border", {QStringLiteral("panel.border"), QStringLiteral("editorGroup.border"), QStringLiteral("widget.border")}},
            {"log_bg", {QStringLiteral("terminal.background"), QStringLiteral("panel.background")}},
            {"ok", {QStringLiteral("terminal.ansiGreen"), QStringLiteral("gitDecoration.addedResourceForeground")}},
            {"warn", {QStringLiteral("editorWarning.foreground"), QStringLiteral("terminal.ansiYellow")}},
            {"err", {QStringLiteral("editorError.foreground"), QStringLiteral("terminal.ansiRed")}},
        };
        for (const auto& [key, sources] : map) {
            for (const QString& s : sources) {
                const QColor c = parseColor(colors.value(s).toString());
                if (c.isValid() && c.alpha() > 0) { given.insert(QString::fromLatin1(key), over(c, solidBg)); break; }
            }
        }
        const QString type = o.value(QStringLiteral("type")).toString().toLower();
        if (type == QLatin1String("dark") || type == QLatin1String("light") || type == QLatin1String("hc")) {
            dark = type != QLatin1String("light");
            darkKnown = true;
        }
    } else {
        r.error = T("No theme colors were found in it. A theme file needs a \"colors\" section with at least "
                                 "\"bg\" (or, for a VS Code theme, \"editor.background\").");
        return r;
    }

    if (!given.contains(QStringLiteral("bg")) || !given.value(QStringLiteral("bg")).isValid()) {
        r.error = T("Its background color (\"bg\") isn't a color like #1E2128.");
        return r;
    }
    const QColor bg = given.value(QStringLiteral("bg"));
    if (!darkKnown) dark = luminance(bg) < 0.2;
    if (!given.contains(QStringLiteral("text")))
        given.insert(QStringLiteral("text"), dark ? QColor(0xE6, 0xE8, 0xEB) : QColor(0x1F, 0x23, 0x28));
    if (!bad.isEmpty())
        r.notes << T("These colors weren't valid and were worked out from the others instead: %1.").arg(bad.join(QStringLiteral(", ")));

    if (name.isEmpty()) name = fallbackName.simplified();
    if (name.isEmpty()) name = T("Custom Theme");
    if (name.size() > kMaxNameLength) name = name.left(kMaxNameLength).trimmed();
    for (const themes::Theme& t : themes::all()) {
        if (t.group != themes::customGroup() && t.name.compare(name, Qt::CaseInsensitive) == 0) {
            const QString renamed = T("%1 (Imported)").arg(name);
            r.notes << T("A built-in theme is already called \"%1\", so this one is called \"%2\".").arg(name, renamed);
            name = renamed;
            break;
        }
    }

    r.theme = themes::Theme{themes::customGroup(), name, keyFor(name), complete(given, dark)};
    const double c = contrast(QColor(r.theme.c(QStringLiteral("text"))), QColor(r.theme.c(QStringLiteral("card"))));
    if (c < 4.5)
        r.notes << T("Its text may be hard to read: the contrast between text and background is %1:1 (4.5:1 or more "
                                  "reads well).").arg(c, 0, 'f', 1);
    return r;
}

Result read(const QString& path)
{
    Result r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = T("It couldn't be opened: %1").arg(f.errorString());
        return r;
    }
    if (f.size() > kMaxFileBytes) {
        r.error = T("It's too big to be a theme file (%1 MB).").arg(f.size() / (1024.0 * 1024.0), 0, 'f', 1);
        return r;
    }
    QString fallback = QFileInfo(path).completeBaseName();
    if (fallback.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive)) fallback.chop(6);   // "Ocean.theme.json"
    if (fallback.endsWith(QLatin1String("-color-theme"), Qt::CaseInsensitive)) fallback.chop(12);   // VS Code's usual name
    return parse(f.readAll(), fallback);
}

QByteArray write(const themes::Theme& t, bool indented)
{
    QJsonObject colors;
    for (const QString& k : colorKeys()) colors.insert(k, t.c(k));
    QJsonObject o;
    o.insert(QStringLiteral("macrorecorder_theme"), kFormatVersion);
    o.insert(QStringLiteral("name"), t.name);
    o.insert(QStringLiteral("base"), luminance(QColor(t.c(QStringLiteral("bg")))) < 0.2 ? QStringLiteral("dark") : QStringLiteral("light"));
    o.insert(QStringLiteral("colors"), colors);
    return QJsonDocument(o).toJson(indented ? QJsonDocument::Indented : QJsonDocument::Compact);
}

QList<themes::Theme> fromStored(const QStringList& stored)
{
    QList<themes::Theme> list;
    for (const QString& s : stored) {
        Result r = parse(s.toUtf8(), QString());
        if (!r.ok()) continue;
        bool duplicate = false;
        for (const themes::Theme& t : list) duplicate = duplicate || t.key == r.theme.key;
        if (!duplicate) list << r.theme;
    }
    return list;
}

}  // namespace themefile
