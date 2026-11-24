#include "Action.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>

namespace core {

namespace {

struct TypeInfo { ActionType type; const char* id; };
constexpr TypeInfo kTypes[] = {
    {ActionType::MouseMove, "mouse_move"},     {ActionType::MouseClick, "mouse_click"},
    {ActionType::MouseDown, "mouse_down"},     {ActionType::MouseUp, "mouse_up"},
    {ActionType::Wheel, "wheel"},              {ActionType::KeyPress, "key_press"},
    {ActionType::KeyDown, "key_down"},         {ActionType::KeyUp, "key_up"},
    {ActionType::Text, "text"},                {ActionType::Wait, "wait"},
    {ActionType::WindowFocus, "window_focus"}, {ActionType::WaitPixel, "wait_pixel"},
    {ActionType::WaitKey, "wait_key"},         {ActionType::Repeat, "repeat"},
    {ActionType::Goto, "goto"},                {ActionType::RunProgram, "run_program"},
    {ActionType::Notify, "notify"},           {ActionType::WaitFile, "wait_file"},
    {ActionType::MessageBox, "message_box"},  {ActionType::Beep, "beep"},
    {ActionType::RunMacro, "run_macro"},
    {ActionType::SetVariable, "set_variable"}, {ActionType::If, "if"},
    {ActionType::Else, "else"},                {ActionType::EndIf, "end_if"},
    {ActionType::AskText, "ask_text"},         {ActionType::Group, "group"},
    {ActionType::EndGroup, "end_group"},       {ActionType::WindowMove, "window_move"},
    {ActionType::FindImage, "find_image"},     {ActionType::Screenshot, "screenshot"},
    {ActionType::SetClipboard, "set_clipboard"}, {ActionType::AskAI, "ask_ai"},
    {ActionType::FindText, "find_text"},
};

const char* const kButtons[] = {"left", "right", "middle", "x1", "x2"};
const char* const kMatches[] = {"exact", "starts_with", "contains", "any"};
const char* const kRepeat[] = {"times", "seconds", "forever"};
const char* const kFileWhen[] = {"exists", "gone", "changed"};
const char* const kWindowStates[] = {"normal", "maximized", "minimized", "restore"};
const char* const kFindDo[] = {"nothing", "move", "click", "double_click", "right_click"};

QJsonArray rectJson(const QRect& r) { return QJsonArray{r.x(), r.y(), r.width(), r.height()}; }
QRect rectFrom(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 4) return {};
    return QRect(a.at(0).toInt(), a.at(1).toInt(), qMax(0, a.at(2).toInt()), qMax(0, a.at(3).toInt()));
}
QString rectText(const QRect& r)
{
    return QStringLiteral("%1, %2 · %3 × %4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

template <typename E, size_t N>
E enumFrom(const QString& s, const char* const (&names)[N], E fallback)
{
    for (size_t i = 0; i < N; ++i)
        if (s == QLatin1String(names[i])) return static_cast<E>(i);
    return fallback;
}

QString num(qint64 v) { return QLocale().toString(v); }   // the system's format, as in the step editor

QString msText(int ms)
{
    if (ms < 10000) return num(ms) + QStringLiteral(" ms");
    return QLocale().toString(ms / 1000.0, 'f', 1) + QStringLiteral(" s");   // the system's decimal mark, as everywhere
}

QString combo(const QStringList& mods, const QString& key)
{
    QStringList parts = mods;
    parts << key;
    return parts.join(QLatin1Char('+'));
}

}  // namespace

QString typeId(ActionType t)
{
    for (const auto& i : kTypes)
        if (i.type == t) return QString::fromLatin1(i.id);
    return QStringLiteral("wait");
}

bool typeFromId(const QString& id, ActionType* out)
{
    for (const auto& i : kTypes)
        if (id == QLatin1String(i.id)) { *out = i.type; return true; }
    return false;
}

QString buttonName(MouseButton b)
{
    switch (b) {
    case MouseButton::Left: return QStringLiteral("Left");
    case MouseButton::Right: return QStringLiteral("Right");
    case MouseButton::Middle: return QStringLiteral("Middle");
    case MouseButton::X1: return QStringLiteral("Back (X1)");
    case MouseButton::X2: return QStringLiteral("Forward (X2)");
    }
    return {};
}

QString Action::typeName() const
{
    const QString b = buttonName(button);
    switch (type) {
    case ActionType::MouseMove: return QStringLiteral("Mouse Move");
    case ActionType::MouseClick: return clicks >= 2 ? b + QStringLiteral(" Double-Click") : b + QStringLiteral(" Click");
    case ActionType::MouseDown: return b + QStringLiteral(" Button Down");
    case ActionType::MouseUp: return b + QStringLiteral(" Button Up");
    case ActionType::Wheel:
        if (horizontal) return notches > 0 ? QStringLiteral("Scroll Right") : QStringLiteral("Scroll Left");
        return notches > 0 ? QStringLiteral("Scroll Up") : QStringLiteral("Scroll Down");
    case ActionType::KeyPress: return QStringLiteral("Key Press");
    case ActionType::KeyDown: return QStringLiteral("Key Down");
    case ActionType::KeyUp: return QStringLiteral("Key Up");
    case ActionType::Text: return QStringLiteral("Text");
    case ActionType::Wait: return QStringLiteral("Wait");
    case ActionType::WindowFocus: return QStringLiteral("Window Focus");
    case ActionType::WaitPixel: return QStringLiteral("Wait for Pixel Color");
    case ActionType::WaitKey: return QStringLiteral("Wait for Key");
    case ActionType::Repeat: return QStringLiteral("Repeat");
    case ActionType::Goto: return QStringLiteral("Go To");
    case ActionType::RunProgram: return QStringLiteral("Run Program");
    case ActionType::Notify: return QStringLiteral("Notification");
    case ActionType::WaitFile: return QStringLiteral("Wait for File");
    case ActionType::MessageBox: return QStringLiteral("Message Box");
    case ActionType::Beep: return QStringLiteral("Beep");
    case ActionType::RunMacro: return QStringLiteral("Run Macro");
    case ActionType::SetVariable: return QStringLiteral("Set Variable");
    case ActionType::If: return QStringLiteral("If");
    case ActionType::Else: return QStringLiteral("Else");
    case ActionType::EndIf: return QStringLiteral("End If");
    case ActionType::AskText: return QStringLiteral("Ask for Text");
    case ActionType::Group: return QStringLiteral("Group");
    case ActionType::EndGroup: return QStringLiteral("End Group");
    case ActionType::WindowMove: return QStringLiteral("Move / Resize Window");
    case ActionType::FindImage:
        if (waitGone) return QStringLiteral("Wait for Image to Go");
        switch (findDo) {
        case FindDo::Click: return QStringLiteral("Click Image");
        case FindDo::DoubleClick: return QStringLiteral("Double-Click Image");
        case FindDo::RightClick: return QStringLiteral("Right-Click Image");
        case FindDo::Move: return QStringLiteral("Point at Image");
        case FindDo::Nothing: break;
        }
        return QStringLiteral("Find Image");
    case ActionType::Screenshot: return QStringLiteral("Save Screenshot");
    case ActionType::SetClipboard: return QStringLiteral("Set Clipboard");
    case ActionType::AskAI: return QStringLiteral("Ask AI");
    case ActionType::FindText:
        if (text.isEmpty()) return QStringLiteral("Read Text");
        if (waitGone) return QStringLiteral("Wait for Text to Go");
        switch (findDo) {
        case FindDo::Click: return QStringLiteral("Click Text");
        case FindDo::DoubleClick: return QStringLiteral("Double-Click Text");
        case FindDo::RightClick: return QStringLiteral("Right-Click Text");
        case FindDo::Move: return QStringLiteral("Point at Text");
        case FindDo::Nothing: break;
        }
        return ms > 0 ? QStringLiteral("Wait for Text") : QStringLiteral("Find Text");
    }
    return {};
}

QString Action::valueText() const
{
    const QString at = QStringLiteral("%1, %2 px").arg(pos.x()).arg(pos.y())
                       + (relativeToWindow ? QStringLiteral(" (in window)") : QString());
    switch (type) {
    case ActionType::MouseMove:
        return QStringLiteral("%1, %2 → %3").arg(from.x()).arg(from.y()).arg(at)
               + (ms > 0 ? QStringLiteral(" · ") + msText(ms) : QString());
    case ActionType::MouseClick: return image.isEmpty() ? at : at + QStringLiteral(" · finds its picture");
    case ActionType::MouseDown:
    case ActionType::MouseUp: return at;
    case ActionType::Wheel: {
        const int n = qAbs(notches);
        return QStringLiteral("%1 notch%2 at %3").arg(n).arg(n == 1 ? QString() : QStringLiteral("es")).arg(at);
    }
    case ActionType::KeyPress:
    case ActionType::KeyDown:
    case ActionType::KeyUp: return combo(mods, key);
    case ActionType::Text: return text;
    case ActionType::Wait:
        return maxMs > ms ? QStringLiteral("%1 to %2 (random)").arg(msText(ms), msText(maxMs)) : msText(ms);
    case ActionType::WindowFocus: {
        QString s = exe.isEmpty() ? QStringLiteral("any program") : exe;
        if (titleMatch != TitleMatch::Any && !title.isEmpty()) s += QStringLiteral(" · ") + title;
        return s;
    }
    case ActionType::WaitPixel:
        return QStringLiteral("%1 at %2, %3 px").arg(color).arg(pos.x()).arg(pos.y())
               + (ms > 0 ? QStringLiteral(" · give up after ") + msText(ms) : QString());
    case ActionType::WaitKey: return combo(mods, key);
    case ActionType::Repeat: {
        QString how;
        switch (repeatMode) {
        case RepeatMode::Times: how = QStringLiteral("%1 times").arg(num(count)); break;
        case RepeatMode::Seconds: how = QStringLiteral("for %1 s").arg(num(count)); break;
        case RepeatMode::Forever: how = QStringLiteral("until stopped"); break;
        }
        return QStringLiteral("from “%1” %2").arg(target, how);
    }
    case ActionType::Goto:
        return QStringLiteral("“%1”").arg(target) + (expr.trimmed().isEmpty() ? QString() : QStringLiteral(" if ") + expr.trimmed());
    case ActionType::RunProgram: return arguments.isEmpty() ? program : program + QLatin1Char(' ') + arguments;
    case ActionType::Notify:
    case ActionType::MessageBox: return message;
    case ActionType::WaitFile: {
        static const char* const kWhen[] = {"exists", "is gone", "changes"};
        return QStringLiteral("%1 %2").arg(file, QLatin1String(kWhen[int(fileWhen)]))
               + (ms > 0 ? QStringLiteral(" · give up after ") + msText(ms) : QString());
    }
    case ActionType::Beep: return {};
    case ActionType::RunMacro: return file;
    case ActionType::SetVariable: return QStringLiteral("%1 = %2").arg(var, expr);
    case ActionType::If: return expr;
    case ActionType::Else:
    case ActionType::EndIf:
    case ActionType::Group:   // the list adds the group's name and size
    case ActionType::EndGroup: return {};
    case ActionType::AskText: return QStringLiteral("“%1” → %2").arg(message, var);
    case ActionType::WindowMove: {
        QString s = exe.isEmpty() ? QStringLiteral("active window") : exe;
        if (titleMatch != TitleMatch::Any && !title.isEmpty()) s += QStringLiteral(" · ") + title;
        switch (windowState) {
        case WindowState::Normal: return s + QStringLiteral(" → ") + rectText(rect);
        case WindowState::Maximized: return s + QStringLiteral(" → maximize");
        case WindowState::Minimized: return s + QStringLiteral(" → minimize");
        case WindowState::Restore: return s + QStringLiteral(" → restore");
        }
        return s;
    }
    case ActionType::FindImage: {
        QString s = area.isEmpty() ? QStringLiteral("anywhere on screen") : QStringLiteral("in ") + rectText(area);
        if (ms > 0) s += QStringLiteral(" · wait up to ") + msText(ms);
        if (!offset.isNull()) s += QStringLiteral(" · offset %1, %2 px").arg(offset.x()).arg(offset.y());
        return s;
    }
    case ActionType::Screenshot: return file + (area.isEmpty() ? QString() : QStringLiteral(" · ") + rectText(area));
    case ActionType::SetClipboard: return text;
    case ActionType::AskAI: {
        QString p = message.simplified();
        if (p.size() > 80) p = p.left(79) + QStringLiteral("…");
        return QStringLiteral("“%1” → %2").arg(p, var);
    }
    case ActionType::FindText: {
        const QString where = area.isEmpty() ? QStringLiteral("anywhere on screen") : QStringLiteral("in ") + rectText(area);
        QString s = text.isEmpty() ? where : QStringLiteral("“%1” %2").arg(text, where);
        if (ms > 0) s += QStringLiteral(" · wait up to ") + msText(ms);
        if (!var.isEmpty()) s += QStringLiteral(" → ") + var;
        return s;
    }
    }
    return {};
}

int Action::durationMs() const
{
    switch (type) {
    case ActionType::MouseMove: return ms;
    case ActionType::Wait: return maxMs > ms ? ms + (maxMs - ms) / 2 : ms;   // a random wait: its average
    case ActionType::MouseClick: return holdMs * clicks + (clicks > 1 ? gapMs * (clicks - 1) : 0);
    case ActionType::KeyPress: return holdMs;
    case ActionType::Wheel: return gapMs * qMax(0, qAbs(notches) - 1);
    case ActionType::Text: return strokes.isEmpty() ? 0 : strokes.last().t;
    default: return 0;
    }
}

QJsonObject Action::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = typeId(type);
    if (!enabled) o[QStringLiteral("enabled")] = false;
    if (!label.isEmpty()) o[QStringLiteral("label")] = label;
    if (!comment.isEmpty()) o[QStringLiteral("comment")] = comment;
    if (delayMs > 0) o[QStringLiteral("delay_ms")] = delayMs;
    if (holdMs > 0) o[QStringLiteral("hold_ms")] = holdMs;
    if (gapMs > 0) o[QStringLiteral("gap_ms")] = gapMs;

    auto point = [](QPoint p) { return QJsonArray{p.x(), p.y()}; };
    switch (type) {
    case ActionType::MouseMove: {
        o[QStringLiteral("from")] = point(from);
        o[QStringLiteral("to")] = point(pos);
        o[QStringLiteral("ms")] = ms;
        QJsonArray pts;  // [x, y, t] triples keep files compact
        for (const PathPoint& p : path) pts.append(QJsonArray{p.x, p.y, p.t});
        if (!pts.isEmpty()) o[QStringLiteral("path")] = pts;
        break;
    }
    case ActionType::MouseClick:
    case ActionType::MouseDown:
    case ActionType::MouseUp:
        o[QStringLiteral("at")] = point(pos);
        o[QStringLiteral("button")] = QString::fromLatin1(kButtons[int(button)]);
        if (type == ActionType::MouseClick && clicks != 1) o[QStringLiteral("clicks")] = clicks;
        if (type == ActionType::MouseClick && !image.isEmpty()) {   // a smart click: what was clicked
            o[QStringLiteral("image")] = QString::fromLatin1(image.toBase64());
            if (!offset.isNull()) o[QStringLiteral("offset")] = point(offset);
        }
        break;
    case ActionType::Wheel:
        o[QStringLiteral("at")] = point(pos);
        o[QStringLiteral("notches")] = notches;
        if (horizontal) o[QStringLiteral("horizontal")] = true;
        break;
    case ActionType::KeyPress:
    case ActionType::KeyDown:
    case ActionType::KeyUp:
    case ActionType::WaitKey:
        o[QStringLiteral("key")] = key;
        if (!mods.isEmpty()) o[QStringLiteral("mods")] = QJsonArray::fromStringList(mods);
        if (type == ActionType::WaitKey && ms > 0) o[QStringLiteral("timeout_ms")] = ms;
        break;
    case ActionType::Text: {
        o[QStringLiteral("text")] = text;
        QJsonArray st;
        for (const Stroke& s : strokes) st.append(s.t);
        if (!st.isEmpty()) o[QStringLiteral("stroke_ms")] = st;
        break;
    }
    case ActionType::Wait:
        o[QStringLiteral("ms")] = ms;
        if (maxMs > ms) o[QStringLiteral("max_ms")] = maxMs;
        break;
    case ActionType::WindowFocus:
        o[QStringLiteral("exe")] = exe;
        o[QStringLiteral("title")] = title;
        o[QStringLiteral("title_match")] = QString::fromLatin1(kMatches[int(titleMatch)]);
        o[QStringLiteral("timeout_ms")] = ms;
        break;
    case ActionType::WaitPixel:
        o[QStringLiteral("at")] = point(pos);
        o[QStringLiteral("color")] = color;
        o[QStringLiteral("tolerance")] = tolerance;
        o[QStringLiteral("timeout_ms")] = ms;
        break;
    case ActionType::Repeat:
        o[QStringLiteral("target")] = target;
        o[QStringLiteral("mode")] = QString::fromLatin1(kRepeat[int(repeatMode)]);
        o[QStringLiteral("count")] = count;
        break;
    case ActionType::Goto:
        o[QStringLiteral("target")] = target;
        if (!expr.trimmed().isEmpty()) o[QStringLiteral("condition")] = expr;
        break;
    case ActionType::RunProgram:
        o[QStringLiteral("program")] = program;
        if (!arguments.isEmpty()) o[QStringLiteral("arguments")] = arguments;
        break;
    case ActionType::Notify:
    case ActionType::MessageBox: o[QStringLiteral("message")] = message; break;
    case ActionType::WaitFile:
        o[QStringLiteral("file")] = file;
        o[QStringLiteral("when")] = QString::fromLatin1(kFileWhen[int(fileWhen)]);
        o[QStringLiteral("timeout_ms")] = ms;
        break;
    case ActionType::Beep: break;
    case ActionType::RunMacro: o[QStringLiteral("file")] = file; break;
    case ActionType::SetVariable:
        o[QStringLiteral("variable")] = var;
        o[QStringLiteral("value")] = expr;
        break;
    case ActionType::If: o[QStringLiteral("condition")] = expr; break;
    case ActionType::Else:
    case ActionType::EndIf:
    case ActionType::EndGroup: break;
    case ActionType::AskText:
        o[QStringLiteral("variable")] = var;
        o[QStringLiteral("message")] = message;
        if (!text.isEmpty()) o[QStringLiteral("text")] = text;
        break;
    case ActionType::Group:
        if (collapsed) o[QStringLiteral("collapsed")] = true;
        break;
    case ActionType::WindowMove:
        o[QStringLiteral("exe")] = exe;
        o[QStringLiteral("title")] = title;
        o[QStringLiteral("title_match")] = QString::fromLatin1(kMatches[int(titleMatch)]);
        o[QStringLiteral("state")] = QString::fromLatin1(kWindowStates[int(windowState)]);
        if (windowState == WindowState::Normal) o[QStringLiteral("rect")] = rectJson(rect);
        o[QStringLiteral("timeout_ms")] = ms;
        break;
    case ActionType::FindImage:
        o[QStringLiteral("image")] = QString::fromLatin1(image.toBase64());
        if (!area.isEmpty()) o[QStringLiteral("area")] = rectJson(area);
        o[QStringLiteral("tolerance")] = tolerance;
        o[QStringLiteral("timeout_ms")] = ms;
        o[QStringLiteral("do")] = QString::fromLatin1(kFindDo[int(findDo)]);
        if (!offset.isNull()) o[QStringLiteral("offset")] = point(offset);
        if (waitGone) o[QStringLiteral("wait_gone")] = true;
        if (stopIfMissing) o[QStringLiteral("stop_if_missing")] = true;
        break;
    case ActionType::Screenshot:
        o[QStringLiteral("file")] = file;
        if (!area.isEmpty()) o[QStringLiteral("area")] = rectJson(area);
        break;
    case ActionType::SetClipboard: o[QStringLiteral("text")] = text; break;
    case ActionType::AskAI:
        o[QStringLiteral("message")] = message;
        o[QStringLiteral("variable")] = var;
        o[QStringLiteral("timeout_ms")] = ms;
        break;
    case ActionType::FindText:
        if (!text.isEmpty()) o[QStringLiteral("text")] = text;
        if (!var.isEmpty()) o[QStringLiteral("variable")] = var;
        if (!area.isEmpty()) o[QStringLiteral("area")] = rectJson(area);
        o[QStringLiteral("timeout_ms")] = ms;
        o[QStringLiteral("do")] = QString::fromLatin1(kFindDo[int(findDo)]);
        if (!offset.isNull()) o[QStringLiteral("offset")] = point(offset);
        if (waitGone) o[QStringLiteral("wait_gone")] = true;
        if (stopIfMissing) o[QStringLiteral("stop_if_missing")] = true;
        break;
    }
    if (relativeToWindow) o[QStringLiteral("relative_to_window")] = true;
    return o;
}

Action Action::fromJson(const QJsonObject& o, bool* ok)
{
    Action a;
    const bool known = typeFromId(o.value(QStringLiteral("type")).toString(), &a.type);
    if (ok) *ok = known;
    auto point = [&](const char* k) {
        const QJsonArray p = o.value(QLatin1String(k)).toArray();
        return QPoint(p.at(0).toInt(), p.at(1).toInt());
    };
    a.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    a.label = o.value(QStringLiteral("label")).toString();
    a.comment = o.value(QStringLiteral("comment")).toString();
    a.delayMs = qMax(0, o.value(QStringLiteral("delay_ms")).toInt());
    a.holdMs = qBound(0, o.value(QStringLiteral("hold_ms")).toInt(), 60000);
    a.gapMs = qBound(0, o.value(QStringLiteral("gap_ms")).toInt(), 60000);
    a.relativeToWindow = o.value(QStringLiteral("relative_to_window")).toBool();
    a.pos = point(a.type == ActionType::MouseMove ? "to" : "at");
    a.from = point("from");
    a.ms = qMax(0, o.value(QStringLiteral("ms")).toInt(o.value(QStringLiteral("timeout_ms")).toInt()));
    for (const QJsonValue& v : o.value(QStringLiteral("path")).toArray()) {
        const QJsonArray p = v.toArray();
        a.path.append({p.at(0).toInt(), p.at(1).toInt(), p.at(2).toInt()});
    }
    a.button = enumFrom(o.value(QStringLiteral("button")).toString(), kButtons, MouseButton::Left);
    a.clicks = qBound(1, o.value(QStringLiteral("clicks")).toInt(1), 3);
    a.notches = o.value(QStringLiteral("notches")).toInt();
    a.horizontal = o.value(QStringLiteral("horizontal")).toBool();
    a.key = o.value(QStringLiteral("key")).toString();
    for (const QJsonValue& m : o.value(QStringLiteral("mods")).toArray()) a.mods << m.toString();
    a.text = o.value(QStringLiteral("text")).toString();
    const QJsonArray st = o.value(QStringLiteral("stroke_ms")).toArray();
    // Strokes pair up with the text's characters (surrogate pairs count as one character).
    QStringList chars;
    for (int i = 0; i < a.text.size(); ++i) {
        if (a.text.at(i).isHighSurrogate() && i + 1 < a.text.size()) { chars << a.text.mid(i, 2); ++i; }
        else chars << a.text.mid(i, 1);
    }
    if (st.size() == chars.size())
        for (int i = 0; i < chars.size(); ++i) a.strokes.append({chars.at(i), st.at(i).toInt()});
    a.exe = o.value(QStringLiteral("exe")).toString();
    a.title = o.value(QStringLiteral("title")).toString();
    a.titleMatch = enumFrom(o.value(QStringLiteral("title_match")).toString(), kMatches, TitleMatch::Exact);
    a.color = o.value(QStringLiteral("color")).toString();
    a.tolerance = qBound(0, o.value(QStringLiteral("tolerance")).toInt(), 100);
    a.target = o.value(QStringLiteral("target")).toString();
    a.repeatMode = enumFrom(o.value(QStringLiteral("mode")).toString(), kRepeat, RepeatMode::Times);
    a.count = qMax(1, o.value(QStringLiteral("count")).toInt(1));
    a.program = o.value(QStringLiteral("program")).toString();
    a.arguments = o.value(QStringLiteral("arguments")).toString();
    a.message = o.value(QStringLiteral("message")).toString();
    a.file = o.value(QStringLiteral("file")).toString();
    a.fileWhen = enumFrom(o.value(QStringLiteral("when")).toString(), kFileWhen, FileCondition::Exists);
    a.var = o.value(QStringLiteral("variable")).toString();
    a.expr = o.value(a.type == ActionType::SetVariable ? QStringLiteral("value") : QStringLiteral("condition")).toString();
    a.maxMs = qMax(0, o.value(QStringLiteral("max_ms")).toInt());
    a.collapsed = o.value(QStringLiteral("collapsed")).toBool();
    a.rect = rectFrom(o.value(QStringLiteral("rect")));
    a.windowState = enumFrom(o.value(QStringLiteral("state")).toString(), kWindowStates, WindowState::Normal);
    if (o.contains(QStringLiteral("image"))) a.image = QByteArray::fromBase64(o.value(QStringLiteral("image")).toString().toLatin1());
    a.area = rectFrom(o.value(QStringLiteral("area")));
    a.findDo = enumFrom(o.value(QStringLiteral("do")).toString(), kFindDo, FindDo::Nothing);
    if (o.contains(QStringLiteral("offset"))) a.offset = point("offset");
    a.waitGone = o.value(QStringLiteral("wait_gone")).toBool();
    a.stopIfMissing = o.value(QStringLiteral("stop_if_missing")).toBool();
    return a;
}

int Macro::totalMs() const
{
    qint64 total = 0;
    for (const Action& a : actions)
        if (a.enabled) total += a.delayMs + a.durationMs();
    return int(qMin<qint64>(total, INT_MAX));
}

static const char kFormat[] = "macro-recorder/macro";
static const int kVersion = 1;

bool Macro::save(const QString& path, QString* error) const
{
    QJsonArray list;
    for (const Action& a : actions) list.append(a.toJson());
    QJsonObject root{{QStringLiteral("format"), QString::fromLatin1(kFormat)},
                     {QStringLiteral("version"), kVersion},
                     {QStringLiteral("actions"), list}};
    QSaveFile f(path);  // writes a temporary file and renames it: never a half-written macro
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool Macro::load(const QString& path, Macro* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    const QJsonObject root = doc.object();
    if (pe.error != QJsonParseError::NoError || root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)) {
        if (error) *error = QStringLiteral("This file isn't a macro saved by this app.");
        return false;
    }
    if (root.value(QStringLiteral("version")).toInt() > kVersion) {
        if (error) *error = QStringLiteral("This macro was saved by a newer version of the app. Please update.");
        return false;
    }
    Macro m;
    int skipped = 0;
    for (const QJsonValue& v : root.value(QStringLiteral("actions")).toArray()) {
        bool ok = false;
        Action a = Action::fromJson(v.toObject(), &ok);
        if (ok) m.actions.append(a); else ++skipped;
    }
    if (skipped && error) *error = QStringLiteral("%1 unknown action(s) were skipped.").arg(skipped);
    *out = m;
    return true;
}

}  // namespace core
