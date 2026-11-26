#include "ActionDialog.h"
#include "FileDialogs.h"
#include "ScreenPicker.h"
#include "core/Expr.h"
#include "core/Tr.h"

#include <QBuffer>
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
#include <QMessageBox>
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
    case ActionType::MouseMove: return T("Moves the mouse from one point to another.");
    case ActionType::MouseClick: return T("Clicks a mouse button at a screen position.");
    case ActionType::MouseDown: return T("Presses and holds a mouse button (for dragging). Add a “button up” later.");
    case ActionType::MouseUp: return T("Releases a held mouse button.");
    case ActionType::Wheel: return T("Scrolls the mouse wheel at a screen position.");
    case ActionType::KeyPress: return T("Presses and releases a key, with any modifier keys held.");
    case ActionType::KeyDown: return T("Presses and holds a key. Add a “key up” later.");
    case ActionType::KeyUp: return T("Releases a held key.");
    case ActionType::Text: return T("Types text into the active window.");
    case ActionType::Wait: return T("Pauses before the next step.");
    case ActionType::WindowFocus: return T("Brings a program's window to the front, waiting for it to appear. The program must already be running.");
    case ActionType::WaitPixel: return T("Waits until a point on the screen turns a color (for example, a button that lights up).");
    case ActionType::WaitKey: return T("Waits until you press a key, then continues.");
    case ActionType::Repeat: return T("Jumps back to a labeled step to repeat the steps in between.");
    case ActionType::Goto: return T("Jumps to a labeled step.");
    case ActionType::RunProgram: return T("Starts a program or opens a file.");
    case ActionType::Notify: return T("Shows a notification.");
    case ActionType::WaitFile: return T("Waits until a file appears, disappears or changes (for example, a download or an export finishing).");
    case ActionType::MessageBox: return T("Shows a message and waits until you press OK.");
    case ActionType::Beep: return T("Plays the system sound.");
    case ActionType::RunMacro: return T("Plays another macro file at this point, then carries on with this one.");
    case ActionType::SetVariable: return T("Gives a variable a value: a number, a text, or a calculation. Other steps can use it.");
    case ActionType::If: return T("The steps after it, up to Else (or End If), play only when the condition is true. "
                                               "The steps between Else and End If play when it's false.");
    case ActionType::Else: return T("Starts the steps that play when the If's condition is false.");
    case ActionType::EndIf: return T("Ends an If.");
    case ActionType::AskText: return T("Asks a question while playing and keeps the answer in a variable. "
                                                    "Cancel stops playback.");
    case ActionType::Group: return T("Starts a named group of steps, up to End Group. A group can be folded away in the "
                                                  "list, and a disabled group skips all its steps.");
    case ActionType::EndGroup: return T("Ends a group.");
    case ActionType::WindowMove: return T("Moves and resizes a program's window, or maximizes, minimizes or restores it. "
                                                       "The program must already be running.");
    case ActionType::FindImage: return T("Looks for a picture on the screen (a button, an icon), and can wait for it "
                                                      "and click it wherever it is.");
    case ActionType::Screenshot: return T("Saves a picture of the screen, or a part of it, as a PNG file.");
    case ActionType::SetClipboard: return T("Puts text on the clipboard, ready to paste (Ctrl+V).");
    case ActionType::FindText: return T("Reads the text on the screen, or looks for a word or phrase there and can "
                                                     "wait for it and click it wherever it is.");
    case ActionType::AskAI: return T("Asks an AI model a question while playing and keeps the answer in a variable: "
                                                  "a reply to write, a summary, a decision for an If step.");
    }
    return {};
}

QLabel* note(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("muted"));
    l->setWordWrap(true);
    return l;
}

// A function, not a constant: the text follows the language chosen when the app starts
QString varTip() { return T("{name} puts in a variable's value, {=expression} a calculation."); }

QWidget* vrow(std::initializer_list<QWidget*> ws)
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    for (QWidget* x : ws) v->addWidget(x);
    v->addStretch();
    return w;
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

QSpinBox* ActionDialog::spin(int min, int max, int value, const QString& suffix, bool groupDigits)
{
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(value);
    if (!suffix.isEmpty()) s->setSuffix(QLatin1Char(' ') + suffix);
    s->setGroupSeparatorShown(groupDigits);   // positions are written plainly ("1480 px"), as in the list
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
    m_form->setHorizontalSpacing(12);   // room between each label and its field (it was 2 px under the style sheet)
    lay->addLayout(m_form);
    const ActionType t = m_a.type;

    const bool hasPos = t == ActionType::MouseMove || t == ActionType::MouseClick || t == ActionType::MouseDown
                        || t == ActionType::MouseUp || t == ActionType::Wheel || t == ActionType::WaitPixel;
    if (t == ActionType::MouseMove) {
        m_fx = spin(-100000, 100000, m_a.from.x(), QStringLiteral("px"), false);
        m_fy = spin(-100000, 100000, m_a.from.y(), QStringLiteral("px"), false);
        m_form->addRow(T("From X, Y:"), hrow({m_fx, m_fy}));
    }
    if (hasPos) {
        m_x = spin(-100000, 100000, m_a.pos.x(), QStringLiteral("px"), false);
        m_y = spin(-100000, 100000, m_a.pos.y(), QStringLiteral("px"), false);
        auto* pick = new QPushButton(t == ActionType::WaitPixel ? T("Pick Point and Color…") : T("Use Mouse Position…"));
        pick->setToolTip(t == ActionType::WaitPixel
                             ? T("After 3 seconds, takes the mouse pointer's position and the color under it. Point at the spot "
                                 "meanwhile. Or point at it and press the Capture Position hotkey (F8 unless you changed it).")
                             : T("After 3 seconds, takes the mouse pointer's position. Point at the spot meanwhile. "
                                 "Or point at it and press the Capture Position hotkey (F8 unless you changed it)."));
        connect(pick, &QPushButton::clicked, this, [this, t] { pickPosition(t == ActionType::WaitPixel); });
        m_form->addRow(t == ActionType::MouseMove ? T("To X, Y:") : T("X, Y:"), hrow({m_x, m_y, pick}));
        m_rel = new QCheckBox(T("Relative to Active Window"));
        m_rel->setChecked(m_a.relativeToWindow);
        m_form->addRow(QString(), m_rel);
    }
    if (t == ActionType::MouseClick || t == ActionType::MouseDown || t == ActionType::MouseUp) {
        m_button = new QComboBox;
        for (auto b : {core::MouseButton::Left, core::MouseButton::Right, core::MouseButton::Middle, core::MouseButton::X1, core::MouseButton::X2})
            m_button->addItem(core::buttonName(b));
        m_button->setCurrentIndex(int(m_a.button));
        m_form->addRow(T("Button:"), m_button);
    }
    if (t == ActionType::MouseClick) {
        m_clicks = new QComboBox;
        m_clicks->addItems({T("Single Click"), T("Double-Click")});
        m_clicks->setCurrentIndex(m_a.clicks >= 2 ? 1 : 0);
        m_form->addRow(T("Click:"), m_clicks);
        if (!m_a.image.isEmpty()) {   // recorded with Find Clicked Buttons by Picture
            auto* pic = new QLabel;
            const QImage img = QImage::fromData(m_a.image);
            pic->setPixmap(QPixmap::fromImage(img.scaled(img.size() * 2, Qt::KeepAspectRatio, Qt::FastTransformation)));
            pic->setFrameShape(QFrame::StyledPanel);
            m_usePicture = new QCheckBox(T("Find by Picture First"));
            m_usePicture->setChecked(true);
            m_usePicture->setToolTip(T("Looks for this picture and clicks it wherever it is now; if it isn't on the "
                                                    "screen, clicks the position above. Unticked: the picture is dropped."));
            m_form->addRow(T("Picture:"), hrow({pic, m_usePicture}));
        }
    }
    if (t == ActionType::Wheel) {
        m_notches = spin(-100, 100, m_a.notches == 0 ? -1 : m_a.notches, T("notches"));
        m_notches->setToolTip(T("Positive scrolls up (or right), negative scrolls down (or left)."));
        m_horiz = new QCheckBox(T("Horizontal (Left / Right)"));
        m_horiz->setChecked(m_a.horizontal);
        m_form->addRow(T("Scroll:"), hrow({m_notches, m_horiz}));
    }
    if (t == ActionType::MouseMove) {
        m_ms = spin(0, 600000, m_a.ms, QStringLiteral("ms"));
        m_ms->setToolTip(T("0 = a natural speed for the distance."));
        m_form->addRow(T("Duration:"), m_ms);
    }
    if (t == ActionType::KeyPress || t == ActionType::KeyDown || t == ActionType::KeyUp || t == ActionType::WaitKey) {
        m_key = new QComboBox;
        m_key->setEditable(true);
        m_key->addItems(keyNames());
        m_key->setCurrentText(m_a.key.isEmpty() ? QStringLiteral("Enter") : m_a.key);
        m_form->addRow(T("Key:"), m_key);
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
            m_form->addRow(T("Held With:"), w);
        }
    }
    if (t == ActionType::Text) {
        m_text = new QPlainTextEdit(m_a.text);
        m_text->setTabChangesFocus(true);
        m_text->setMinimumHeight(80);
        m_form->addRow(T("Text:"), m_text);
        m_form->addRow(QString(), note(m_a.strokes.isEmpty()
                                           ? varTip()
                                           : T("Changing the text drops the recorded typing rhythm; it's then typed at an "
                                               "even pace.") + QLatin1Char(' ') + varTip()));
    }
    if (t == ActionType::Wait) {
        m_ms = spin(0, 86400000, m_a.ms, QStringLiteral("ms"));
        m_form->addRow(T("Wait:"), m_ms);
        m_maxMs = spin(0, 86400000, m_a.maxMs > m_a.ms ? m_a.maxMs : 0, QStringLiteral("ms"));
        m_maxMs->setSpecialValueText(T("Off"));
        m_maxMs->setToolTip(T("Waits a random time from the first value to this one, different each time. "
                                           "Off: always the same wait."));
        m_form->addRow(T("Random Up To:"), m_maxMs);
    }
    if (t == ActionType::WindowFocus || t == ActionType::WindowMove) {
        m_exe = new QLineEdit(m_a.exe);
        m_exe->setPlaceholderText(T("e.g. notepad.exe (empty = any program)"));
        m_title = new QLineEdit(m_a.title);
        m_title->setMinimumWidth(260);
        m_match = new QComboBox;
        m_match->addItems({T("Is Exactly"), T("Starts With"), T("Contains"), T("Any Title")});
        m_match->setCurrentIndex(int(m_a.titleMatch));
        m_form->addRow(T("Program:"), m_exe);
        m_form->addRow(T("Window Title:"), hrow({m_match, m_title}));
        if (t == ActionType::WindowMove) {
            m_exe->setPlaceholderText(T("e.g. notepad.exe (empty = the active window)"));
            auto* pick = new QPushButton(T("Use Window Under Pointer…"));
            pick->setToolTip(T("After 3 seconds, takes the program, title, position and size of the window under "
                                            "the mouse pointer. Point at it meanwhile."));
            connect(pick, &QPushButton::clicked, this, [this] { pickWindow(); });
            m_form->addRow(QString(), hrow({pick}));
            m_state = new QComboBox;
            m_state->addItems({T("This Position and Size"), T("Maximized"), T("Minimized"), T("Restored (Not Maximized or Minimized)")});
            m_state->setCurrentIndex(int(m_a.windowState));
            m_form->addRow(T("Window State:"), m_state);
            const QRect r = m_a.rect.isEmpty() ? QRect(100, 100, 1000, 700) : m_a.rect;
            m_rx = spin(-100000, 100000, r.x(), QStringLiteral("px"), false);
            m_ry = spin(-100000, 100000, r.y(), QStringLiteral("px"), false);
            m_rw = spin(1, 100000, r.width(), QStringLiteral("px"), false);
            m_rh = spin(1, 100000, r.height(), QStringLiteral("px"), false);
            const int wide = m_rx->sizeHint().width();   // the four boxes line up in two equal columns
            for (QSpinBox* s : {m_rx, m_ry, m_rw, m_rh}) s->setFixedWidth(wide);
            m_form->addRow(T("X, Y:"), hrow({m_rx, m_ry}));
            m_form->addRow(T("Width, Height:"), hrow({m_rw, m_rh}));
            auto sync = [this] {
                const bool normal = m_state->currentIndex() == 0;
                for (QSpinBox* s : {m_rx, m_ry, m_rw, m_rh}) s->setEnabled(normal);
            };
            connect(m_state, &QComboBox::currentIndexChanged, this, sync);
            sync();
        }
    }
    if (t == ActionType::SetVariable || t == ActionType::If || t == ActionType::Goto) {
        m_expr = new QLineEdit(m_a.expr);
        m_expr->setMinimumWidth(300);
        auto* help = new QPushButton(T("Functions…"));
        help->setToolTip(T("What a value or condition can use."));
        connect(help, &QPushButton::clicked, this, [this] {
            QString html = T("<p><b>Values:</b> numbers (2.5), text in quotes (\"hello\"), variables (count), "
                             "true and false.</p><p><b>Calculations:</b> + − * / and % (remainder). "
                             "+ adds numbers and joins text; &amp; always joins.</p>"
                             "<p><b>Comparisons:</b> = (or ==), != (or &lt;&gt;), &lt;, &lt;=, &gt;, &gt;=. "
                             "Text is compared without regard to case. Combine with and, or, not.</p>"
                             "<p><b>Ready-made variables:</b> round (which time through the macro), found, found_x and "
                             "found_y (after a Find Image step).</p><p><b>Functions:</b></p>")
                           + QStringLiteral("<ul>");
            for (const QString& f : core::expr::functionHelp()) html += QStringLiteral("<li>%1</li>").arg(f.toHtmlEscaped());
            html += QStringLiteral("</ul>") + T("<p>In other steps' text, {name} puts in a variable's value and {=expression} "
                                                "a calculation.</p>");
            QMessageBox box(QMessageBox::Information, T("Values and Conditions"), html, QMessageBox::Ok, this);
            box.setTextFormat(Qt::RichText);
            box.exec();
        });
        if (t == ActionType::SetVariable) {
            m_var = new QLineEdit(m_a.var);
            m_var->setPlaceholderText(T("e.g. count"));
            m_expr->setPlaceholderText(T("e.g. count + 1, random(1, 10), \"some text\""));
            m_form->addRow(T("Variable:"), m_var);
            m_form->addRow(T("Value:"), hrow({m_expr, help}));
            m_form->addRow(QString(), note(T("Numbers, \"text in quotes\", other variables and calculations. "
                                                          "count + 1 counts up; random(1, 10) picks a number; "
                                                          "item(\"a, b, c\", round) takes the next item of a list each round.")));
        } else if (t == ActionType::If) {
            m_expr->setPlaceholderText(T("e.g. count < 10"));
            m_form->addRow(T("Condition:"), hrow({m_expr, help}));
            m_form->addRow(QString(), note(T("Examples: count < 10 · answer = \"yes\" · found (after a Find Image "
                                                          "step) · round > 1 and not exists(\"C:\\done.txt\")")));
        }
    }
    if (t == ActionType::AskText) {
        m_message = new QLineEdit(m_a.message);
        m_message->setPlaceholderText(T("e.g. Which customer number?"));
        m_default = new QLineEdit(m_a.text);
        m_default->setPlaceholderText(T("optional"));
        m_var = new QLineEdit(m_a.var.isEmpty() ? T("answer", "variable name") : m_a.var);
        m_form->addRow(T("Question:"), m_message);
        m_form->addRow(T("Default Answer:"), m_default);
        m_form->addRow(T("Save Answer In:"), m_var);
        m_form->addRow(QString(), note(T("Type the answer later with {answer} in a Text step (with the variable's name).")));
    }
    if (t == ActionType::FindImage) {
        m_imageView = new QLabel;
        m_imageView->setAlignment(Qt::AlignCenter);
        m_imageView->setMinimumSize(180, 80);
        m_imageView->setFrameShape(QFrame::StyledPanel);
        m_imageInfo = new QLabel;
        m_imageInfo->setObjectName(QStringLiteral("muted"));
        auto* capture = new QPushButton(T("Capture From Screen…"));
        capture->setToolTip(T("Shows a still picture of the screen: drag a box around what to find. Esc cancels."));
        connect(capture, &QPushButton::clicked, this, [this] {
            QRect r;
            QImage img;
            QString err;
            if (screenpicker::pick(m_inj, T("Drag a box around what to find"), &r, &img, &err)) {
                setImage(img);
                if (m_aw && m_ax->value() == 0 && m_aw->value() == 0) {   // a likely area to look in: around where it is now
                    m_ax->setValue(r.x() - 100);
                    m_ay->setValue(r.y() - 100);
                    m_aw->setValue(r.width() + 200);
                    m_ah->setValue(r.height() + 200);
                }
            } else if (!err.isEmpty()) {
                m_error->setText(err);
                m_error->show();
            }
        });
        auto* load = new QPushButton(T("Load From File…"));
        connect(load, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, T("Choose a Picture"), QString(),
                                                           T("Pictures (*.png *.bmp)"), nullptr, filedialogs::options());
            if (f.isEmpty()) return;
            QImage img(f);
            if (img.isNull()) { m_error->setText(T("“%1” isn't a picture this app can read.").arg(QDir::toNativeSeparators(f))); m_error->show(); return; }
            setImage(img);
        });
        m_form->addRow(T("Picture:"), hrow({m_imageView, vrow({capture, load, m_imageInfo})}));
        m_image = m_a.image;
        setImage(QImage::fromData(m_a.image));
    }
    if (t == ActionType::FindText) {
        m_default = new QLineEdit(m_a.text);
        m_default->setPlaceholderText(T("e.g. Save As (empty: only read the text)"));
        m_form->addRow(T("Text to Find:"), m_default);
        m_var = new QLineEdit(m_a.var);
        m_var->setPlaceholderText(T("optional, e.g. screen_text"));
        m_form->addRow(T("Keep the Text In:"), m_var);
    }
    if (t == ActionType::FindImage || t == ActionType::Screenshot || t == ActionType::FindText) {
        m_lookIn = new QComboBox;
        m_lookIn->addItems({T("Every Screen"), T("This Area")});
        m_lookIn->setCurrentIndex(m_a.area.isEmpty() ? 0 : 1);
        m_ax = spin(-100000, 100000, m_a.area.x(), QStringLiteral("px"), false);
        m_ay = spin(-100000, 100000, m_a.area.y(), QStringLiteral("px"), false);
        m_aw = spin(0, 100000, m_a.area.width(), QStringLiteral("px"), false);
        m_ah = spin(0, 100000, m_a.area.height(), QStringLiteral("px"), false);
        for (QSpinBox* s : {m_ax, m_ay, m_aw, m_ah}) s->setMinimumWidth(96);
        auto* pick = new QPushButton(T("Pick Area…"));
        pick->setToolTip(T("Shows a still picture of the screen: drag a box around the area."));
        connect(pick, &QPushButton::clicked, this, [this] { pickArea(); });
        m_form->addRow(t == ActionType::Screenshot ? T("Save:") : T("Look In:"), m_lookIn);
        m_areaRow = hrow({m_ax, m_ay, m_aw, m_ah, pick});
        m_areaRow->setToolTip(T("X, Y, width and height in screen pixels."));
        m_form->addRow(T("Area:"), m_areaRow);
        auto sync = [this] { m_areaRow->setEnabled(m_lookIn->currentIndex() == 1); };
        connect(m_lookIn, &QComboBox::currentIndexChanged, this, sync);
        sync();
    }
    if (t == ActionType::FindImage) {
        m_tol = spin(0, 100, m_a.tolerance, QStringLiteral("%"));
        m_tol->setToolTip(T("How different the screen may be from the picture: 0 % = exactly the same pixels. "
                                         "10 % allows small changes, such as a button lit up under the pointer."));
        m_form->addRow(T("Tolerance:"), m_tol);
    }
    if (t == ActionType::FindImage || t == ActionType::FindText) {
        m_findDo = new QComboBox;
        m_findDo->addItems({T("Nothing (Only Find)"), T("Move the Pointer There"), T("Click"), T("Double-Click"), T("Right-Click")});
        m_findDo->setCurrentIndex(int(m_a.findDo));
        m_form->addRow(T("Then:"), m_findDo);
        m_ox = spin(-10000, 10000, m_a.offset.x(), QStringLiteral("px"), false);
        m_oy = spin(-10000, 10000, m_a.offset.y(), QStringLiteral("px"), false);
        m_form->addRow(T("Offset From Center:"), hrow({m_ox, m_oy}));
        m_ms = spin(0, 86400, m_a.ms / 1000, QStringLiteral("s"));
        m_ms->setSpecialValueText(T("Look Once"));
        m_ms->setToolTip(t == ActionType::FindText ? T("Keeps reading the screen until the text appears, up to this long.")
                                                   : T("Keeps looking until the picture appears, up to this long."));
        m_form->addRow(T("Wait Up To:"), m_ms);
        m_waitGone = new QCheckBox(T("Wait Until Gone Instead"));
        m_waitGone->setChecked(m_a.waitGone);
        m_stopMissing = new QCheckBox(T("Stop Playback If Not Found"));
        m_stopMissing->setChecked(m_a.stopIfMissing);
        m_form->addRow(QString(), m_waitGone);
        m_form->addRow(QString(), m_stopMissing);
        auto sync = [this] {
            m_findDo->setEnabled(!m_waitGone->isChecked());
            m_stopMissing->setEnabled(!m_waitGone->isChecked());
        };
        connect(m_waitGone, &QCheckBox::toggled, this, sync);
        sync();
        m_form->addRow(QString(), note(t == ActionType::FindText
            ? T("Reads the screen with the system's text recognition (case and spaces don't matter). Afterwards "
                "found is 1 (or 0), and found_x, found_y hold the text's position. A small area reads faster "
                "and more reliably than the whole screen.")
            : T("Afterwards found is 1 (or 0 if it wasn't found), and found_x, found_y hold its position: use "
                "them in an If step.")));
    }
    if (t == ActionType::AskAI) {
        m_text = new QPlainTextEdit(m_a.message);
        m_text->setTabChangesFocus(true);
        m_text->setMinimumHeight(90);
        m_text->setPlaceholderText(T("e.g. Write a one-line reply to this message: {=clipboard()}"));
        m_form->addRow(T("Prompt:"), m_text);
        m_var = new QLineEdit(m_a.var.isEmpty() ? T("answer", "variable name") : m_a.var);
        m_form->addRow(T("Save Answer In:"), m_var);
        m_ms = spin(1, 3600, m_a.ms > 0 ? m_a.ms / 1000 : 60, QStringLiteral("s"));
        m_ms->setToolTip(T("Playback stops if there's no answer by then."));
        m_form->addRow(T("Give Up After:"), m_ms);
        m_form->addRow(QString(), note(varTip() + QLatin1Char(' ')
                                       + T("The prompt goes to the AI server in File > Settings > AI Server… "
                                           "(LM Studio on this PC unless changed). A Text step types the answer "
                                           "with the variable's name in braces, for example {answer}.")));
    }
    if (t == ActionType::SetClipboard) {
        m_text = new QPlainTextEdit(m_a.text);
        m_text->setTabChangesFocus(true);
        m_text->setMinimumHeight(80);
        m_form->addRow(T("Text:"), m_text);
        m_form->addRow(QString(), note(varTip()));
    }
    if (t == ActionType::WaitPixel) {
        m_color = new QLineEdit(m_a.color.isEmpty() ? QStringLiteral("#FFFFFF") : m_a.color);
        m_color->setPlaceholderText(QStringLiteral("#RRGGBB"));
        m_tol = spin(0, 100, m_a.tolerance, QStringLiteral("%"));
        m_form->addRow(T("Color:"), hrow({m_color, new QLabel(T("Tolerance:")), m_tol}));
    }
    if (t == ActionType::WindowFocus || t == ActionType::WaitPixel || t == ActionType::WaitKey || t == ActionType::WindowMove) {
        m_ms = spin(0, 86400, m_a.ms / 1000, QStringLiteral("s"));
        m_ms->setToolTip(t == ActionType::WindowFocus  ? T("Playback stops if the window doesn't appear in time.")
                         : t == ActionType::WindowMove ? T("Waits this long for the window to appear; then playback continues without it.")
                                                       : T("0 = wait as long as it takes. On timeout, playback continues."));
        m_form->addRow(T("Give Up After:"), m_ms);
    }
    if (t == ActionType::Repeat || t == ActionType::Goto) {
        m_target = new QComboBox;
        m_target->setEditable(true);
        m_target->addItems(m_labels);
        m_target->setCurrentText(m_a.target);
        m_form->addRow(T("Jump to Label:"), m_target);
    }
    if (t == ActionType::Goto) {
        m_expr->setPlaceholderText(T("optional, e.g. count < 10 (empty: always jumps)"));
        m_form->addRow(T("Only If:"), m_expr);
        m_form->addRow(QString(), note(T("With a condition, Go To can make a loop that ends: it jumps back only "
                                                      "while the condition is true.")));
    }
    if (t == ActionType::Repeat) {
        m_mode = new QComboBox;
        m_mode->addItems({T("Times in Total"), T("Seconds"), T("Until Stopped")});
        m_mode->setCurrentIndex(int(m_a.repeatMode));
        m_count = spin(1, 1000000, m_a.count);
        m_form->addRow(T("Repeat:"), hrow({m_count, m_mode}));
        connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int i) { m_count->setEnabled(i != 2); });
        m_count->setEnabled(m_mode->currentIndex() != 2);
    }
    if (t == ActionType::RunProgram) {
        m_program = new QLineEdit(m_a.program);
        m_program->setCursorPosition(0);   // a long path shows its start
        auto* browse = new QPushButton(T("Browse…"));
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, T("Choose a Program or File"), m_program->text(),
                                                           QString(), nullptr, filedialogs::options());
            if (!f.isEmpty()) m_program->setText(QDir::toNativeSeparators(f));
        });
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_program, 1);
        h->addWidget(browse);
        m_args = new QLineEdit(m_a.arguments);
        m_form->addRow(T("Program:"), w);
        m_form->addRow(T("Arguments:"), m_args);
        m_form->addRow(QString(), note(varTip()));
    }
    if (t == ActionType::Notify || t == ActionType::MessageBox) {
        m_message = new QLineEdit(m_a.message);
        m_form->addRow(T("Message:"), m_message);
        m_form->addRow(QString(), note(varTip()));
    }
    if (t == ActionType::WaitFile || t == ActionType::RunMacro || t == ActionType::Screenshot) {
        m_file = new QLineEdit(QDir::toNativeSeparators(m_a.file));
        m_file->setCursorPosition(0);
        m_file->setMinimumWidth(260);
        auto* browse = new QPushButton(T("Browse…"));
        connect(browse, &QPushButton::clicked, this, [this, t] {
            const QString f = t == ActionType::RunMacro
                                  ? QFileDialog::getOpenFileName(this, T("Choose a Macro"), m_file->text(), T("Macros (*.macro)"),
                                                                 nullptr, filedialogs::options())
                              : t == ActionType::Screenshot
                                  ? QFileDialog::getSaveFileName(this, T("Save Screenshots As"), m_file->text(),
                                                                 T("PNG Pictures (*.png)"), nullptr, filedialogs::options())
                                  : QFileDialog::getSaveFileName(this, T("Choose File to Watch"), m_file->text(), QString(), nullptr,
                                                                 filedialogs::options(QFileDialog::DontConfirmOverwrite));
            if (!f.isEmpty()) m_file->setText(QDir::toNativeSeparators(f));
        });
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_file, 1);
        h->addWidget(browse);
        m_form->addRow(t == ActionType::RunMacro ? T("Macro File:") : T("File:"), w);
        if (t == ActionType::Screenshot)
            m_form->addRow(QString(), note(T("A name without a folder saves in the Pictures folder. "
                                                          "{=now(\"yyyy-MM-dd HH.mm.ss\")} in the name gives each picture its own name.")));
    }
    if (t == ActionType::WaitFile) {
        m_fileWhen = new QComboBox;
        m_fileWhen->addItems({T("Exists"), T("Is Gone"), T("Changes")});
        m_fileWhen->setCurrentIndex(int(m_a.fileWhen));
        m_form->addRow(T("Continue When File:"), m_fileWhen);
        m_ms = spin(0, 86400, m_a.ms / 1000, QStringLiteral("s"));
        m_ms->setToolTip(T("0 = wait as long as it takes. On timeout, playback continues."));
        m_form->addRow(T("Give Up After:"), m_ms);
    }

    // Fields every action has
    m_label = new QLineEdit(m_a.label);
    m_label->setPlaceholderText(T("optional: a name Repeat / Go to can jump to"));
    m_comment = new QLineEdit(m_a.comment);
    m_delay = spin(0, 60000, m_a.delayMs, QStringLiteral("ms"));
    m_delay->setToolTip(T("A short pause before this step, kept from the recording (used when timing is exact)."));
    m_enabled = new QCheckBox(T("Enabled"));
    m_enabled->setToolTip(T("Unticked steps are skipped when playing."));
    m_enabled->setChecked(m_a.enabled);
    if (t == ActionType::Group) {
        m_label->setPlaceholderText(T("e.g. Log In"));
        m_form->addRow(T("Group Name:"), m_label);
    } else {
        m_form->addRow(T("Label:"), m_label);
    }
    m_form->addRow(T("Comment:"), m_comment);
    m_form->addRow(T("Pause Before:"), m_delay);
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
    btn->setText(T("Point at It… 3"));
    connect(timer, &QTimer::timeout, this, [=, this] {
        if (--*left > 0) { btn->setText(T("Point at It… %1").arg(*left)); return; }
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

void ActionDialog::setImage(const QImage& img)
{
    if (!m_imageView) return;
    if (img.isNull()) {
        m_imageView->setPixmap(QPixmap());
        m_imageView->setText(T("No picture yet"));
        m_imageInfo->setText(QString());
        return;
    }
    if (img.size() != QImage::fromData(m_image).size() || m_image.isEmpty() || QImage::fromData(m_image) != img) {
        QBuffer buf(&m_image);
        m_image.clear();
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
    }
    // Small pictures shown larger (up to 3×, pixels kept sharp), big ones shrunk to fit
    const QSize box(220, 110);
    QSize shown = img.size();
    const double up = qMin(3.0, qMin(double(box.width()) / img.width(), double(box.height()) / img.height()));
    shown = (QSizeF(img.size()) * up).toSize();
    m_imageView->setPixmap(QPixmap::fromImage(img.scaled(shown.expandedTo(QSize(1, 1)), Qt::KeepAspectRatio,
                                                         up >= 1 ? Qt::FastTransformation : Qt::SmoothTransformation)));
    m_imageInfo->setText(T("%1 × %2 px").arg(img.width()).arg(img.height()));
}

void ActionDialog::pickArea()
{
    QRect r;
    QString err;
    if (!screenpicker::pick(m_inj, T("Drag a box around the area"), &r, nullptr, &err)) {
        if (!err.isEmpty()) { m_error->setText(err); m_error->show(); }
        return;
    }
    m_lookIn->setCurrentIndex(1);
    m_ax->setValue(r.x());
    m_ay->setValue(r.y());
    m_aw->setValue(r.width());
    m_ah->setValue(r.height());
}

void ActionDialog::pickWindow()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn || !m_inj) return;
    const QString original = btn->text();
    btn->setEnabled(false);
    auto left = std::make_shared<int>(3);
    auto* timer = new QTimer(this);
    btn->setText(T("Point at It… 3"));
    connect(timer, &QTimer::timeout, this, [=, this] {
        if (--*left > 0) { btn->setText(T("Point at It… %1").arg(*left)); return; }
        timer->stop();
        timer->deleteLater();
        btn->setText(original);
        btn->setEnabled(true);
        QString exe, title;
        QRect frame;
        if (!m_inj->windowAt(m_inj->cursor(), &exe, &title, &frame)) {
            m_error->setText(T("No window was found under the pointer (or this system doesn't say which it is)."));
            m_error->show();
            return;
        }
        m_error->hide();
        m_exe->setText(exe);
        m_title->setText(title);
        m_match->setCurrentIndex(title.isEmpty() ? int(core::TitleMatch::Any) : int(core::TitleMatch::Exact));
        m_rx->setValue(frame.x());
        m_ry->setValue(frame.y());
        m_rw->setValue(frame.width());
        m_rh->setValue(frame.height());
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
    if (m_usePicture && !m_usePicture->isChecked()) {
        a.image.clear();
        a.offset = {};
    }
    if (m_notches) {
        if (m_notches->value() == 0) return fail(T("Scroll at least one notch (use a negative number to scroll down)."));
        a.notches = m_notches->value();
        a.horizontal = m_horiz->isChecked();
    }
    if (m_key) {
        const QString k = m_key->currentText().trimmed();
        bool known = k.startsWith(QLatin1String("VK_0x"));
        if (known) a.key = k;
        for (const QString& n : keyNames())
            if (n.compare(k, Qt::CaseInsensitive) == 0) { a.key = n; known = true; }
        if (!known) return fail(T("“%1” isn't a key name this app knows. Pick one from the list.").arg(k));
        a.mods.clear();
        for (QCheckBox* c : std::as_const(m_mods))
            if (c->isChecked() && c->text() != a.key) a.mods << c->text();
    }
    if (m_text && t == ActionType::AskAI) {
        a.message = m_text->toPlainText().trimmed();
        if (a.message.isEmpty()) return fail(T("Enter the prompt: what to ask the AI."));
    } else if (m_text) {
        const QString s = m_text->toPlainText();
        if (s.isEmpty() && t == ActionType::Text) return fail(T("Enter the text to type."));
        if (s != a.text) a.strokes.clear();
        a.text = s;
    }
    if (m_ms) {
        const bool seconds = t == ActionType::WindowFocus || t == ActionType::WaitPixel || t == ActionType::WaitKey || t == ActionType::WaitFile
                             || t == ActionType::WindowMove || t == ActionType::FindImage || t == ActionType::AskAI
                             || t == ActionType::FindText;
        a.ms = seconds ? m_ms->value() * 1000 : m_ms->value();
        if (t == ActionType::MouseMove && a.ms != m_a.ms) a.path.clear();  // a new duration can't keep the recorded path
    }
    if (m_maxMs) {
        a.maxMs = m_maxMs->value();
        if (a.maxMs > 0 && a.maxMs <= a.ms)
            return fail(T("“Random Up To” must be longer than the wait (or Off)."));
    }
    if (t == ActionType::MouseMove && (a.pos != m_a.pos || a.from != m_a.from)) a.path.clear();
    if (m_exe) {
        a.exe = m_exe->text().trimmed();
        a.title = m_title->text();
        a.titleMatch = core::TitleMatch(m_match->currentIndex());
        if (t == ActionType::WindowFocus && a.exe.isEmpty() && (a.titleMatch == core::TitleMatch::Any || a.title.isEmpty()))
            return fail(T("Enter a program name, a window title, or both."));
    }
    if (m_state) {
        a.windowState = core::WindowState(m_state->currentIndex());
        a.rect = a.windowState == core::WindowState::Normal ? QRect(m_rx->value(), m_ry->value(), m_rw->value(), m_rh->value()) : QRect();
    }
    if (m_var) {
        a.var = m_var->text().trimmed();
        if (a.var.isEmpty() && t != ActionType::FindText) return fail(T("Enter the variable's name."));
        if (!a.var.isEmpty() && !core::expr::isName(a.var))
            return fail(T("“%1” can't be a variable's name: use letters, digits and _, starting with a letter "
                                       "(for example count or file_2).").arg(a.var));
    }
    if (m_expr) {
        a.expr = m_expr->text().trimmed();
        if (a.expr.isEmpty() && t != ActionType::Goto)
            return fail(t == ActionType::If ? T("Enter the condition, for example count < 10.")
                                            : T("Enter the value, for example 0, count + 1 or \"text\"."));
        QString e;
        if (!a.expr.isEmpty() && !core::expr::check(a.expr, &e)) return fail(e);
    }
    if (m_default) a.text = t == ActionType::FindText ? m_default->text().trimmed() : m_default->text();
    if (t == ActionType::FindText && a.text.isEmpty() && a.var.isEmpty())
        return fail(T("Enter the text to find, or a variable to keep the text read in."));
    if (t == ActionType::FindImage) {
        if (m_image.isEmpty()) return fail(T("Capture the picture to find from the screen first (or load it from a file)."));
        a.image = m_image;
        a.tolerance = m_tol->value();
    }
    if (m_findDo) {
        a.findDo = core::FindDo(m_findDo->currentIndex());
        a.offset = QPoint(m_ox->value(), m_oy->value());
        a.waitGone = m_waitGone->isChecked();
        a.stopIfMissing = m_stopMissing->isChecked();
    }
    if (m_lookIn) {
        a.area = m_lookIn->currentIndex() == 1 ? QRect(m_ax->value(), m_ay->value(), m_aw->value(), m_ah->value()) : QRect();
        if (m_lookIn->currentIndex() == 1 && a.area.isEmpty()) return fail(T("Give the area a width and a height (or Pick Area…)."));
        if (t == ActionType::FindImage && !a.area.isEmpty()) {
            const QImage img = QImage::fromData(m_image);
            if (img.width() > a.area.width() || img.height() > a.area.height())
                return fail(T("The area is smaller than the picture (%1 × %2 px).").arg(img.width()).arg(img.height()));
        }
    }
    if (m_color) {
        const QColor c(m_color->text().trimmed());
        if (!c.isValid()) return fail(T("Enter the color as #RRGGBB, for example #FF8800."));
        a.color = c.name(QColor::HexRgb).toUpper();
        a.tolerance = m_tol->value();
    }
    if (m_target) {
        a.target = m_target->currentText().trimmed();
        if (a.target.isEmpty()) return fail(T("Choose the label to jump to. Give a step a label first (Label column)."));
        if (!m_labels.contains(a.target) && a.target != m_label->text().trimmed())
            return fail(T("No step has the label “%1” yet. Give the step you want to jump to that label first.").arg(a.target));
    }
    if (m_mode) {
        a.repeatMode = core::RepeatMode(m_mode->currentIndex());
        a.count = m_count->value();
    }
    if (m_program) {
        a.program = m_program->text().trimmed();
        a.arguments = m_args->text().trimmed();
        if (a.program.isEmpty()) return fail(T("Choose the program or file to open."));
    }
    if (m_message) {
        a.message = m_message->text().trimmed();
        if (a.message.isEmpty())
            return fail(t == ActionType::AskText ? T("Enter the question to ask.") : T("Enter the message to show."));
    }
    if (m_file) {
        a.file = QDir::fromNativeSeparators(m_file->text().trimmed());
        if (a.file.isEmpty())
            return fail(t == ActionType::RunMacro     ? T("Choose the macro file to play.")
                        : t == ActionType::Screenshot ? T("Enter the file to save the picture as.")
                                                      : T("Enter the file to watch."));
        // A file name made from variables can't be checked until it plays
        if (t == ActionType::RunMacro && !core::expr::hasPlaceholders(a.file) && !QFileInfo::exists(a.file))
            return fail(T("“%1” doesn't exist.").arg(m_file->text().trimmed()));
    }
    if (m_fileWhen) a.fileWhen = core::FileCondition(m_fileWhen->currentIndex());
    a.label = m_label->text().trimmed();
    a.comment = m_comment->text().trimmed();
    a.delayMs = m_delay->value();
    a.enabled = m_enabled->isChecked();
    m_a = a;
    QDialog::accept();
}
