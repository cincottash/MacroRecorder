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
    static QString glyph(const core::Action& a);   // a group's arrow shows whether it's folded
    // indent: steps inside If and Group blocks; the picture gets wider, so the symbol and the text move right.
    static QPixmap icon(const QString& glyph, const QColor& color, int indent = 0);
    static QPixmap icon(core::ActionType t, const QColor& color) { return icon(glyph(t), color); }
    static constexpr int IndentPx = 16;

    static constexpr int DepthRole = Qt::UserRole + 1;   // how far the step is indented (inside If and Group blocks)
    static constexpr int GroupRole = Qt::UserRole + 2;   // Group rows: true when folded; other rows: nothing
    int depth(int row) const { ensureStructure(); return row >= 0 && row < m_depth.size() ? m_depth.at(row) : 0; }
    bool foldedAway(int row) const { ensureStructure(); return row >= 0 && row < m_folded.size() && m_folded.at(row); }
    int groupEnd(int row) const { ensureStructure(); return m_groupEnd.value(row, -1); }   // a Group's End Group row (rowCount: none)
    void setCollapsed(int row, bool collapsed);   // folds or unfolds a group (not an edit: undo doesn't see it)

signals:
    void edited();   // a label or comment was changed in place
    void foldingChanged();

private:
    void ensureStructure() const;    // indentation, folding and group sizes, worked out again if the steps changed
    void changed(bool repaintAll);   // the steps changed: the structure is out of date

    QList<core::Action> m_actions;
    int m_playing = -1;
    mutable bool m_stale = true;
    mutable QVector<int> m_depth;
    mutable QVector<bool> m_folded;
    mutable QHash<int, int> m_groupEnd;
};
