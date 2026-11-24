#include "AutoRunDialogs.h"

#include "FileDialogs.h"
#include "core/Action.h"

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
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

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
    auto* browse = new QPushButton(QStringLiteral("Browse…"));
    QObject::connect(browse, &QPushButton::clicked, dialog, [edit, dialog, folder] {
        const QString start = edit->text().isEmpty() ? folder : edit->text();
        const QString f = QFileDialog::getOpenFileName(dialog, QStringLiteral("Choose a Macro"), start, QStringLiteral("Macros (*.macro)"),
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
    if (path.isEmpty()) return QStringLiteral("Choose the macro file to play.");
    if (!QFileInfo::exists(path)) return QStringLiteral("“%1” doesn't exist.").arg(QDir::toNativeSeparators(path));
    core::Macro m;
    QString err;
    if (!core::Macro::load(path, &m, &err)) return QStringLiteral("“%1” can't be played: %2").arg(QFileInfo(path).fileName(), err);
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
    if (!when.isValid()) return QStringLiteral("Not again");
    const QLocale loc;
    const QString time = loc.toString(when.time(), QLocale::ShortFormat);
    const qint64 days = now.date().daysTo(when.date());
    if (days == 0) return QStringLiteral("Today %1").arg(time);
    if (days == 1) return QStringLiteral("Tomorrow %1").arg(time);
    return loc.toString(when, QLocale::ShortFormat);
}

}  // namespace autorun

// ---------------------------------------------------------------------------------------------- Macro Hotkeys

MacroHotkeysDialog::MacroHotkeysDialog(const QList<MacroHotkey>& list, const QHash<QString, QString>& appKeys, const QString& folder,
                                       QWidget* parent)
    : QDialog(parent), m_list(list), m_appKeys(appKeys), m_folder(folder)
{
    setWindowTitle(QStringLiteral("Macro Hotkeys"));
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(intro(QStringLiteral("A hotkey here plays its macro file right away, from any program, without opening it. "
                                        "Pressing it again while that macro plays stops it. The macro plays once, with the "
                                        "playback options of the main window.")));
    m_table = makeTable({QStringLiteral("Hotkey"), QStringLiteral("Macro File")});
    m_table->setProperty("testId", QStringLiteral("macroHotkeyList"));
    m_table->horizontalHeader()->resizeSection(0, 150);
    auto* add = new QPushButton(QStringLiteral("Add…"));
    m_edit = new QPushButton(QStringLiteral("Edit…"));
    m_remove = new QPushButton(QStringLiteral("Remove"));
    lay->addLayout(listWithButtons(m_table, add, m_edit, m_remove));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setToolTip(QStringLiteral("Leaves the hotkeys as they were when this window opened."));
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
    d.setWindowTitle(row < 0 ? QStringLiteral("Add Macro Hotkey") : QStringLiteral("Edit Macro Hotkey"));
    auto* lay = new QVBoxLayout(&d);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(12);   // room between each label and its field
    auto* file = new QLineEdit(QDir::toNativeSeparators(h->file));
    auto* key = new QKeySequenceEdit(QKeySequence(h->key));
    key->setMaximumSequenceLength(1);
    key->setClearButtonEnabled(true);
    form->addRow(QStringLiteral("Macro File:"), fileRow(file, &d, m_folder));
    form->addRow(QStringLiteral("Hotkey:"), key);
    lay->addLayout(form);
    auto* tip = new QLabel(QStringLiteral("Click the hotkey box, then press the keys, for example Ctrl+Alt+1."));
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
        if (why.isEmpty() && k.isEmpty()) why = QStringLiteral("Press the keys for the hotkey in the Hotkey box.");
        if (why.isEmpty())
            for (auto it = m_appKeys.cbegin(); it != m_appKeys.cend(); ++it)
                if (!it.value().isEmpty() && QKeySequence(it.value()) == QKeySequence(k))
                    why = QStringLiteral("%1 is already the %2 hotkey (in the main window's Hotkeys card).").arg(key->keySequence().toString(QKeySequence::NativeText), it.key());
        if (why.isEmpty())
            for (int i = 0; i < m_list.size(); ++i)
                if (i != row && QKeySequence(m_list.at(i).key) == QKeySequence(k))
                    why = QStringLiteral("%1 already plays %2.").arg(key->keySequence().toString(QKeySequence::NativeText), QFileInfo(m_list.at(i).file).fileName());
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
    setWindowTitle(QStringLiteral("Schedules"));
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(intro(QStringLiteral("Plays a macro file at the times set here, while the app is running (minimized is fine). "
                                        "To be sure it's running, turn on File > Settings > Start at Login. A macro that's due while "
                                        "another one plays waits for it to finish.")));
    m_table = makeTable({QStringLiteral("On"), QStringLiteral("When"), QStringLiteral("Next Time"), QStringLiteral("Macro File")});
    m_table->setProperty("testId", QStringLiteral("scheduleList"));
    m_table->horizontalHeader()->resizeSection(0, 44);
    m_table->horizontalHeader()->resizeSection(1, 230);
    m_table->horizontalHeader()->resizeSection(2, 150);
    auto* add = new QPushButton(QStringLiteral("Add…"));
    m_edit = new QPushButton(QStringLiteral("Edit…"));
    m_remove = new QPushButton(QStringLiteral("Remove"));
    lay->addLayout(listWithButtons(m_table, add, m_edit, m_remove));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Cancel)->setToolTip(QStringLiteral("Leaves the schedules as they were when this window opened."));
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
        if (QTableWidgetItem* n = m_table->item(i->row(), 2)) n->setText(on ? autorun::whenText(next, m_now()) : QStringLiteral("Off"));
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
        on->setToolTip(QStringLiteral("Unticked: kept, but not played."));
        m_table->setItem(r, 0, on);
        m_table->setItem(r, 1, item(s.describe()));
        m_table->setItem(r, 2, item(s.enabled ? autorun::whenText(s.nextRun(now), now) : QStringLiteral("Off")));
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
    d.setWindowTitle(s->file.isEmpty() ? QStringLiteral("Add Schedule") : QStringLiteral("Edit Schedule"));
    auto* lay = new QVBoxLayout(&d);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(12);   // room between each label and its field
    auto* file = new QLineEdit(QDir::toNativeSeparators(s->file));
    form->addRow(QStringLiteral("Macro File:"), fileRow(file, &d, m_folder));
    auto* repeat = new QComboBox;
    const QList<QPair<QString, QString>> kinds = {{QStringLiteral("Once"), QStringLiteral("once")},
                                                  {QStringLiteral("Every Day"), QStringLiteral("daily")},
                                                  {QStringLiteral("On Chosen Days"), QStringLiteral("days")},
                                                  {QStringLiteral("Every Few Minutes"), QStringLiteral("minutes")}};
    for (const auto& [text, key] : kinds) repeat->addItem(text, key);
    repeat->setCurrentIndex(qMax(0, repeat->findData(s->repeat)));
    form->addRow(QStringLiteral("Repeat:"), repeat);
    auto* date = new QDateEdit(s->at.isValid() ? s->at.date() : m_now().date());
    date->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
    date->setMinimumDate(QDate(2000, 1, 1));
    auto* time = new QTimeEdit(s->at.isValid() ? s->at.time() : QTime(9, 0));
    time->setDisplayFormat(QLocale().timeFormat(QLocale::ShortFormat));
    form->addRow(QStringLiteral("Date:"), date);
    form->addRow(QStringLiteral("Time:"), time);
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
    form->addRow(QStringLiteral("Days:"), daysRow);
    auto* minutes = new QSpinBox;
    minutes->setRange(1, 100000);
    minutes->setValue(s->minutes);
    minutes->setSuffix(QStringLiteral(" min"));
    minutes->setMinimumWidth(110);
    form->addRow(QStringLiteral("Every:"), minutes);
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
        if (auto* l = qobject_cast<QLabel*>(form->labelForField(time)))
            l->setText(k == QLatin1String("minutes") ? QStringLiteral("Starting At:") : QStringLiteral("Time:"));
        d.adjustSize();
    };
    connect(repeat, &QComboBox::currentIndexChanged, &d, sync);
    sync();
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
        if (why.isEmpty() && n.repeat == QLatin1String("days") && n.days == 0) why = QStringLiteral("Tick at least one day.");
        if (why.isEmpty() && n.repeat == QLatin1String("once") && n.at <= m_now()) why = QStringLiteral("That time has already passed.");
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
