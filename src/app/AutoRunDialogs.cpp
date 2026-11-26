#include "AutoRunDialogs.h"

#include "FileDialogs.h"
#include "core/Action.h"
#include "core/Tr.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextCharFormat>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// A small calendar in the theme's colours: the frame in the button's text colour, its top in the accent
QIcon calendarIcon(const QPalette& pal)
{
    const QColor ink = pal.color(QPalette::ButtonText), band = pal.color(QPalette::Highlight);
    QIcon icon;
    for (int s : {16, 20, 24, 32, 40, 48}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(s / 16.0, s / 16.0);
        QPainterPath body;
        body.addRoundedRect(QRectF(1.5, 2.5, 13, 12), 2, 2);
        p.save();
        p.setClipRect(QRectF(0, 0, 16, 6));
        p.fillPath(body, band);
        p.restore();
        p.setPen(QPen(ink, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(body);
        p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(5, 1), QPointF(5, 4));
        p.drawLine(QPointF(11, 1), QPointF(11, 4));
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c) p.drawRect(QRectF(3.6 + c * 3.2, 7.6 + r * 3.2, 1.9, 1.9));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// The calendar's month arrows in the theme's text colour (Qt's own are dark on every theme)
QIcon chevronIcon(const QColor& ink, bool left)
{
    QIcon icon;
    for (int s : {16, 24, 32, 48}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(s / 16.0, s / 16.0);
        p.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal x = left ? 10 : 6, tip = left ? 5.5 : 10.5;
        p.drawPolyline(QPolygonF({QPointF(x, 3.5), QPointF(tip, 8), QPointF(x, 12.5)}));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// The calendar button's pop-up: a month to click the day in, and columns of hours and minutes to click the time
// in. Each click goes straight into the Date and Time boxes, which still take typing (any minute, not only the
// five-minute steps listed here).
class WhenPicker : public QFrame {
public:
    WhenPicker(QDateEdit* date, QTimeEdit* time, bool withDate, const QDate& firstDay, QWidget* parent)
        : QFrame(parent, Qt::Popup), m_date(date), m_time(time)
    {
        setObjectName(QStringLiteral("whenPicker"));
        setProperty("testId", QStringLiteral("whenPicker"));
        setAttribute(Qt::WA_DeleteOnClose);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(10, 10, 10, 10);
        auto* row = new QHBoxLayout;
        row->setSpacing(10);
        if (withDate) {
            m_cal = new QCalendarWidget;
            m_cal->setProperty("testId", QStringLiteral("pickDay"));
            m_cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
            m_cal->setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
            m_cal->setFirstDayOfWeek(QLocale().firstDayOfWeek());
            if (firstDay.isValid()) m_cal->setMinimumDate(firstDay);
            m_cal->setSelectedDate(date->date());
            // Every day in the normal text colour (Qt draws weekends red and Saturdays blue, hard to read on dark themes)
            QTextCharFormat day;
            day.setForeground(palette().color(QPalette::Text));
            for (int dn = Qt::Monday; dn <= Qt::Sunday; ++dn) m_cal->setWeekdayTextFormat(Qt::DayOfWeek(dn), day);
            // Days that can't be picked (before today, for Once) look it: Qt draws them like the rest
            if (firstDay.isValid()) {
                QTextCharFormat past;
                past.setForeground(palette().color(QPalette::Disabled, QPalette::Text));
                for (QDate p = firstDay.addDays(-42); p < firstDay; p = p.addDays(1)) m_cal->setDateTextFormat(p, past);
            }
            // Columns wide enough for the day names ("Wed" was cut to "…")
            int dayW = 0;
            for (int dn = 1; dn <= 7; ++dn) dayW = qMax(dayW, fontMetrics().horizontalAdvance(QLocale().dayName(dn, QLocale::ShortFormat)));
            m_cal->setMinimumWidth(7 * (dayW + 22) + 4);
            QTextCharFormat head;
            head.setForeground(palette().color(QPalette::PlaceholderText));
            head.setFontWeight(QFont::DemiBold);
            m_cal->setHeaderTextFormat(head);
            for (const auto& [name, left] : {std::pair{"qt_calendar_prevmonth", true}, std::pair{"qt_calendar_nextmonth", false}})
                if (auto* b = m_cal->findChild<QToolButton*>(QLatin1String(name))) {
                    b->setIcon(chevronIcon(palette().color(QPalette::Text), left));
                    b->setToolTip(left ? T("Previous month") : T("Next month"));
                }
            connect(m_cal, &QCalendarWidget::selectionChanged, this, [this] { m_date->setDate(m_cal->selectedDate()); });
            row->addWidget(m_cal, 1);
        }
        const bool twelve = QLocale().timeFormat(QLocale::ShortFormat).contains(QLatin1String("ap"), Qt::CaseInsensitive);
        m_hours = column(QStringLiteral("pickHour"));
        for (int h = 0; h < 24; ++h)
            addTime(m_hours, twelve ? QLocale().toString(QTime(h, 0), QStringLiteral("h AP")) : QStringLiteral("%1").arg(h, 2, 10, QLatin1Char('0')), h);
        m_minutes = column(QStringLiteral("pickMinute"));
        for (int m = 0; m < 60; m += 5) addTime(m_minutes, QStringLiteral(":%1").arg(m, 2, 10, QLatin1Char('0')), m);
        row->addLayout(labelled(T("Hour"), m_hours));
        row->addLayout(labelled(T("Minute"), m_minutes));
        lay->addLayout(row);
        auto* done = new QPushButton(T("Done"));
        done->setObjectName(QStringLiteral("primary"));
        done->setProperty("testId", QStringLiteral("pickDone"));
        connect(done, &QPushButton::clicked, this, &QWidget::close);
        auto* bottom = new QHBoxLayout;
        auto* hint = new QLabel(T("Any other minute can be typed in the Time box."));
        hint->setObjectName(QStringLiteral("muted"));
        bottom->addWidget(hint, 1);
        bottom->addWidget(done);
        lay->addLayout(bottom);
        showTime();
        // A click or the arrow keys: the time box follows
        connect(m_hours, &QListWidget::currentRowChanged, this, [this](int r) {
            if (r >= 0) m_time->setTime(QTime(m_hours->item(r)->data(Qt::UserRole).toInt(), m_time->time().minute()));
        });
        connect(m_minutes, &QListWidget::currentRowChanged, this, [this](int r) {
            if (r >= 0) m_time->setTime(QTime(m_time->time().hour(), m_minutes->item(r)->data(Qt::UserRole).toInt()));
        });
        // Typing in the boxes while this is open (it isn't, normally: a pop-up takes the keyboard) keeps it in step
        connect(m_time, &QTimeEdit::timeChanged, this, [this] { showTime(); });
        if (m_cal) connect(m_date, &QDateEdit::dateChanged, this, [this](QDate d) { if (m_cal->selectedDate() != d) m_cal->setSelectedDate(d); });
    }

    // Opens below `anchor`, its right edge on the anchor's, kept on the anchor's screen
    void openAt(QWidget* anchor)
    {
        adjustSize();
        if (m_cal) {   // the time columns as tall as the month
            const int h = m_cal->sizeHint().height();
            m_hours->setFixedHeight(h);
            m_minutes->setFixedHeight(h);
        } else {
            m_hours->setFixedHeight(m_hours->sizeHintForRow(0) * 9 + 4);
            m_minutes->setFixedHeight(m_hours->height());
        }
        adjustSize();
        QPoint at = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height() + 2));
        if (const QScreen* s = anchor->screen()) {
            const QRect avail = s->availableGeometry();
            if (at.y() + height() > avail.bottom()) at.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 2);   // above instead
            at.setX(qBound(avail.left(), at.x(), avail.right() - width()));
            at.setY(qBound(avail.top(), at.y(), avail.bottom() - height()));
        }
        move(at);
        show();
        // The chosen hour and minute in view
        if (QListWidgetItem* i = m_hours->currentItem()) m_hours->scrollToItem(i, QAbstractItemView::PositionAtCenter);
        if (QListWidgetItem* i = m_minutes->currentItem()) m_minutes->scrollToItem(i, QAbstractItemView::PositionAtCenter);
        if (m_cal) m_cal->setFocus();
    }

private:
    static QListWidget* column(const QString& id)
    {
        auto* l = new QListWidget;
        l->setProperty("testId", id);
        l->setSelectionMode(QAbstractItemView::SingleSelection);
        l->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        l->setUniformItemSizes(true);
        return l;
    }
    static void addTime(QListWidget* l, const QString& text, int value)
    {
        auto* i = new QListWidgetItem(text);
        i->setData(Qt::UserRole, value);
        i->setTextAlignment(Qt::AlignCenter);
        l->addItem(i);
    }
    static QVBoxLayout* labelled(const QString& title, QListWidget* list)
    {
        auto* v = new QVBoxLayout;
        v->setSpacing(4);
        auto* t = new QLabel(title);
        t->setObjectName(QStringLiteral("muted"));
        t->setAlignment(Qt::AlignCenter);
        v->addWidget(t);
        v->addWidget(list);
        // Wide enough for the longest entry and a scroll bar
        int w = 0;
        for (int r = 0; r < list->count(); ++r) w = qMax(w, list->fontMetrics().horizontalAdvance(list->item(r)->text()));
        w = qMax(w, t->fontMetrics().horizontalAdvance(title));
        list->setFixedWidth(w + list->style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 28);
        return v;
    }
    void showTime()
    {
        const QSignalBlocker a(m_hours), b(m_minutes);
        const QTime t = m_time->time();
        m_hours->setCurrentRow(t.hour());
        if (t.minute() % 5 == 0) m_minutes->setCurrentRow(t.minute() / 5);
        else {   // a typed minute between the steps: none of them is chosen
            m_minutes->setCurrentRow(-1);
            m_minutes->clearSelection();
        }
    }

    QDateEdit* m_date;
    QTimeEdit* m_time;
    QCalendarWidget* m_cal = nullptr;
    QListWidget* m_hours = nullptr;
    QListWidget* m_minutes = nullptr;
};

QLabel* intro(const QString& text)
{
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

QLabel* errorLabel()
{
    auto* l = new QLabel;
    l->setProperty("tone", QStringLiteral("err"));
    l->setWordWrap(true);
    l->hide();
    return l;
}

QTableWidget* makeTable(const QStringList& headers)
{
    auto* t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(26);
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);
    t->setWordWrap(false);
    t->horizontalHeader()->setStretchLastSection(true);
    t->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    t->setFrameShape(QFrame::StyledPanel);   // a visible frame around the list
    t->setMinimumHeight(170);
    return t;
}

// The list with its buttons in a column to its right (Add…, Edit…, Remove)
QHBoxLayout* listWithButtons(QTableWidget* table, QPushButton* add, QPushButton* edit, QPushButton* remove)
{
    auto* row = new QHBoxLayout;
    row->addWidget(table, 1);
    auto* col = new QVBoxLayout;
    col->addWidget(add);
    col->addWidget(edit);
    col->addWidget(remove);
    col->addStretch();
    row->addLayout(col);
    return row;
}

QWidget* fileRow(QLineEdit* edit, QWidget* dialog, const QString& folder)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* browse = new QPushButton(T("Browse…"));
    QObject::connect(browse, &QPushButton::clicked, dialog, [edit, dialog, folder] {
        const QString start = edit->text().isEmpty() ? folder : edit->text();
        const QString f = QFileDialog::getOpenFileName(dialog, T("Choose a Macro"), start, T("Macros (*.macro)"),
                                                       nullptr, filedialogs::options());
        if (!f.isEmpty()) edit->setText(QDir::toNativeSeparators(f));
    });
    edit->setMinimumWidth(320);
    edit->setCursorPosition(0);   // a long path shows its start (the drive and folders), not its end
    h->addWidget(edit, 1);
    h->addWidget(browse);
    return w;
}

// A macro file the app can play: exists and reads as a macro. Empty when fine, else the reason.
QString checkMacro(const QString& path)
{
    if (path.isEmpty()) return T("Choose the macro file to play.");
    if (!QFileInfo::exists(path)) return T("“%1” doesn't exist.").arg(QDir::toNativeSeparators(path));
    core::Macro m;
    QString err;
    if (!core::Macro::load(path, &m, &err)) return T("“%1” can't be played: %2").arg(QFileInfo(path).fileName(), err);
    return {};
}

QTableWidgetItem* item(const QString& text, const QString& tip = {})
{
    auto* i = new QTableWidgetItem(text);
    i->setToolTip(tip.isEmpty() ? text : tip);
    return i;
}

}  // namespace

namespace autorun {

QString whenText(const QDateTime& when, const QDateTime& now)
{
    if (!when.isValid()) return T("Not again");
    const QLocale loc;
    const QString time = loc.toString(when.time(), QLocale::ShortFormat);
    const qint64 days = now.date().daysTo(when.date());
    if (days == 0) return T("Today %1").arg(time);
    if (days == 1) return T("Tomorrow %1").arg(time);
    return loc.toString(when, QLocale::ShortFormat);
}

}  // namespace autorun

// ---------------------------------------------------------------------------------------------- Macro Hotkeys

MacroHotkeysDialog::MacroHotkeysDialog(const QList<MacroHotkey>& list, const QHash<QString, QString>& appKeys, const QString& folder,
                                       QWidget* parent)
    : QDialog(parent), m_list(list), m_appKeys(appKeys), m_folder(folder)
{
    setWindowTitle(T("Macro Hotkeys"));
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(intro(T("A hotkey here plays its macro file right away, from any program, without opening it. "
                           "Pressing it again while that macro plays stops it. The macro plays once, with the "
                           "playback options of the main window.")));
    m_table = makeTable({T("Hotkey"), T("Macro File")});
    m_table->setProperty("testId", QStringLiteral("macroHotkeyList"));
    m_table->horizontalHeader()->resizeSection(0, 150);
    auto* add = new QPushButton(T("Add…"));
    m_edit = new QPushButton(T("Edit…"));
    m_remove = new QPushButton(T("Remove"));
    lay->addLayout(listWithButtons(m_table, add, m_edit, m_remove));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setToolTip(T("Leaves the hotkeys as they were when this window opened."));
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(add, &QPushButton::clicked, this, [this] {
        MacroHotkey h;
        if (edit(&h, -1)) {
            m_list << h;
            refresh();
            m_table->selectRow(int(m_list.size()) - 1);
        }
    });
    connect(m_edit, &QPushButton::clicked, this, [this] { editAt(m_table->currentRow()); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) { editAt(row); });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int r = m_table->currentRow();
        if (r < 0 || r >= m_list.size()) return;
        m_list.removeAt(r);
        refresh();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool any = !m_table->selectedItems().isEmpty();
        m_edit->setEnabled(any);
        m_remove->setEnabled(any);
    });
    refresh();
    resize(720, 360);
}

void MacroHotkeysDialog::refresh()
{
    m_table->setRowCount(int(m_list.size()));
    for (int r = 0; r < m_list.size(); ++r) {
        const MacroHotkey& h = m_list.at(r);
        m_table->setItem(r, 0, item(QKeySequence(h.key).toString(QKeySequence::NativeText)));
        m_table->setItem(r, 1, item(QFileInfo(h.file).fileName(), QDir::toNativeSeparators(h.file)));
    }
    m_table->clearSelection();
    m_edit->setEnabled(false);
    m_remove->setEnabled(false);
}

bool MacroHotkeysDialog::editAt(int row)
{
    if (row < 0 || row >= m_list.size()) return false;
    MacroHotkey h = m_list.at(row);
    if (!edit(&h, row)) return false;
    m_list[row] = h;
    refresh();
    m_table->selectRow(row);
    return true;
}

bool MacroHotkeysDialog::edit(MacroHotkey* h, int row)
{
    QDialog d(this);
    d.setWindowTitle(row < 0 ? T("Add Macro Hotkey") : T("Edit Macro Hotkey"));
    auto* lay = new QVBoxLayout(&d);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(12);   // room between each label and its field
    auto* file = new QLineEdit(QDir::toNativeSeparators(h->file));
    auto* key = new QKeySequenceEdit(QKeySequence(h->key));
    key->setMaximumSequenceLength(1);
    key->setClearButtonEnabled(true);
    form->addRow(T("Macro File:"), fileRow(file, &d, m_folder));
    form->addRow(T("Hotkey:"), key);
    lay->addLayout(form);
    auto* tip = new QLabel(T("Click the hotkey box, then press the keys, for example Ctrl+Alt+1."));
    tip->setObjectName(QStringLiteral("muted"));
    tip->setWordWrap(true);
    lay->addWidget(tip);
    QLabel* error = errorLabel();
    lay->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
        const QString path = QDir::fromNativeSeparators(file->text().trimmed());
        const QString k = key->keySequence().toString(QKeySequence::PortableText);
        QString why = checkMacro(path);
        if (why.isEmpty() && k.isEmpty()) why = T("Press the keys for the hotkey in the Hotkey box.");
        if (why.isEmpty())
            for (auto it = m_appKeys.cbegin(); it != m_appKeys.cend(); ++it)
                if (!it.value().isEmpty() && QKeySequence(it.value()) == QKeySequence(k))
                    why = T("%1 is already the %2 hotkey (in the main window's Hotkeys card).").arg(key->keySequence().toString(QKeySequence::NativeText), it.key());
        if (why.isEmpty())
            for (int i = 0; i < m_list.size(); ++i)
                if (i != row && QKeySequence(m_list.at(i).key) == QKeySequence(k))
                    why = T("%1 already plays %2.").arg(key->keySequence().toString(QKeySequence::NativeText), QFileInfo(m_list.at(i).file).fileName());
        if (!why.isEmpty()) {
            error->setText(why);
            error->show();
            return;
        }
        h->file = path;
        h->key = k;
        d.accept();
    });
    d.setMinimumWidth(520);
    return d.exec() == QDialog::Accepted;
}

// ---------------------------------------------------------------------------------------------- Schedules

SchedulesDialog::SchedulesDialog(const QList<Schedule>& list, std::function<QDateTime()> now, const QString& folder, QWidget* parent)
    : QDialog(parent), m_list(list), m_now(std::move(now)), m_folder(folder)
{
    setWindowTitle(T("Schedules"));
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(intro(T("Plays a macro file at the times set here, while the app is running (minimized is fine). "
                           "To be sure it's running, turn on File > Settings > Start at Login. A macro that's due while "
                           "another one plays waits for it to finish.")));
    m_table = makeTable({T("On", "column: schedule turned on"), T("When"), T("Next Time"), T("Macro File")});
    m_table->setProperty("testId", QStringLiteral("scheduleList"));
    // The On column as wide as its header (in Spanish, "Activa"); its check box needs 44 px
    QFont headerFont = m_table->horizontalHeader()->font();
    headerFont.setWeight(QFont::DemiBold);   // the style sheet's header weight
    m_table->horizontalHeader()->resizeSection(
        0, qMax(44, QFontMetrics(headerFont).horizontalAdvance(m_table->horizontalHeaderItem(0)->text()) + 30));
    m_table->horizontalHeader()->resizeSection(1, 230);
    m_table->horizontalHeader()->resizeSection(2, 150);
    auto* add = new QPushButton(T("Add…"));
    m_edit = new QPushButton(T("Edit…"));
    m_remove = new QPushButton(T("Remove"));
    lay->addLayout(listWithButtons(m_table, add, m_edit, m_remove));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setToolTip(T("Leaves the schedules as they were when this window opened."));
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(add, &QPushButton::clicked, this, [this] {
        Schedule s;
        const QDateTime n = m_now();
        s.at = QDateTime(n.date(), QTime(n.time().hour(), 0)).addSecs(3600);   // the next full hour
        if (edit(&s)) {
            m_list << s;
            refresh();
            m_table->selectRow(int(m_list.size()) - 1);
        }
    });
    connect(m_edit, &QPushButton::clicked, this, [this] { editAt(m_table->currentRow()); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) { if (col != 0) editAt(row); });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int r = m_table->currentRow();
        if (r < 0 || r >= m_list.size()) return;
        m_list.removeAt(r);
        refresh();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool any = !m_table->selectedItems().isEmpty();
        m_edit->setEnabled(any);
        m_remove->setEnabled(any);
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* i) {
        if (i->column() != 0 || i->row() >= m_list.size()) return;
        const bool on = i->checkState() == Qt::Checked;
        if (m_list[i->row()].enabled == on) return;
        m_list[i->row()].enabled = on;
        const QDateTime next = on ? m_list.at(i->row()).nextRun(m_now()) : QDateTime();
        if (QTableWidgetItem* n = m_table->item(i->row(), 2)) n->setText(on ? autorun::whenText(next, m_now()) : T("Off"));
    });
    resize(820, 380);
    refresh();
}

void SchedulesDialog::refresh()
{
    const QSignalBlocker b(m_table);
    m_table->setRowCount(int(m_list.size()));
    const QDateTime now = m_now();
    for (int r = 0; r < m_list.size(); ++r) {
        const Schedule& s = m_list.at(r);
        auto* on = new QTableWidgetItem;
        on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        on->setCheckState(s.enabled ? Qt::Checked : Qt::Unchecked);
        on->setToolTip(T("Unticked: kept, but not played."));
        m_table->setItem(r, 0, on);
        m_table->setItem(r, 1, item(s.describe()));
        m_table->setItem(r, 2, item(s.enabled ? autorun::whenText(s.nextRun(now), now) : T("Off")));
        m_table->setItem(r, 3, item(QFileInfo(s.file).fileName(), QDir::toNativeSeparators(s.file)));
    }
    // "When" and "Next Time" are shown whole; the file name takes what's left (its full path is in the tooltip)
    for (int c : {1, 2}) {
        m_table->resizeColumnToContents(c);
        m_table->horizontalHeader()->resizeSection(c, qMax(m_table->horizontalHeader()->sectionSize(c) + 12, c == 1 ? 200 : 130));
    }
    int fileWidth = 220;
    for (const Schedule& s : std::as_const(m_list)) fileWidth = qMax(fileWidth, m_table->fontMetrics().horizontalAdvance(QFileInfo(s.file).fileName()) + 30);
    const int need = m_table->horizontalHeader()->sectionSize(0) + m_table->horizontalHeader()->sectionSize(1)
                     + m_table->horizontalHeader()->sectionSize(2) + fileWidth + 150;   // + the buttons beside the list
    if (width() < need) resize(qMin(need, 1300), height());
    m_table->clearSelection();
    m_edit->setEnabled(false);
    m_remove->setEnabled(false);
}

bool SchedulesDialog::editAt(int row)
{
    if (row < 0 || row >= m_list.size()) return false;
    Schedule s = m_list.at(row);
    if (!edit(&s)) return false;
    m_list[row] = s;
    refresh();
    m_table->selectRow(row);
    return true;
}

bool SchedulesDialog::edit(Schedule* s)
{
    QDialog d(this);
    d.setWindowTitle(s->file.isEmpty() ? T("Add Schedule") : T("Edit Schedule"));
    auto* lay = new QVBoxLayout(&d);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(12);   // room between each label and its field
    auto* file = new QLineEdit(QDir::toNativeSeparators(s->file));
    form->addRow(T("Macro File:"), fileRow(file, &d, m_folder));
    auto* repeat = new QComboBox;
    const QList<QPair<QString, QString>> kinds = {{T("Once"), QStringLiteral("once")},
                                                  {T("Every Day"), QStringLiteral("daily")},
                                                  {T("On Chosen Days"), QStringLiteral("days")},
                                                  {T("Every Few Minutes"), QStringLiteral("minutes")}};
    for (const auto& [text, key] : kinds) repeat->addItem(text, key);
    repeat->setCurrentIndex(qMax(0, repeat->findData(s->repeat)));
    form->addRow(T("Repeat:"), repeat);
    auto* date = new QDateEdit(s->at.isValid() ? s->at.date() : m_now().date());
    date->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
    date->setMinimumDate(QDate(2000, 1, 1));
    auto* time = new QTimeEdit(s->at.isValid() ? s->at.time() : QTime(9, 0));
    time->setDisplayFormat(QLocale().timeFormat(QLocale::ShortFormat));
    form->addRow(T("Date:"), date);
    // The time box with the calendar button beside it: pick the day and time by clicking, or type them
    auto* timeRow = new QWidget;
    auto* th = new QHBoxLayout(timeRow);
    th->setContentsMargins(0, 0, 0, 0);
    th->addWidget(time);
    auto* pick = new QToolButton;
    pick->setObjectName(QStringLiteral("tool"));
    pick->setProperty("testId", QStringLiteral("pickWhen"));
    pick->setIcon(calendarIcon(pick->palette()));
    pick->setIconSize(QSize(18, 18));
    pick->setAccessibleName(T("Pick on a Calendar"));
    th->addWidget(pick);
    th->addStretch();
    form->addRow(T("Time:"), timeRow);
    auto* daysRow = new QWidget;
    auto* dh = new QHBoxLayout(daysRow);
    dh->setContentsMargins(0, 0, 0, 0);
    QList<QCheckBox*> dayBoxes;
    for (int dnum = 1; dnum <= 7; ++dnum) {
        auto* c = new QCheckBox(QLocale().dayName(dnum, QLocale::ShortFormat));
        c->setChecked(s->days & (1 << (dnum - 1)));
        dayBoxes << c;
        dh->addWidget(c);
    }
    dh->addStretch();
    form->addRow(T("Days:"), daysRow);
    auto* minutes = new QSpinBox;
    minutes->setRange(1, 100000);
    minutes->setValue(s->minutes);
    minutes->setSuffix(QStringLiteral(" min"));
    minutes->setMinimumWidth(110);
    form->addRow(T("Every:"), minutes);
    lay->addLayout(form);
    QLabel* error = errorLabel();
    lay->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(buttons);
    auto sync = [&] {
        const QString k = repeat->currentData().toString();
        // The Date row: the day for "Once", the first day for "Every Few Minutes"
        form->setRowVisible(date, k == QLatin1String("once") || k == QLatin1String("minutes"));
        form->setRowVisible(daysRow, k == QLatin1String("days"));
        form->setRowVisible(minutes, k == QLatin1String("minutes"));
        if (auto* l = qobject_cast<QLabel*>(form->labelForField(timeRow)))
            l->setText(k == QLatin1String("minutes") ? T("Starting At:") : T("Time:"));
        pick->setToolTip(form->isRowVisible(date) ? T("Pick the day and the time on a calendar. They can be typed in the boxes too.")
                                                  : T("Pick the time from a list. It can be typed in the box too."));
        d.adjustSize();
    };
    connect(repeat, &QComboBox::currentIndexChanged, &d, sync);
    sync();
    connect(pick, &QToolButton::clicked, &d, [&] {
        const bool once = repeat->currentData().toString() == QLatin1String("once");
        auto* picker = new WhenPicker(date, time, form->isRowVisible(date), once ? m_now().date() : QDate(), &d);
        picker->openAt(pick);
    });
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
        Schedule n = *s;
        n.file = QDir::fromNativeSeparators(file->text().trimmed());
        n.repeat = repeat->currentData().toString();
        const QTime t(time->time().hour(), time->time().minute());
        n.at = (n.repeat == QLatin1String("once") || n.repeat == QLatin1String("minutes")) ? QDateTime(date->date(), t)
                                                                                             : QDateTime(m_now().date(), t);
        n.days = 0;
        for (int i = 0; i < 7; ++i)
            if (dayBoxes[i]->isChecked()) n.days |= 1 << i;
        n.minutes = minutes->value();
        QString why = checkMacro(n.file);
        if (why.isEmpty() && n.repeat == QLatin1String("days") && n.days == 0) why = T("Tick at least one day.");
        if (why.isEmpty() && n.repeat == QLatin1String("once") && n.at <= m_now()) why = T("That time has already passed.");
        if (!why.isEmpty()) {
            error->setText(why);
            error->show();
            return;
        }
        if (n.repeat != s->repeat || n.at != s->at || n.days != s->days || n.minutes != s->minutes) n.lastRun = QDateTime();
        n.enabled = true;
        *s = n;
        d.accept();
    });
    d.setMinimumWidth(560);
    return d.exec() == QDialog::Accepted;
}
