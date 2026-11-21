// The action list shown in the main window: # · Action · Value · Label · Comment.
#pragma once

#include "core/Action.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QPixmap>

class ActionModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColNum, ColAction, ColValue, ColLabel, ColComment, ColCount };

    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : int(m_actions.size()); }
    int columnCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : ColCount; }
    QVariant data(const QModelIndex& i, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& i) const override;
    bool setData(const QModelIndex& i, const QVariant& v, int role) override;

    const QList<core::Action>& actions() const { return m_actions; }
    const core::Action& at(int row) const { return m_actions.at(row); }
    void setActions(const QList<core::Action>& a);
    void insert(int row, const core::Action& a);
    void replace(int row, const core::Action& a);
    void removeRowsList(QList<int> rows);
    void setPlayingRow(int row);
    int playingRow() const { return m_playing; }
    static QString glyph(core::ActionType t);
    static QPixmap icon(core::ActionType t, const QColor& color);

signals:
    void edited();   // a label or comment was changed in place

private:
    QList<core::Action> m_actions;
    int m_playing = -1;
};
