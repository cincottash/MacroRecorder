#include "Expr.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <cmath>

namespace core {

bool Value::toNumber(double* out) const
{
    if (kind == Number) { *out = num; return true; }
    const QString t = str.trimmed();
    if (t.isEmpty()) return false;
    bool ok = false;
    const double d = t.toDouble(&ok);   // C locale: a dot is the decimal mark
    if (ok && std::isfinite(d)) { *out = d; return true; }
    return false;
}

QString Value::toText() const
{
    if (kind == Text) return str;
    if (!std::isfinite(num)) return QStringLiteral("0");
    if (num == std::floor(num) && std::fabs(num) < 1e15) return QString::number(qint64(num));
    QString s = QString::number(num, 'f', 10);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

bool Value::truthy() const
{
    if (kind == Number) return num != 0;
    const QString t = str.trimmed();
    return !(t.isEmpty() || t == QLatin1String("0") || t.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0);
}

namespace expr {

namespace {

const QStringList kWords = {QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("not"), QStringLiteral("true"),
                            QStringLiteral("false")};

struct Token {
    enum Kind { End, Num, Str, Name, Op, LParen, RParen, Comma } kind = End;
    QString text;
    double num = 0;
    int pos = 0;
};

struct Fn {
    const char* name;
    int minArgs, maxArgs;   // maxArgs -1: any number
    const char* help;
};

const Fn kFunctions[] = {
    {"random", 1, 2, "random(1, 10): a whole number from 1 to 10 (random(6) is 1 to 6)"},
    {"pick", 1, 2, "pick(\"red, green, blue\"): one item of a list, chosen at random"},
    {"item", 2, 3, "item(\"red, green, blue\", 2): the 2nd item (green); after the last it starts over"},
    {"count", 1, 2, "count(\"red, green, blue\"): how many items a list has (3)"},
    {"line", 2, 2, "line(\"C:\\names.txt\", 3): the 3rd line of a text file; after the last it starts over"},
    {"lines", 1, 1, "lines(\"C:\\names.txt\"): how many lines a text file has"},
    {"round", 1, 2, "round(2.567, 1): 2.6 (no second number: a whole number)"},
    {"floor", 1, 1, "floor(2.7): 2"},
    {"ceil", 1, 1, "ceil(2.1): 3"},
    {"abs", 1, 1, "abs(-5): 5"},
    {"min", 1, -1, "min(4, 9, 2): 2"},
    {"max", 1, -1, "max(4, 9, 2): 9"},
    {"len", 1, 1, "len(\"hello\"): 5"},
    {"upper", 1, 1, "upper(\"hello\"): HELLO"},
    {"lower", 1, 1, "lower(\"HELLO\"): hello"},
    {"trim", 1, 1, "trim(\"  hi  \"): hi"},
    {"left", 2, 2, "left(\"hello\", 2): he"},
    {"right", 2, 2, "right(\"hello\", 2): lo"},
    {"mid", 2, 3, "mid(\"hello\", 2, 3): ell (from the 2nd character, 3 characters)"},
    {"find", 2, 2, "find(\"hello\", \"l\"): 3 (where it first appears; 0 = not found)"},
    {"contains", 2, 2, "contains(\"hello\", \"ell\"): true"},
    {"replace", 3, 3, "replace(\"a-b-c\", \"-\", \"+\"): a+b+c"},
    {"number", 1, 1, "number(\"12\"): 12 as a number"},
    {"text", 1, 1, "text(12): 12 as text"},
    {"if", 3, 3, "if(count > 3, \"many\", \"few\"): the 2nd value when true, else the 3rd"},
    {"exists", 1, 1, "exists(\"C:\\report.pdf\"): true when the file or folder is there"},
    {"now", 0, 1, "now(\"yyyy-MM-dd HH:mm\"): the date and time in that form"},
    {"clipboard", 0, 0, "clipboard(): the text on the clipboard"},
};

const Fn* findFn(const QString& name)
{
    for (const Fn& f : kFunctions)
        if (name.compare(QLatin1String(f.name), Qt::CaseInsensitive) == 0) return &f;
    return nullptr;
}

class Parser {
public:
    Parser(const QString& src, Scope* scope) : m_src(src), m_scope(scope) {}

    bool run(Value* out, QString* error)
    {
        if (m_src.trimmed().isEmpty()) return fail(QStringLiteral("The expression is empty."), error);
        if (!tokenize()) return fail(m_error, error);
        Value v;
        if (!parseOr(&v, true)) return fail(m_error, error);
        if (cur().kind != Token::End) return fail(QStringLiteral("Unexpected “%1”.").arg(cur().text), error);
        if (out) *out = v;
        return true;
    }

private:
    bool fail(const QString& msg, QString* error)
    {
        if (error) *error = msg;
        return false;
    }
    bool err(const QString& msg)
    {
        if (m_error.isEmpty()) m_error = msg;
        return false;
    }
    const Token& cur() const { return m_tokens.at(m_i); }
    bool isOp(const char* op) const { return cur().kind == Token::Op && cur().text == QLatin1String(op); }
    bool isWord(const char* w) const { return cur().kind == Token::Name && cur().text.compare(QLatin1String(w), Qt::CaseInsensitive) == 0; }

    bool tokenize()
    {
        int i = 0;
        const int n = int(m_src.size());
        while (i < n) {
            const QChar c = m_src.at(i);
            if (c.isSpace()) { ++i; continue; }
            Token t;
            t.pos = i;
            if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && m_src.at(i + 1).isDigit())) {
                int j = i;
                while (j < n && (m_src.at(j).isDigit() || m_src.at(j) == QLatin1Char('.'))) ++j;
                t.kind = Token::Num;
                t.text = m_src.mid(i, j - i);
                bool ok = false;
                t.num = t.text.toDouble(&ok);
                if (!ok) return err(QStringLiteral("“%1” isn't a number.").arg(t.text));
                i = j;
            } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                int j = i + 1;
                QString s;
                bool closed = false;
                while (j < n) {
                    if (m_src.at(j) == c) {
                        if (j + 1 < n && m_src.at(j + 1) == c) { s += c; j += 2; continue; }   // "" inside "...": one quote
                        closed = true;
                        break;
                    }
                    s += m_src.at(j++);
                }
                if (!closed) return err(QStringLiteral("A text in quotes isn't closed: add the closing %1.").arg(c));
                t.kind = Token::Str;
                t.text = s;
                i = j + 1;
            } else if (c.isLetter() || c == QLatin1Char('_')) {
                int j = i;
                while (j < n && (m_src.at(j).isLetterOrNumber() || m_src.at(j) == QLatin1Char('_'))) ++j;
                t.kind = Token::Name;
                t.text = m_src.mid(i, j - i);
                i = j;
            } else if (c == QLatin1Char('(')) { t.kind = Token::LParen; t.text = c; ++i; }
            else if (c == QLatin1Char(')')) { t.kind = Token::RParen; t.text = c; ++i; }
            else if (c == QLatin1Char(',')) { t.kind = Token::Comma; t.text = c; ++i; }
            else {
                static const char* const kOps[] = {"==", "!=", "<>", "<=", ">=", "&&", "||", "+", "-", "*", "/", "%", "&", "=", "<", ">", "!"};
                bool found = false;
                for (const char* op : kOps) {
                    const QLatin1String o(op);
                    if (m_src.mid(i, o.size()) == o) {
                        t.kind = Token::Op;
                        t.text = o;
                        i += int(o.size());
                        found = true;
                        break;
                    }
                }
                if (!found) return err(QStringLiteral("“%1” can't be used in an expression.").arg(c));
            }
            m_tokens << t;
        }
        Token end;
        end.pos = n;
        end.text = QStringLiteral("the end");
        m_tokens << end;
        return true;
    }

    // Every parse function evaluates only when `exec` is true (false: checking the syntax, or the branch not taken).
    bool parseOr(Value* out, bool exec)
    {
        if (!parseAnd(out, exec)) return false;
        while (isOp("||") || isWord("or")) {
            ++m_i;
            const bool left = exec && out->truthy();
            Value r;
            if (!parseAnd(&r, exec && !left)) return false;
            if (exec) *out = Value::number(left || r.truthy() ? 1 : 0);
        }
        return true;
    }

    bool parseAnd(Value* out, bool exec)
    {
        if (!parseNot(out, exec)) return false;
        while (isOp("&&") || isWord("and")) {
            ++m_i;
            const bool left = exec && out->truthy();
            Value r;
            if (!parseNot(&r, exec && left)) return false;
            if (exec) *out = Value::number(left && r.truthy() ? 1 : 0);
        }
        return true;
    }

    bool parseNot(Value* out, bool exec)
    {
        if (isOp("!") || isWord("not")) {
            ++m_i;
            Value v;
            if (!parseNot(&v, exec)) return false;
            if (exec) *out = Value::number(v.truthy() ? 0 : 1);
            return true;
        }
        return parseCompare(out, exec);
    }

    bool parseCompare(Value* out, bool exec)
    {
        if (!parseAdd(out, exec)) return false;
        static const char* const kCmp[] = {"==", "!=", "<>", "<=", ">=", "=", "<", ">"};
        for (const char* op : kCmp) {
            if (!isOp(op)) continue;
            ++m_i;
            Value r;
            if (!parseAdd(&r, exec)) return false;
            if (!exec) return true;
            double a = 0, b = 0;
            int c;
            if (out->toNumber(&a) && r.toNumber(&b)) c = a < b ? -1 : a > b ? 1 : 0;
            else c = QString::compare(out->toText(), r.toText(), Qt::CaseInsensitive);   // text: case doesn't matter
            const QLatin1String o(op);
            bool res;
            if (o == QLatin1String("=") || o == QLatin1String("==")) res = c == 0;
            else if (o == QLatin1String("!=") || o == QLatin1String("<>")) res = c != 0;
            else if (o == QLatin1String("<")) res = c < 0;
            else if (o == QLatin1String("<=")) res = c <= 0;
            else if (o == QLatin1String(">")) res = c > 0;
            else res = c >= 0;
            *out = Value::number(res ? 1 : 0);
            return true;
        }
        return true;
    }

    bool parseAdd(Value* out, bool exec)
    {
        if (!parseMul(out, exec)) return false;
        while (isOp("+") || isOp("-") || isOp("&")) {
            const QString op = cur().text;
            ++m_i;
            Value r;
            if (!parseMul(&r, exec)) return false;
            if (!exec) continue;
            double a = 0, b = 0;
            const bool nums = out->toNumber(&a) && r.toNumber(&b);
            if (op == QLatin1String("&") || (op == QLatin1String("+") && !nums)) {
                *out = Value::text(out->toText() + r.toText());   // + joins text when either side isn't a number
            } else if (!nums) {
                return err(QStringLiteral("“-” needs two numbers (“%1” and “%2”).").arg(out->toText(), r.toText()));
            } else {
                *out = Value::number(op == QLatin1String("+") ? a + b : a - b);
            }
        }
        return true;
    }

    bool parseMul(Value* out, bool exec)
    {
        if (!parseUnary(out, exec)) return false;
        while (isOp("*") || isOp("/") || isOp("%")) {
            const QString op = cur().text;
            ++m_i;
            Value r;
            if (!parseUnary(&r, exec)) return false;
            if (!exec) continue;
            double a = 0, b = 0;
            if (!out->toNumber(&a) || !r.toNumber(&b))
                return err(QStringLiteral("“%1” needs two numbers (“%2” and “%3”).").arg(op, out->toText(), r.toText()));
            if ((op == QLatin1String("/") || op == QLatin1String("%")) && b == 0) return err(QStringLiteral("Division by zero."));
            if (op == QLatin1String("*")) *out = Value::number(a * b);
            else if (op == QLatin1String("/")) *out = Value::number(a / b);
            else *out = Value::number(std::fmod(a, b));
        }
        return true;
    }

    bool parseUnary(Value* out, bool exec)
    {
        if (isOp("-") || isOp("+")) {
            const bool neg = isOp("-");
            ++m_i;
            Value v;
            if (!parseUnary(&v, exec)) return false;
            if (!exec) return true;
            double d = 0;
            if (!v.toNumber(&d)) return err(QStringLiteral("“-” needs a number, not “%1”.").arg(v.toText()));
            *out = Value::number(neg ? -d : d);
            return true;
        }
        return parsePrimary(out, exec);
    }

    bool parsePrimary(Value* out, bool exec)
    {
        const Token t = cur();
        switch (t.kind) {
        case Token::Num: ++m_i; if (exec) *out = Value::number(t.num); return true;
        case Token::Str: ++m_i; if (exec) *out = Value::text(t.text); return true;
        case Token::LParen: {
            ++m_i;
            if (!parseOr(out, exec)) return false;
            if (cur().kind != Token::RParen) return err(QStringLiteral("A “(” isn't closed: add a “)”."));
            ++m_i;
            return true;
        }
        case Token::Name: {
            ++m_i;
            const QString lower = t.text.toLower();
            if (lower == QLatin1String("true")) { if (exec) *out = Value::number(1); return true; }
            if (lower == QLatin1String("false")) { if (exec) *out = Value::number(0); return true; }
            if (kWords.contains(lower)) return err(QStringLiteral("“%1” is missing something before or after it.").arg(t.text));
            if (cur().kind == Token::LParen) return parseCall(t.text, out, exec);
            if (!exec) return true;
            if (!m_scope || !m_scope->vars.contains(lower))
                return err(QStringLiteral("No variable is named “%1” (set it with a Set Variable step first).").arg(t.text));
            *out = m_scope->vars.value(lower);
            return true;
        }
        case Token::End: return err(QStringLiteral("The expression ends too early."));
        default: return err(QStringLiteral("Unexpected “%1”.").arg(t.text));
        }
    }

    bool parseCall(const QString& name, Value* out, bool exec)
    {
        const Fn* fn = findFn(name);
        if (!fn) return err(QStringLiteral("There's no function named “%1”.").arg(name));
        ++m_i;   // (
        QList<Value> args;
        if (cur().kind != Token::RParen) {
            while (true) {
                Value v;
                if (!parseOr(&v, exec)) return false;
                args << v;
                if (cur().kind == Token::Comma) { ++m_i; continue; }
                break;
            }
        }
        if (cur().kind != Token::RParen) return err(QStringLiteral("%1( isn't closed: add a “)”.").arg(name));
        ++m_i;
        const int n = int(args.size());
        if (n < fn->minArgs || (fn->maxArgs >= 0 && n > fn->maxArgs)) {
            const QString want = fn->minArgs == fn->maxArgs ? QString::number(fn->minArgs)
                                 : fn->maxArgs < 0 ? QStringLiteral("%1 or more").arg(fn->minArgs)
                                                   : QStringLiteral("%1 to %2").arg(fn->minArgs).arg(fn->maxArgs);
            return err(QStringLiteral("%1() takes %2 value%3. Example: %4").arg(QLatin1String(fn->name), want,
                                                                               want == QLatin1String("1") ? QString() : QStringLiteral("s"),
                                                                               QString::fromUtf8(fn->help).section(QLatin1Char(':'), 0, 0)));
        }
        if (!exec) return true;
        return call(QLatin1String(fn->name), args, out);
    }

    bool numArg(const QList<Value>& args, int i, const QString& fn, double* out)
    {
        if (args.at(i).toNumber(out)) return true;
        return err(QStringLiteral("%1() needs a number, not “%2”.").arg(fn, args.at(i).toText()));
    }
    bool intArg(const QList<Value>& args, int i, const QString& fn, qint64* out)
    {
        double d = 0;
        if (!numArg(args, i, fn, &d)) return false;
        if (std::fabs(d) > 1e15) return err(QStringLiteral("%1(): %2 is too large.").arg(fn, args.at(i).toText()));
        *out = qint64(std::llround(d));
        return true;
    }

    qint64 randomBetween(qint64 lo, qint64 hi)
    {
        if (m_scope && m_scope->random) return m_scope->random(lo, hi);
        return lo + qint64(QRandomGenerator::global()->bounded(quint64(hi - lo) + 1));
    }

    static QStringList splitList(const QString& list, const QString& sep)
    {
        QStringList out;
        for (const QString& s : list.split(sep.isEmpty() ? QStringLiteral(",") : sep)) out << s.trimmed();
        if (out.size() == 1 && out.first().isEmpty()) out.clear();
        return out;
    }

    static int wrapIndex(qint64 n, int size) { return int(((n - 1) % size + size) % size); }

    bool readLines(const QString& fn, const QString& path, QStringList* out)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return err(QStringLiteral("%1(): can't read “%2”.").arg(fn, path));
        if (f.size() > 50 * 1024 * 1024) return err(QStringLiteral("%1(): “%2” is too large (over 50 MB).").arg(fn, path));
        QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        for (QString& l : lines) if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
        if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();   // the file's last line break
        *out = lines;
        return true;
    }

    bool call(const QString& f, const QList<Value>& a, Value* out)
    {
        auto txt = [&](int i) { return a.at(i).toText(); };
        if (f == QLatin1String("random")) {
            qint64 lo = 1, hi = 0;
            if (a.size() == 1) { if (!intArg(a, 0, f, &hi)) return false; }
            else if (!intArg(a, 0, f, &lo) || !intArg(a, 1, f, &hi)) return false;
            if (hi < lo) std::swap(lo, hi);
            *out = Value::number(double(randomBetween(lo, hi)));
            return true;
        }
        if (f == QLatin1String("pick") || f == QLatin1String("item") || f == QLatin1String("count")) {
            const int sepArg = f == QLatin1String("item") ? 2 : 1;
            const QStringList items = splitList(txt(0), a.size() > sepArg ? txt(sepArg) : QString());
            if (f == QLatin1String("count")) { *out = Value::number(items.size()); return true; }
            if (items.isEmpty()) return err(QStringLiteral("%1(): the list is empty.").arg(f));
            qint64 n = 1;
            if (f == QLatin1String("pick")) n = randomBetween(1, items.size());
            else if (!intArg(a, 1, f, &n)) return false;
            if (n < 1) return err(QStringLiteral("item() counts from 1, not %1.").arg(n));
            *out = Value::text(items.at(wrapIndex(n, int(items.size()))));
            return true;
        }
        if (f == QLatin1String("line") || f == QLatin1String("lines")) {
            QStringList lines;
            if (!readLines(f, txt(0), &lines)) return false;
            if (f == QLatin1String("lines")) { *out = Value::number(lines.size()); return true; }
            qint64 n = 1;
            if (!intArg(a, 1, f, &n)) return false;
            if (lines.isEmpty()) return err(QStringLiteral("line(): “%1” is empty.").arg(txt(0)));
            if (n < 1) return err(QStringLiteral("line() counts from 1, not %1.").arg(n));
            *out = Value::text(lines.at(wrapIndex(n, int(lines.size()))));
            return true;
        }
        if (f == QLatin1String("round")) {
            double d = 0;
            qint64 digits = 0;
            if (!numArg(a, 0, f, &d) || (a.size() > 1 && !intArg(a, 1, f, &digits))) return false;
            digits = qBound(qint64(0), digits, qint64(10));
            const double p = std::pow(10.0, double(digits));
            *out = Value::number(std::round(d * p) / p);
            return true;
        }
        if (f == QLatin1String("floor") || f == QLatin1String("ceil") || f == QLatin1String("abs")) {
            double d = 0;
            if (!numArg(a, 0, f, &d)) return false;
            *out = Value::number(f == QLatin1String("floor") ? std::floor(d) : f == QLatin1String("ceil") ? std::ceil(d) : std::fabs(d));
            return true;
        }
        if (f == QLatin1String("min") || f == QLatin1String("max")) {
            double best = 0;
            for (int i = 0; i < a.size(); ++i) {
                double d = 0;
                if (!numArg(a, i, f, &d)) return false;
                if (i == 0 || (f == QLatin1String("min") ? d < best : d > best)) best = d;
            }
            *out = Value::number(best);
            return true;
        }
        if (f == QLatin1String("len")) { *out = Value::number(txt(0).size()); return true; }
        if (f == QLatin1String("upper")) { *out = Value::text(txt(0).toUpper()); return true; }
        if (f == QLatin1String("lower")) { *out = Value::text(txt(0).toLower()); return true; }
        if (f == QLatin1String("trim")) { *out = Value::text(txt(0).trimmed()); return true; }
        if (f == QLatin1String("left") || f == QLatin1String("right")) {
            qint64 n = 0;
            if (!intArg(a, 1, f, &n)) return false;
            n = qBound(qint64(0), n, qint64(txt(0).size()));
            *out = Value::text(f == QLatin1String("left") ? txt(0).left(int(n)) : txt(0).right(int(n)));
            return true;
        }
        if (f == QLatin1String("mid")) {
            qint64 start = 1, n = -1;
            if (!intArg(a, 1, f, &start) || (a.size() > 2 && !intArg(a, 2, f, &n))) return false;
            if (start < 1) return err(QStringLiteral("mid() counts from 1, not %1.").arg(start));
            *out = Value::text(txt(0).mid(int(qMin<qint64>(start - 1, INT_MAX)), n < 0 ? -1 : int(qMin<qint64>(n, INT_MAX))));
            return true;
        }
        if (f == QLatin1String("find")) { *out = Value::number(txt(0).indexOf(txt(1), 0, Qt::CaseInsensitive) + 1); return true; }
        if (f == QLatin1String("contains")) { *out = Value::number(txt(0).contains(txt(1), Qt::CaseInsensitive) ? 1 : 0); return true; }
        if (f == QLatin1String("replace")) {
            QString s = txt(0);
            if (!txt(1).isEmpty()) s.replace(txt(1), txt(2));
            *out = Value::text(s);
            return true;
        }
        if (f == QLatin1String("number")) {
            double d = 0;
            if (!numArg(a, 0, f, &d)) return false;
            *out = Value::number(d);
            return true;
        }
        if (f == QLatin1String("text")) { *out = Value::text(txt(0)); return true; }
        if (f == QLatin1String("if")) { *out = a.at(0).truthy() ? a.at(1) : a.at(2); return true; }
        if (f == QLatin1String("exists")) { *out = Value::number(!txt(0).isEmpty() && QFileInfo::exists(txt(0)) ? 1 : 0); return true; }
        if (f == QLatin1String("now")) {
            const QString fmt = a.isEmpty() ? QStringLiteral("yyyy-MM-dd HH:mm:ss") : txt(0);
            *out = Value::text(QDateTime::currentDateTime().toString(fmt));
            return true;
        }
        if (f == QLatin1String("clipboard")) {
            *out = Value::text(m_scope && m_scope->clipboard ? m_scope->clipboard() : QString());
            return true;
        }
        return err(QStringLiteral("There's no function named “%1”.").arg(f));
    }

    QString m_src;
    Scope* m_scope;
    QList<Token> m_tokens;
    int m_i = 0;
    QString m_error;
};

// The end of a {=expression} starting at `from` (just after "{="): the first "}" outside quotes; -1 if none.
int closingBrace(const QString& s, int from)
{
    QChar quote;
    for (int i = from; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (!quote.isNull()) { if (c == quote) quote = QChar(); continue; }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) quote = c;
        else if (c == QLatin1Char('}')) return i;
    }
    return -1;
}

}  // namespace

bool isName(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    // A function's name can be a variable too ("count"): only a "(" after it makes it a call.
    return re.match(s).hasMatch() && !kWords.contains(s.toLower());
}

bool evaluate(const QString& src, Scope& scope, Value* out, QString* error)
{
    Parser p(src, &scope);
    return p.run(out, error);
}

bool check(const QString& src, QString* error)
{
    // Evaluated with a scope in which every name is a variable holding 1: only the syntax can fail.
    Value dummy;
    Scope any;
    static const QRegularExpression names(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
    for (auto it = names.globalMatch(src); it.hasNext();) {
        const QString n = it.next().captured().toLower();
        if (!kWords.contains(n)) any.vars.insert(n, Value::number(1));
    }
    any.clipboard = [] { return QString(); };
    any.random = [](qint64 lo, qint64) { return lo; };
    Parser checker(src, &any);
    QString e;
    if (checker.run(&dummy, &e)) return true;
    // Errors that come from the values rather than the syntax (a file that can't be read, a division by zero with
    // the stand-in values, "needs a number") aren't syntax errors: they're reported when the step plays.
    static const QStringList syntax = {QStringLiteral("isn't closed"), QStringLiteral("Unexpected"), QStringLiteral("ends too early"),
                                       QStringLiteral("can't be used"), QStringLiteral("no function"), QStringLiteral("takes"),
                                       QStringLiteral("is empty."), QStringLiteral("is missing something"), QStringLiteral("isn't a number")};
    for (const QString& s : syntax)
        if (e.contains(s)) {
            if (error) *error = e;
            return false;
        }
    return true;
}

QString substitute(const QString& text, Scope& scope, QStringList* errors)
{
    if (!text.contains(QLatin1Char('{'))) return text;
    QString out;
    int i = 0;
    while (i < text.size()) {
        const int open = text.indexOf(QLatin1Char('{'), i);
        if (open < 0) { out += text.mid(i); break; }
        out += text.mid(i, open - i);
        if (open + 1 < text.size() && text.at(open + 1) == QLatin1Char('=')) {
            const int close = closingBrace(text, open + 2);
            if (close < 0) { out += text.mid(open); break; }
            Value v;
            QString e;
            if (evaluate(text.mid(open + 2, close - open - 2), scope, &v, &e)) out += v.toText();
            else {
                out += text.mid(open, close - open + 1);
                if (errors) *errors << e;
            }
            i = close + 1;
            continue;
        }
        const int close = text.indexOf(QLatin1Char('}'), open + 1);
        const QString name = close > 0 ? text.mid(open + 1, close - open - 1) : QString();
        if (close > 0 && isName(name) && scope.vars.contains(name.toLower())) {
            out += scope.vars.value(name.toLower()).toText();
            i = close + 1;
        } else {
            out += QLatin1Char('{');
            i = open + 1;
        }
    }
    return out;
}

bool hasPlaceholders(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("\\{(=|[A-Za-z_][A-Za-z0-9_]*\\})"));
    return re.match(text).hasMatch();
}

QStringList functionHelp()
{
    QStringList l;
    for (const Fn& f : kFunctions) l << QString::fromUtf8(f.help);
    return l;
}

}  // namespace expr
}  // namespace core
