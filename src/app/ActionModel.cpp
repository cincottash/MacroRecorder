#include "ActionModel.h"

#include <QApplication>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QPalette>

using core::ActionType;

QString ActionModel::glyph(ActionType t)
{
    switch (t) {
    case ActionType::MouseMove: return QStringLiteral("↝");
    case ActionType::MouseClick: return QStringLiteral("◉");
    case ActionType::MouseDown: return QStringLiteral("▼");
    case ActionType::MouseUp: return QStringLiteral("▲");
    case ActionType::Wheel: return QStringLiteral("⇕");
    case ActionType::KeyPress:
    case ActionType::KeyDown:
    case ActionType::KeyUp: return QStringLiteral("⌨");
    case ActionType::Text: return QStringLiteral("T");
    case ActionType::Wait: return QStringLiteral("◷");
    case ActionType::WindowFocus: return QStringLiteral("▣");
    case ActionType::WaitPixel: return QStringLiteral("◈");
    case ActionType::WaitKey: return QStringLiteral("⌛");
    case ActionType::Repeat: return QStringLiteral("↻");
    case ActionType::Goto: return QStringLiteral("↪");
    case ActionType::RunProgram: return QStringLiteral("▶");
    case ActionType::Notify: return QStringLiteral("✉");
    case ActionType::WaitFile: return QStringLiteral("▤");
    case ActionType::MessageBox: return QStringLiteral("□");
    case ActionType::Beep: return QStringLiteral("♪");
    case ActionType::RunMacro: return QStringLiteral("⧉");
    }
    return {};
}

QPixmap ActionModel::icon(core::ActionType t, const QColor& color)
{
    // Symbols drawn as small monochrome pictures in the theme's text color, so they look the same size everywhere
    // and never turn into colored emoji. Cached per symbol and color.
    static QHash<QString, QPixmap> cache;
    const qreal dpr = qApp->devicePixelRatio();
    const QString key = glyph(t) + color.name() + QString::number(dpr);
    if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;
    QPixmap pm(QSize(18, 18) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f(QStringLiteral("Segoe UI Symbol"));
    f.setPixelSize(14);
    p.setFont(f);
    p.setPen(color);
    p.drawText(QRect(0, 0, 18, 18), Qt::AlignCenter, glyph(t) + QChar(0xFE0E));  // U+FE0E: text style, not emoji
    p.end();
    cache.insert(key, pm);
    return pm;
}

QVariant ActionModel::data(const QModelIndex& i, int role) const
{
    if (!i.isValid() || i.row() >= m_actions.size()) return {};
    const core::Action& a = m_actions.at(i.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        switch (i.column()) {
        case ColNum: return i.row() + 1;
        case ColAction: return a.typeName();
        case ColValue: {
            QString v = a.valueText();
            if (a.delayMs > 0 && role == Qt::DisplayRole) v += QStringLiteral("   (+%1 ms before)").arg(a.delayMs);
            return v;
        }
        case ColLabel: return a.label;
        case ColComment: return a.comment;
        }
        break;
    case Qt::DecorationRole:
        if (i.column() == ColAction) return icon(a.type, QApplication::palette().color(a.enabled ? QPalette::Text : QPalette::PlaceholderText));
        break;
    case Qt::ToolTipRole:
        if (!a.enabled) return QStringLiteral("Disabled: skipped when playing (Edit > Enable / Disable).");
        if (i.column() == ColLabel) return QStringLiteral("Double-click to name this step, so Repeat and Go to can jump here.");
        if (i.column() == ColValue && a.type == ActionType::Text) return a.text;
        break;
    case Qt::ForegroundRole:
        if (!a.enabled) return QApplication::palette().color(QPalette::PlaceholderText);
        break;
    case Qt::FontRole:
        if (i.row() == m_playing || !a.enabled) {
            QFont f = QApplication::font();
            f.setBold(i.row() == m_playing);
            f.setStrikeOut(!a.enabled);
            return f;
        }
        break;
    case Qt::TextAlignmentRole:
        if (i.column() == ColNum) return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    }
    return {};
}

QVariant ActionModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal) return {};
    // Headers line up with their column's values: numbers on the right, everything else on the left.
    if (role == Qt::TextAlignmentRole)
        return int((section == ColNum ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    if (role != Qt::DisplayRole) return {};
    switch (section) {
    case ColNum: return QStringLiteral("#");
    case ColAction: return QStringLiteral("Action");
    case ColValue: return QStringLiteral("Value");
    case ColLabel: return QStringLiteral("Label");
    case ColComment: return QStringLiteral("Comment");
    }
    return {};
}

Qt::ItemFlags ActionModel::flags(const QModelIndex& i) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(i);
    if (i.column() == ColLabel || i.column() == ColComment) f |= Qt::ItemIsEditable;
    return f;
}

bool ActionModel::setData(const QModelIndex& i, const QVariant& v, int role)
{
    if (role != Qt::EditRole || !i.isValid()) return false;
    core::Action& a = m_actions[i.row()];
    const QString s = v.toString().trimmed();
    if (i.column() == ColLabel) { if (a.label == s) return false; a.label = s; }
    else if (i.column() == ColComment) { if (a.comment == s) return false; a.comment = s; }
    else return false;
    emit dataChanged(i, i);
    emit edited();
    return true;
}

void ActionModel::setActions(const QList<core::Action>& a)
{
    beginResetModel();
    m_actions = a;
    m_playing = -1;
    endResetModel();
}

void ActionModel::insert(int row, const core::Action& a)
{
    row = qBound(0, row, int(m_actions.size()));
    beginInsertRows({}, row, row);
    m_actions.insert(row, a);
    endInsertRows();
    if (row < m_actions.size() - 1) emit dataChanged(index(row, ColNum), index(int(m_actions.size()) - 1, ColNum));
}

void ActionModel::replace(int row, const core::Action& a)
{
    if (row < 0 || row >= m_actions.size()) return;
    m_actions[row] = a;
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
}

void ActionModel::removeRowsList(QList<int> rows)
{
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    for (int r : rows) {
        if (r < 0 || r >= m_actions.size()) continue;
        beginRemoveRows({}, r, r);
        m_actions.removeAt(r);
        endRemoveRows();
    }
    if (!m_actions.isEmpty()) emit dataChanged(index(0, ColNum), index(int(m_actions.size()) - 1, ColNum));
}

void ActionModel::setPlayingRow(int row)
{
    const int old = m_playing;
    m_playing = row;
    if (old >= 0 && old < m_actions.size()) emit dataChanged(index(old, 0), index(old, ColCount - 1));
    if (row >= 0 && row < m_actions.size()) emit dataChanged(index(row, 0), index(row, ColCount - 1));
}
