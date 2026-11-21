#include "ActionDialog.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

using core::Action;
using core::ActionType;

namespace {

const QStringList kModNames = {QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("Shift"), QStringLiteral("Win")};

QString describe(ActionType t)
{
    switch (t) {
    case ActionType::MouseMove: return QStringLiteral("Moves the mouse from one point to another.");
    case ActionType::MouseClick: return QStringLiteral("Clicks a mouse button at a screen position.");
    case ActionType::MouseDown: return QStringLiteral("Presses and holds a mouse button (for dragging). Add a “button up” later.");
    case ActionType::MouseUp: return QStringLiteral("Releases a held mouse button.");
    case ActionType::Wheel: return QStringLiteral("Scrolls the mouse wheel at a screen position.");
    case ActionType::KeyPress: return QStringLiteral("Presses and releases a key, with any modifier keys held.");
    case ActionType::KeyDown: return QStringLiteral("Presses and holds a key. Add a “key up” later.");
    case ActionType::KeyUp: return QStringLiteral("Releases a held key.");
    case ActionType::Text: return QStringLiteral("Types text into the active window.");
    case ActionType::Wait: return QStringLiteral("Pauses before the next step.");
    case ActionType::WindowFocus: return QStringLiteral("Brings a program's window to the front, waiting for it to appear. The program must already be running.");
    case ActionType::WaitPixel: return QStringLiteral("Waits until a point on the screen turns a color (for example, a button that lights up).");
    case ActionType::WaitKey: return QStringLiteral("Waits until you press a key, then continues.");
    case ActionType::Repeat: return QStringLiteral("Jumps back to a labeled step to repeat the steps in between.");
    case ActionType::Goto: return QStringLiteral("Jumps to a labeled step.");
    case ActionType::RunProgram: return QStringLiteral("Starts a program or opens a file.");
    case ActionType::Notify: return QStringLiteral("Shows a notification.");
    case ActionType::WaitFile: return QStringLiteral("Waits until a file appears, disappears or changes (for example, a download or an export finishing).");
    case ActionType::MessageBox: return QStringLiteral("Shows a message and waits until you press OK.");
    case ActionType::Beep: return QStringLiteral("Plays the system sound.");
    case ActionType::RunMacro: return QStringLiteral("Plays another macro file at this point, then carries on with this one.");
    }
    return {};
}

QWidget* hrow(std::initializer_list<QWidget*> ws)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    for (QWidget* x : ws) h->addWidget(x);
    h->addStretch();
    return w;
}

}  // namespace

QStringList ActionDialog::keyNames()
{
    QStringList k;
    for (char c = 'A'; c <= 'Z'; ++c) k << QString(QChar(c));
    for (char c = '0'; c <= '9'; ++c) k << QString(QChar(c));
    for (int i = 1; i <= 24; ++i) k << QStringLiteral("F%1").arg(i);
    k << QStringLiteral("Enter") << QStringLiteral("Escape") << QStringLiteral("Tab") << QStringLiteral("Space")
      << QStringLiteral("Backspace") << QStringLiteral("Delete") << QStringLiteral("Insert") << QStringLiteral("Home")
      << QStringLiteral("End") << QStringLiteral("PageUp") << QStringLiteral("PageDown") << QStringLiteral("Left")
      << QStringLiteral("Right") << QStringLiteral("Up") << QStringLiteral("Down") << QStringLiteral("CapsLock")
      << QStringLiteral("PrintScreen") << QStringLiteral("Pause") << QStringLiteral("Menu")
      << QStringLiteral("Shift") << QStringLiteral("Ctrl") << QStringLiteral("Alt") << QStringLiteral("Win")
      << QStringLiteral("Minus") << QStringLiteral("Equals") << QStringLiteral("LeftBracket") << QStringLiteral("RightBracket")
      << QStringLiteral("Semicolon") << QStringLiteral("Quote") << QStringLiteral("Comma") << QStringLiteral("Period")
      << QStringLiteral("Slash") << QStringLiteral("Backslash") << QStringLiteral("Backquote");
    for (int i = 0; i <= 9; ++i) k << QStringLiteral("Num%1").arg(i);
    k << QStringLiteral("NumAdd") << QStringLiteral("NumSubtract") << QStringLiteral("NumMultiply") << QStringLiteral("NumDivide")
      << QStringLiteral("NumDecimal") << QStringLiteral("VolumeUp") << QStringLiteral("VolumeDown") << QStringLiteral("VolumeMute")
      << QStringLiteral("MediaPlayPause") << QStringLiteral("MediaNext") << QStringLiteral("MediaPrevious");
    return k;
}

ActionDialog::ActionDialog(const Action& a, const QStringList& labels, core::Injector* inj, QWidget* parent)
    : QDialog(parent), m_a(a), m_labels(labels), m_inj(inj)
{
    setWindowTitle(a.typeName());
    build();
}

QSpinBox* ActionDialog::spin(int min, int max, int value, const QString& suffix)
{
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(value);
    if (!suffix.isEmpty()) s->setSuffix(QLatin1Char(' ') + suffix);
    s->setGroupSeparatorShown(true);
    s->setMinimumWidth(110);
    return s;
}

void ActionDialog::build()
{
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel(describe(m_a.type));
    intro->setWordWrap(true);
    lay->addWidget(intro);
    m_form = new QFormLayout;
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    lay->addLayout(m_form);
    const ActionType t = m_a.type;

    const bool hasPos = t == ActionType::MouseMove || t == ActionType::MouseClick || t == ActionType::MouseDown
                        || t == ActionType::MouseUp || t == ActionType::Wheel || t == ActionType::WaitPixel;
    if (t == ActionType::MouseMove) {
        m_fx = spin(-100000, 100000, m_a.from.x(), QStringLiteral("px"));
        m_fy = spin(-100000, 100000, m_a.from.y(), QStringLiteral("px"));
        m_form->addRow(QStringLiteral("From X, Y:"), hrow({m_fx, m_fy}));
    }
    if (hasPos) {
        m_x = spin(-100000, 100000, m_a.pos.x(), QStringLiteral("px"));
        m_y = spin(-100000, 100000, m_a.pos.y(), QStringLiteral("px"));
        auto* pick = new QPushButton(t == ActionType::WaitPixel ? QStringLiteral("Pick point and color…") : QStringLiteral("Use mouse position…"));
        pick->setToolTip(QStringLiteral("After 3 seconds, takes the mouse pointer's position%1. Point at the spot meanwhile. "
                                        "Or point at it and press the Capture position hotkey (F8 unless you changed it).")
                             .arg(t == ActionType::WaitPixel ? QStringLiteral(" and the color under it") : QString()));
        connect(pick, &QPushButton::clicked, this, [this, t] { pickPosition(t == ActionType::WaitPixel); });
        m_form->addRow(t == ActionType::MouseMove ? QStringLiteral("To X, Y:") : QStringLiteral("X, Y:"), hrow({m_x, m_y, pick}));
        m_rel = new QCheckBox(QStringLiteral("Relative to the active window's top-left corner"));
        m_rel->setChecked(m_a.relativeToWindow);
        m_form->addRow(QString(), m_rel);
    }
    if (t == ActionType::MouseClick || t == ActionType::MouseDown || t == ActionType::MouseUp) {
        m_button = new QComboBox;
        for (auto b : {core::MouseButton::Left, core::MouseButton::Right, core::MouseButton::Middle, core::MouseButton::X1, core::MouseButton::X2})
            m_button->addItem(core::buttonName(b));
        m_button->setCurrentIndex(int(m_a.button));
        m_form->addRow(QStringLiteral("Button:"), m_button);
    }
    if (t == ActionType::MouseClick) {
        m_clicks = new QComboBox;
        m_clicks->addItems({QStringLiteral("Single click"), QStringLiteral("Double-click")});
        m_clicks->setCurrentIndex(m_a.clicks >= 2 ? 1 : 0);
        m_form->addRow(QStringLiteral("Click:"), m_clicks);
    }
    if (t == ActionType::Wheel) {
        m_notches = spin(-100, 100, m_a.notches == 0 ? -1 : m_a.notches, QStringLiteral("notches"));
        m_notches->setToolTip(QStringLiteral("Positive scrolls up (or right), negative scrolls down (or left)."));
        m_horiz = new QCheckBox(QStringLiteral("Horizontal (left / right)"));
        m_horiz->setChecked(m_a.horizontal);
        m_form->addRow(QStringLiteral("Scroll:"), hrow({m_notches, m_horiz}));
    }
    if (t == ActionType::MouseMove) {
        m_ms = spin(0, 600000, m_a.ms, QStringLiteral("ms"));
        m_ms->setToolTip(QStringLiteral("0 = a natural speed for the distance."));
        m_form->addRow(QStringLiteral("Duration:"), m_ms);
    }
    if (t == ActionType::KeyPress || t == ActionType::KeyDown || t == ActionType::KeyUp || t == ActionType::WaitKey) {
        m_key = new QComboBox;
        m_key->setEditable(true);
        m_key->addItems(keyNames());
        m_key->setCurrentText(m_a.key.isEmpty() ? QStringLiteral("Enter") : m_a.key);
        m_form->addRow(QStringLiteral("Key:"), m_key);
        if (t != ActionType::KeyUp) {
            auto* w = new QWidget;
            auto* h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            for (const QString& m : kModNames) {
                auto* c = new QCheckBox(m);
                c->setChecked(m_a.mods.contains(m));
                m_mods << c;
                h->addWidget(c);
            }
            h->addStretch();
            m_form->addRow(QStringLiteral("Held with:"), w);
        }
    }
    if (t == ActionType::Text) {
        m_text = new QPlainTextEdit(m_a.text);
        m_text->setTabChangesFocus(true);
        m_text->setMinimumHeight(80);
        m_form->addRow(QStringLiteral("Text:"), m_text);
        if (!m_a.strokes.isEmpty()) {
            auto* note = new QLabel(QStringLiteral("Changing the text drops the recorded typing rhythm; it's then typed at an even pace."));
            note->setObjectName(QStringLiteral("muted"));
            note->setWordWrap(true);
            m_form->addRow(QString(), note);
        }
    }
    if (t == ActionType::Wait) {
        m_ms = spin(0, 86400000, m_a.ms, QStringLiteral("ms"));
        m_form->addRow(QStringLiteral("Wait:"), m_ms);
    }
    if (t == ActionType::WindowFocus) {
        m_exe = new QLineEdit(m_a.exe);
        m_exe->setPlaceholderText(QStringLiteral("e.g. notepad.exe (empty = any program)"));
        m_title = new QLineEdit(m_a.title);
        m_title->setMinimumWidth(260);
        m_match = new QComboBox;
        m_match->addItems({QStringLiteral("Is exactly"), QStringLiteral("Starts with"), QStringLiteral("Contains"), QStringLiteral("Any title")});
        m_match->setCurrentIndex(int(m_a.titleMatch));
        m_form->addRow(QStringLiteral("Program:"), m_exe);
        m_form->addRow(QStringLiteral("Window title:"), hrow({m_match, m_title}));
    }
    if (t == ActionType::WaitPixel) {
        m_color = new QLineEdit(m_a.color.isEmpty() ? QStringLiteral("#FFFFFF") : m_a.color);
        m_color->setPlaceholderText(QStringLiteral("#RRGGBB"));
        m_tol = spin(0, 100, m_a.tolerance, QStringLiteral("%"));
        m_form->addRow(QStringLiteral("Color:"), hrow({m_color, new QLabel(QStringLiteral("Tolerance:")), m_tol}));
    }
    if (t == ActionType::WindowFocus || t == ActionType::WaitPixel || t == ActionType::WaitKey) {
        m_ms = spin(0, 86400, m_a.ms / 1000, QStringLiteral("s"));
        m_ms->setToolTip(t == ActionType::WindowFocus ? QStringLiteral("Playback stops if the window doesn't appear in time.")
                                                      : QStringLiteral("0 = wait as long as it takes. On timeout, playback continues."));
        m_form->addRow(QStringLiteral("Give up after:"), m_ms);
    }
    if (t == ActionType::Repeat || t == ActionType::Goto) {
        m_target = new QComboBox;
        m_target->setEditable(true);
        m_target->addItems(m_labels);
        m_target->setCurrentText(m_a.target);
        m_form->addRow(QStringLiteral("Jump to label:"), m_target);
    }
    if (t == ActionType::Repeat) {
        m_mode = new QComboBox;
        m_mode->addItems({QStringLiteral("times in total"), QStringLiteral("seconds"), QStringLiteral("until stopped")});
        m_mode->setCurrentIndex(int(m_a.repeatMode));
        m_count = spin(1, 1000000, m_a.count);
        m_form->addRow(QStringLiteral("Repeat:"), hrow({m_count, m_mode}));
        connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int i) { m_count->setEnabled(i != 2); });
        m_count->setEnabled(m_mode->currentIndex() != 2);
    }
    if (t == ActionType::RunProgram) {
        m_program = new QLineEdit(m_a.program);
        auto* browse = new QPushButton(QStringLiteral("Browse…"));
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a program or file"), m_program->text());
            if (!f.isEmpty()) m_program->setText(QDir::toNativeSeparators(f));
        });
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_program, 1);
        h->addWidget(browse);
        m_args = new QLineEdit(m_a.arguments);
        m_form->addRow(QStringLiteral("Program:"), w);
        m_form->addRow(QStringLiteral("Arguments:"), m_args);
    }
    if (t == ActionType::Notify || t == ActionType::MessageBox) {
        m_message = new QLineEdit(m_a.message);
        m_form->addRow(QStringLiteral("Message:"), m_message);
    }
    if (t == ActionType::WaitFile || t == ActionType::RunMacro) {
        m_file = new QLineEdit(QDir::toNativeSeparators(m_a.file));
        m_file->setMinimumWidth(260);
        auto* browse = new QPushButton(QStringLiteral("Browse…"));
        connect(browse, &QPushButton::clicked, this, [this, t] {
            const QString f = t == ActionType::RunMacro
                                  ? QFileDialog::getOpenFileName(this, QStringLiteral("Choose a macro"), m_file->text(), QStringLiteral("Macros (*.macro)"))
                                  : QFileDialog::getSaveFileName(this, QStringLiteral("Choose the file to watch"), m_file->text(), QString(), nullptr,
                                                                 QFileDialog::DontConfirmOverwrite);
            if (!f.isEmpty()) m_file->setText(QDir::toNativeSeparators(f));
        });
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_file, 1);
        h->addWidget(browse);
        m_form->addRow(t == ActionType::RunMacro ? QStringLiteral("Macro file:") : QStringLiteral("File:"), w);
    }
    if (t == ActionType::WaitFile) {
        m_fileWhen = new QComboBox;
        m_fileWhen->addItems({QStringLiteral("Exists"), QStringLiteral("Is gone"), QStringLiteral("Changes")});
        m_fileWhen->setCurrentIndex(int(m_a.fileWhen));
        m_form->addRow(QStringLiteral("Continue when it:"), m_fileWhen);
        m_ms = spin(0, 86400, m_a.ms / 1000, QStringLiteral("s"));
        m_ms->setToolTip(QStringLiteral("0 = wait as long as it takes. On timeout, playback continues."));
        m_form->addRow(QStringLiteral("Give up after:"), m_ms);
    }

    // Fields every action has
    m_label = new QLineEdit(m_a.label);
    m_label->setPlaceholderText(QStringLiteral("optional: a name Repeat / Go to can jump to"));
    m_comment = new QLineEdit(m_a.comment);
    m_delay = spin(0, 60000, m_a.delayMs, QStringLiteral("ms"));
    m_delay->setToolTip(QStringLiteral("A short pause before this step, kept from the recording (used when timing is exact)."));
    m_enabled = new QCheckBox(QStringLiteral("Enabled (unticked steps are skipped when playing)"));
    m_enabled->setChecked(m_a.enabled);
    m_form->addRow(QStringLiteral("Label:"), m_label);
    m_form->addRow(QStringLiteral("Comment:"), m_comment);
    m_form->addRow(QStringLiteral("Pause before:"), m_delay);
    m_form->addRow(QString(), m_enabled);

    m_error = new QLabel;
    m_error->setProperty("tone", QStringLiteral("err"));
    m_error->setWordWrap(true);
    m_error->hide();
    lay->addWidget(m_error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ActionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    setMinimumWidth(520);
}

void ActionDialog::captureNow()
{
    if (!m_x || !m_inj) return;
    const QPoint p = m_inj->cursor();
    m_x->setValue(p.x());
    m_y->setValue(p.y());
    if (m_color) {
        const QString c = m_inj->pixelColor(p);
        if (!c.isEmpty()) m_color->setText(c);
    }
}

void ActionDialog::pickPosition(bool withColor)
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn || !m_inj) return;
    const QString original = btn->text();
    btn->setEnabled(false);
    auto left = std::make_shared<int>(3);
    auto* timer = new QTimer(this);
    btn->setText(QStringLiteral("Point at it… 3"));
    connect(timer, &QTimer::timeout, this, [=, this] {
        if (--*left > 0) { btn->setText(QStringLiteral("Point at it… %1").arg(*left)); return; }
        timer->stop();
        timer->deleteLater();
        const QPoint p = m_inj->cursor();
        m_x->setValue(p.x());
        m_y->setValue(p.y());
        if (withColor && m_color) {
            const QString c = m_inj->pixelColor(p);
            if (!c.isEmpty()) m_color->setText(c);
        }
        btn->setText(original);
        btn->setEnabled(true);
    });
    timer->start(1000);
}

void ActionDialog::accept()
{
    auto fail = [this](const QString& msg) {
        m_error->setText(msg);
        m_error->show();
    };
    Action a = m_a;
    const ActionType t = a.type;
    if (m_x) a.pos = QPoint(m_x->value(), m_y->value());
    if (m_fx) a.from = QPoint(m_fx->value(), m_fy->value());
    if (m_rel) a.relativeToWindow = m_rel->isChecked();
    if (m_button) a.button = core::MouseButton(m_button->currentIndex());
    if (m_clicks) a.clicks = m_clicks->currentIndex() == 1 ? 2 : 1;
    if (m_notches) {
        if (m_notches->value() == 0) return fail(QStringLiteral("Scroll at least one notch (use a negative number to scroll down)."));
        a.notches = m_notches->value();
        a.horizontal = m_horiz->isChecked();
    }
    if (m_key) {
        const QString k = m_key->currentText().trimmed();
        bool known = k.startsWith(QLatin1String("VK_0x"));
        if (known) a.key = k;
        for (const QString& n : keyNames())
            if (n.compare(k, Qt::CaseInsensitive) == 0) { a.key = n; known = true; }
        if (!known) return fail(QStringLiteral("“%1” isn't a key name this app knows. Pick one from the list.").arg(k));
        a.mods.clear();
        for (QCheckBox* c : std::as_const(m_mods))
            if (c->isChecked() && c->text() != a.key) a.mods << c->text();
    }
    if (m_text) {
        const QString s = m_text->toPlainText();
        if (s.isEmpty()) return fail(QStringLiteral("Enter the text to type."));
        if (s != a.text) a.strokes.clear();
        a.text = s;
    }
    if (m_ms) {
        const bool seconds = t == ActionType::WindowFocus || t == ActionType::WaitPixel || t == ActionType::WaitKey || t == ActionType::WaitFile;
        a.ms = seconds ? m_ms->value() * 1000 : m_ms->value();
        if (t == ActionType::MouseMove && a.ms != m_a.ms) a.path.clear();  // a new duration can't keep the recorded path
    }
    if (t == ActionType::MouseMove && (a.pos != m_a.pos || a.from != m_a.from)) a.path.clear();
    if (m_exe) {
        a.exe = m_exe->text().trimmed();
        a.title = m_title->text();
        a.titleMatch = core::TitleMatch(m_match->currentIndex());
        if (a.exe.isEmpty() && (a.titleMatch == core::TitleMatch::Any || a.title.isEmpty()))
            return fail(QStringLiteral("Enter a program name, a window title, or both."));
    }
    if (m_color) {
        const QColor c(m_color->text().trimmed());
        if (!c.isValid()) return fail(QStringLiteral("Enter the color as #RRGGBB, for example #FF8800."));
        a.color = c.name(QColor::HexRgb).toUpper();
        a.tolerance = m_tol->value();
    }
    if (m_target) {
        a.target = m_target->currentText().trimmed();
        if (a.target.isEmpty()) return fail(QStringLiteral("Choose the label to jump to. Give a step a label first (Label column)."));
        if (!m_labels.contains(a.target) && a.target != m_label->text().trimmed())
            return fail(QStringLiteral("No step has the label “%1” yet. Give the step you want to jump to that label first.").arg(a.target));
    }
    if (m_mode) {
        a.repeatMode = core::RepeatMode(m_mode->currentIndex());
        a.count = m_count->value();
    }
    if (m_program) {
        a.program = m_program->text().trimmed();
        a.arguments = m_args->text().trimmed();
        if (a.program.isEmpty()) return fail(QStringLiteral("Choose the program or file to open."));
    }
    if (m_message) {
        a.message = m_message->text().trimmed();
        if (a.message.isEmpty()) return fail(QStringLiteral("Enter the message to show."));
    }
    if (m_file) {
        a.file = QDir::fromNativeSeparators(m_file->text().trimmed());
        if (a.file.isEmpty()) return fail(t == ActionType::RunMacro ? QStringLiteral("Choose the macro file to play.") : QStringLiteral("Enter the file to watch."));
        if (t == ActionType::RunMacro && !QFileInfo::exists(a.file)) return fail(QStringLiteral("“%1” doesn't exist.").arg(m_file->text().trimmed()));
    }
    if (m_fileWhen) a.fileWhen = core::FileCondition(m_fileWhen->currentIndex());
    a.label = m_label->text().trimmed();
    a.comment = m_comment->text().trimmed();
    a.delayMs = m_delay->value();
    a.enabled = m_enabled->isChecked();
    m_a = a;
    QDialog::accept();
}
