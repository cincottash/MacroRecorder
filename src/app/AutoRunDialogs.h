// Macro > Macro Hotkeys… and Macro > Schedules…: saved macros played without opening them. Each window edits a
// copy of its list; OK keeps the changes, Cancel leaves the list as it was.
#pragma once

#include "Settings.h"

#include <QDialog>
#include <QHash>

#include <functional>

class QLabel;
class QPushButton;
class QTableWidget;

class MacroHotkeysDialog : public QDialog {
    Q_OBJECT
public:
    // appKeys: the app's own hotkeys (name -> key), which a macro can't use as well
    MacroHotkeysDialog(const QList<MacroHotkey>& list, const QHash<QString, QString>& appKeys, const QString& folder, QWidget* parent);
    QList<MacroHotkey> result() const { return m_list; }
    bool editAt(int row);   // Edit… (also double-click); public for tests

private:
    void refresh();
    bool edit(MacroHotkey* h, int row);   // row -1: a new one
    QList<MacroHotkey> m_list;
    QHash<QString, QString> m_appKeys;
    QString m_folder;
    QTableWidget* m_table;
    QPushButton *m_edit, *m_remove;
};

class SchedulesDialog : public QDialog {
    Q_OBJECT
public:
    SchedulesDialog(const QList<Schedule>& list, std::function<QDateTime()> now, const QString& folder, QWidget* parent);
    QList<Schedule> result() const { return m_list; }
    bool editAt(int row);

private:
    void refresh();
    bool edit(Schedule* s);
    QList<Schedule> m_list;
    std::function<QDateTime()> m_now;
    QString m_folder;
    QTableWidget* m_table;
    QPushButton *m_edit, *m_remove;
};

namespace autorun {
// "Tomorrow 08:00", "Today 17:30", "Mon 12 Dec 09:00" (the system's formats); "Not again" when invalid
QString whenText(const QDateTime& when, const QDateTime& now);
}
