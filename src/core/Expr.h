// Variables and expressions in macros: Set Variable, If, Go To's condition, and {name} / {=expression} in the text
// of other steps. Numbers and text only; no locale (a dot is the decimal mark, as in the macro files).
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

namespace core {

struct Value {
    enum Kind { Number, Text } kind = Text;
    double num = 0;
    QString str;

    static Value number(double d) { Value v; v.kind = Number; v.num = d; return v; }
    static Value text(const QString& s) { Value v; v.kind = Text; v.str = s; return v; }
    bool toNumber(double* out) const;   // a number, or text that reads as one ("12", " 3.5 ")
    QString toText() const;              // numbers without trailing zeros: 3, 2.5, -0.25
    bool truthy() const;                 // 0, "", "0" and "false" are false
};

struct Scope {
    QHash<QString, Value> vars;                        // names in lower case
    std::function<QString()> clipboard;                // optional: the clipboard's text
    std::function<qint64(qint64 lo, qint64 hi)> random;   // optional (tests): a whole number from lo to hi
};

namespace expr {

bool isName(const QString& s);   // letters, digits and _, not starting with a digit, and not a word of the language
// Evaluates `src`. False (with a sentence in `error`) on a syntax error, an unknown variable or a bad argument.
bool evaluate(const QString& src, Scope& scope, Value* out, QString* error);
// Syntax only: functions and argument counts are checked, variables aren't (they may be set while playing).
bool check(const QString& src, QString* error);
// Replaces {name} (a variable that exists; anything else in braces is left alone) and {=expression}.
QString substitute(const QString& text, Scope& scope, QStringList* errors = nullptr);
bool hasPlaceholders(const QString& text);   // contains "{=" or "{name}"
QStringList functionHelp();                  // one line per function, for the step editor's help

}  // namespace expr
}  // namespace core
