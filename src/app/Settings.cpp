#include "Settings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    f("geometry", s.geometry);
    f("table_state", s.tableState);
    f("speed_pct", s.speedPct);
    f("repeat_mode", s.repeatMode);
    f("repeat_times", s.repeatTimes);
    f("repeat_seconds", s.repeatSeconds);
    f("path_style", s.pathStyle);
    f("typing", s.typing);
    f("countdown", s.countdown);
    f("countdown_seconds", s.countdownSeconds);
    f("stop_on_user_input", s.stopOnUserInput);
    f("restore_mouse", s.restoreMouse);
    f("hide_while_playing", s.hideWhilePlaying);
    f("record_moves", s.recordMoves);
    f("min_move_px", s.minMovePx);
    f("record_keyboard", s.recordKeyboard);
    f("merge_typing", s.mergeTyping);
    f("shortest_pause_ms", s.shortestPauseMs);
    f("hide_while_recording", s.hideWhileRecording);
    f("hotkey_record", s.hkRecord);
    f("hotkey_play", s.hkPlay);
    f("hotkey_stop", s.hkStop);
    f("recent", s.recent);
    f("last_folder", s.lastFolder);
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

bool Settings::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    fields(*this, [&](const char* k, auto& v) { get(o, k, v); });
    // Keep values sane even if the file was edited by hand.
    speedPct = qBound(10, speedPct, 1000);
    repeatTimes = qBound(1, repeatTimes, 1000000);
    repeatSeconds = qBound(1, repeatSeconds, 864000);
    countdownSeconds = qBound(1, countdownSeconds, 60);
    minMovePx = qBound(1, minMovePx, 100);
    shortestPauseMs = qBound(0, shortestPauseMs, 60000);
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
    tableState.clear();
}

void Settings::resetOptions()
{
    const Settings d;
    const QStringList keepRecent = recent;
    const QString keepFolder = lastFolder;
    const QString keepTheme = theme;
    const QStringList keepFolded = foldedCards;
    const bool keepLog = showLog, keepDetail = detailedLog, keepTop = alwaysOnTop;
    const QByteArray keepGeo = geometry, keepTable = tableState;
    *this = d;
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
