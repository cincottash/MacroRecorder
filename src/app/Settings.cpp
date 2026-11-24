#include "Settings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>

namespace {

// One table drives load and save, so the two never disagree. Unknown keys in the file are ignored and missing
// keys keep their defaults, so old or newer settings files never stop the app from starting.
template <typename F>
void fields(Settings& s, F&& f)
{
    f("theme", s.theme);
    f("folded_cards", s.foldedCards);
    f("show_log", s.showLog);
    f("detailed_log", s.detailedLog);
    f("always_on_top", s.alwaysOnTop);
    f("window_opacity", s.windowOpacity);
    f("custom_themes", s.customThemes);
    f("geometry", s.geometry);
    f("table_state", s.tableState);
    f("fit_value_column", s.fitValueColumn);
    f("speed_pct", s.speedPct);
    f("repeat_mode", s.repeatMode);
    f("repeat_times", s.repeatTimes);
    f("repeat_seconds", s.repeatSeconds);
    f("path_style", s.pathStyle);
    f("typing", s.typing);
    f("countdown", s.countdown);
    f("countdown_seconds", s.countdownSeconds);
    f("stop_on_key", s.stopOnKey);
    f("stop_on_mouse", s.stopOnMouse);
    f("block_keyboard", s.blockKeyboard);
    f("restore_mouse", s.restoreMouse);
    f("hide_while_playing", s.hideWhilePlaying);
    f("show_notifications", s.showNotifications);
    f("after_playback", s.afterPlayback);
    f("record_moves", s.recordMoves);
    f("min_move_px", s.minMovePx);
    f("record_keyboard", s.recordKeyboard);
    f("merge_typing", s.mergeTyping);
    f("shortest_pause_ms", s.shortestPauseMs);
    f("hide_while_recording", s.hideWhileRecording);
    f("relative_positions", s.relativePositions);
    f("path_detail", s.pathDetail);
    f("hotkey_record", s.hkRecord);
    f("hotkey_play", s.hkPlay);
    f("hotkey_stop", s.hkStop);
    f("hotkey_new_recording", s.hkNewRecording);
    f("hotkey_play_from", s.hkPlayFrom);
    f("hotkey_play_selection", s.hkPlaySelection);
    f("hotkey_capture", s.hkCapture);
    f("confirm_delete", s.confirmDelete);
    f("start_minimized", s.startMinimized);
    f("show_position", s.showPosition);
    f("show_preview", s.showPreview);
    f("scan_codes", s.scanCodes);
    f("recent", s.recent);
    f("last_folder", s.lastFolder);
    f("macro_hotkeys", s.macroHotkeys);
    f("schedules", s.schedules);
}

const char* const kRepeats[] = {"once", "daily", "days", "minutes"};

void get(const QJsonObject& o, const char* k, QList<MacroHotkey>& v)
{
    if (!o.value(QLatin1String(k)).isArray()) return;
    v.clear();
    for (const QJsonValue& x : o.value(QLatin1String(k)).toArray()) {
        const QJsonObject h = x.toObject();
        MacroHotkey m{h.value(QStringLiteral("file")).toString(), h.value(QStringLiteral("key")).toString()};
        if (!m.file.isEmpty()) v << m;
    }
}
void put(QJsonObject& o, const char* k, const QList<MacroHotkey>& v)
{
    QJsonArray a;
    for (const MacroHotkey& m : v) a.append(QJsonObject{{QStringLiteral("file"), m.file}, {QStringLiteral("key"), m.key}});
    o[QLatin1String(k)] = a;
}
void get(const QJsonObject& o, const char* k, QList<Schedule>& v)
{
    if (!o.value(QLatin1String(k)).isArray()) return;
    v.clear();
    for (const QJsonValue& x : o.value(QLatin1String(k)).toArray()) {
        const QJsonObject j = x.toObject();
        Schedule s;
        s.file = j.value(QStringLiteral("file")).toString();
        s.enabled = j.value(QStringLiteral("enabled")).toBool(true);
        const QString r = j.value(QStringLiteral("repeat")).toString();
        for (const char* known : kRepeats) if (r == QLatin1String(known)) s.repeat = r;
        s.at = QDateTime::fromString(j.value(QStringLiteral("at")).toString(), Qt::ISODate);
        s.days = j.value(QStringLiteral("days")).toInt(0x1F) & 0x7F;
        s.minutes = qBound(1, j.value(QStringLiteral("minutes")).toInt(60), 100000);
        s.lastRun = QDateTime::fromString(j.value(QStringLiteral("last_run")).toString(), Qt::ISODate);
        if (!s.file.isEmpty() && s.at.isValid()) v << s;
    }
}
void put(QJsonObject& o, const char* k, const QList<Schedule>& v)
{
    QJsonArray a;
    for (const Schedule& s : v) {
        QJsonObject j{{QStringLiteral("file"), s.file}, {QStringLiteral("enabled"), s.enabled}, {QStringLiteral("repeat"), s.repeat},
                      {QStringLiteral("at"), s.at.toString(Qt::ISODate)}, {QStringLiteral("days"), s.days}, {QStringLiteral("minutes"), s.minutes}};
        if (s.lastRun.isValid()) j[QStringLiteral("last_run")] = s.lastRun.toString(Qt::ISODate);
        a.append(j);
    }
    o[QLatin1String(k)] = a;
}

void get(const QJsonObject& o, const char* k, QString& v) { if (o.value(QLatin1String(k)).isString()) v = o.value(QLatin1String(k)).toString(); }
void get(const QJsonObject& o, const char* k, bool& v) { if (o.value(QLatin1String(k)).isBool()) v = o.value(QLatin1String(k)).toBool(); }
void get(const QJsonObject& o, const char* k, int& v) { if (o.value(QLatin1String(k)).isDouble()) v = o.value(QLatin1String(k)).toInt(); }
void get(const QJsonObject& o, const char* k, QByteArray& v)
{
    if (o.value(QLatin1String(k)).isString()) v = QByteArray::fromBase64(o.value(QLatin1String(k)).toString().toLatin1());
}
void get(const QJsonObject& o, const char* k, QStringList& v)
{
    if (!o.value(QLatin1String(k)).isArray()) return;
    v.clear();
    for (const QJsonValue& x : o.value(QLatin1String(k)).toArray()) v << x.toString();
}

void put(QJsonObject& o, const char* k, const QString& v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, bool v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, int v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, const QByteArray& v) { o[QLatin1String(k)] = QString::fromLatin1(v.toBase64()); }
void put(QJsonObject& o, const char* k, const QStringList& v) { o[QLatin1String(k)] = QJsonArray::fromStringList(v); }

}  // namespace

QDateTime Schedule::nextRun(const QDateTime& after) const
{
    if (!at.isValid()) return {};
    if (repeat == QLatin1String("once")) return at > after ? at : QDateTime();
    if (repeat == QLatin1String("minutes")) {
        // Every N minutes from the first run on, at steady times (12:00, 12:15, ...) however late the app started
        if (at > after) return at;
        const qint64 step = qint64(qMax(1, minutes)) * 60;
        return at.addSecs((at.secsTo(after) / step + 1) * step);
    }
    const QTime t = QTime(at.time().hour(), at.time().minute());
    for (int d = 0; d <= 8; ++d) {
        const QDate day = after.date().addDays(d);
        const QDateTime when(day, t);
        if (when <= after) continue;
        if (repeat == QLatin1String("daily") || (days & (1 << (day.dayOfWeek() - 1)))) return when;
    }
    return {};
}

QString Schedule::describe() const
{
    const QLocale loc;
    const QString time = loc.toString(at.time(), QLocale::ShortFormat);
    if (repeat == QLatin1String("once")) return QStringLiteral("Once, %1").arg(loc.toString(at, QLocale::ShortFormat));
    if (repeat == QLatin1String("minutes"))
        return QStringLiteral("Every %1 min from %2").arg(loc.toString(minutes), loc.toString(at, QLocale::ShortFormat));
    if (repeat == QLatin1String("daily") || (days & 0x7F) == 0x7F) return QStringLiteral("Every day at %1").arg(time);
    if ((days & 0x7F) == 0x1F) return QStringLiteral("Weekdays at %1").arg(time);
    if ((days & 0x7F) == 0x60) return QStringLiteral("Weekends at %1").arg(time);
    QStringList names;
    for (int d = 1; d <= 7; ++d)
        if (days & (1 << (d - 1))) names << loc.dayName(d, QLocale::ShortFormat);
    if (names.isEmpty()) return QStringLiteral("No days chosen");
    return QStringLiteral("%1 at %2").arg(names.join(QStringLiteral(", ")), time);
}

bool Settings::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    // Before version 0.2 one setting covered both; it still applies when the newer ones aren't there.
    if (o.value(QLatin1String("stop_on_user_input")).isBool()) stopOnKey = stopOnMouse = o.value(QLatin1String("stop_on_user_input")).toBool();
    fields(*this, [&](const char* k, auto& v) { get(o, k, v); });
    // Keep values sane even if the file was edited by hand.
    speedPct = qBound(10, speedPct, 1000);
    repeatTimes = qBound(1, repeatTimes, 1000000);
    repeatSeconds = qBound(1, repeatSeconds, 864000);
    countdownSeconds = qBound(1, countdownSeconds, 60);
    minMovePx = qBound(1, minMovePx, 100);
    shortestPauseMs = qBound(0, shortestPauseMs, 60000);
    pathDetail = qBound(0, pathDetail, 100);
    windowOpacity = qBound(30, windowOpacity, 100);
    static const QStringList kAfter = {QStringLiteral("nothing"), QStringLiteral("quit"), QStringLiteral("lock"), QStringLiteral("sleep"),
                                       QStringLiteral("logoff"), QStringLiteral("restart"), QStringLiteral("shutdown")};
    if (!kAfter.contains(afterPlayback)) afterPlayback = QStringLiteral("nothing");
    while (recent.size() > 10) recent.removeLast();
    return true;
}

bool Settings::save(const QString& path) const
{
    QJsonObject o;
    fields(const_cast<Settings&>(*this), [&](const char* k, auto& v) { put(o, k, v); });
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

void Settings::resetView()
{
    const Settings d;
    theme = d.theme;
    foldedCards = d.foldedCards;
    showLog = d.showLog;
    detailedLog = d.detailedLog;
    alwaysOnTop = d.alwaysOnTop;
    windowOpacity = d.windowOpacity;
    showPosition = d.showPosition;
    showPreview = d.showPreview;
    tableState.clear();
    fitValueColumn = d.fitValueColumn;
}

void Settings::resetOptions()
{
    const Settings d;
    const QStringList keepRecent = recent;
    const QString keepFolder = lastFolder;
    const QString keepTheme = theme;
    const QStringList keepFolded = foldedCards;
    const bool keepLog = showLog, keepDetail = detailedLog, keepTop = alwaysOnTop;
    const int keepOpacity = windowOpacity;
    const QStringList keepThemes = customThemes;
    const QList<MacroHotkey> keepHotkeys = macroHotkeys;
    const QList<Schedule> keepSchedules = schedules;
    const QByteArray keepGeo = geometry, keepTable = tableState;
    *this = d;
    windowOpacity = keepOpacity;
    customThemes = keepThemes;
    macroHotkeys = keepHotkeys;
    schedules = keepSchedules;
    recent = keepRecent;
    lastFolder = keepFolder;
    theme = keepTheme;
    foldedCards = keepFolded;
    showLog = keepLog;
    detailedLog = keepDetail;
    alwaysOnTop = keepTop;
    geometry = keepGeo;
    tableState = keepTable;
}
