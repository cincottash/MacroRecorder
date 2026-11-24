#include "ActionModel.h"

#include "core/Player.h"

#include <QApplication>
#include <QLocale>
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
    case ActionType::SetVariable: return QStringLiteral("≡");
    case ActionType::If: return QStringLiteral("◇");
    case ActionType::Else: return QStringLiteral("↳");
    case ActionType::EndIf: return QStringLiteral("↲");
    case ActionType::AskText: return QStringLiteral("✎");
    case ActionType::Group: return QStringLiteral("▾");
    case ActionType::EndGroup: return QStringLiteral("▴");
    case ActionType::WindowMove: return QStringLiteral("⇲");
    case ActionType::FindImage: return QStringLiteral("⌖");
    case ActionType::Screenshot: return QStringLiteral("⎙");
    case ActionType::SetClipboard: return QStringLiteral("⎘");
    }
    return {};
}

QString ActionModel::glyph(const core::Action& a)
{
    if (a.type == ActionType::Group && a.collapsed) return QStringLiteral("▸");
    return glyph(a.type);
}

QPixmap ActionModel::icon(const QString& glyphText, const QColor& color, int indent)
{
    // Symbols drawn as small monochrome pictures in the theme's text color, so they look the same size everywhere
    // and never turn into colored emoji. Cached per symbol, color and indent.
    static QHash<QString, QPixmap> cache;
    const qreal dpr = qApp->devicePixelRatio();
    const int x0 = qBound(0, indent, 12) * IndentPx;
    const QString key = glyphText + color.name() + QString::number(dpr) + QLatin1Char('/') + QString::number(x0);
    if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;
    QPixmap pm(QSize(18 + x0, 18) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f(QStringLiteral("Segoe UI Symbol"));
    f.setPixelSize(14);
    p.setFont(f);
    p.setPen(color);
    p.drawText(QRect(x0, 0, 18, 18), Qt::AlignCenter, glyphText + QChar(0xFE0E));  // U+FE0E: text style, not emoji
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
            if (a.type == ActionType::Group && role == Qt::DisplayRole) {
                const int end = groupEnd(i.row());
                const int n = qMax(0, (end < 0 ? int(m_actions.size()) : end) - i.row() - 1);
                v = QStringLiteral("%1 step%2").arg(QLocale().toString(n), n == 1 ? QString() : QStringLiteral("s"));
                if (a.collapsed) v += QStringLiteral(" · folded");
            }
            if (a.delayMs > 0 && role == Qt::DisplayRole) v += QStringLiteral("   (+%1 ms before)").arg(a.delayMs);
            // A text over several lines shows all of them, its line breaks as ⏎ (a cell shows only its first line)
            if (role == Qt::DisplayRole && v.contains(QLatin1Char('\n'))) {
                v.replace(QLatin1String("\r\n"), QLatin1String("\n"));
                v.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
            }
            return v;
        }
        case ColLabel: return a.label;
        case ColComment: return a.comment;
        }
        break;
    case Qt::DecorationRole:
        if (i.column() == ColAction)
            return icon(glyph(a), QApplication::palette().color(a.enabled ? QPalette::Text : QPalette::PlaceholderText), depth(i.row()));
        break;
    case DepthRole: return depth(i.row());
    case GroupRole:
        if (a.type == ActionType::Group) return a.collapsed;
        break;
    case Qt::ToolTipRole:
        if (!a.enabled && (a.type == ActionType::Group || a.type == ActionType::If))
            return QStringLiteral("Disabled: every step inside is skipped when playing (Edit > Enable / Disable).");
        if (!a.enabled) return QStringLiteral("Disabled: skipped when playing (Edit > Enable / Disable).");
        if (a.type == ActionType::Group && i.column() == ColAction)
            return a.collapsed ? QStringLiteral("Click the arrow (or press Right) to show the steps in this group.")
                               : QStringLiteral("Click the arrow (or press Left) to fold the steps in this group away.");
        if (i.column() == ColValue && (a.type == ActionType::SetVariable || a.type == ActionType::If)) return a.valueText();
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

static bool isStructure(ActionType t)
{
    return t == ActionType::If || t == ActionType::Else || t == ActionType::EndIf || t == ActionType::Group || t == ActionType::EndGroup;
}

void ActionModel::recompute()
{
    bool any = false;
    for (const core::Action& a : std::as_const(m_actions))
        if (isStructure(a.type)) { any = true; break; }
    const bool had = !m_depth.isEmpty() || !m_groupEnd.isEmpty();
    m_depth.clear();
    m_folded.clear();
    m_groupEnd.clear();
    if (!any) {   // a plain list (most recordings): nothing to work out
        if (had && !m_actions.isEmpty()) emit dataChanged(index(0, 0), index(int(m_actions.size()) - 1, ColCount - 1));
        return;
    }
    const int n = int(m_actions.size());
    m_depth.resize(n);
    m_folded.resize(n);
    int d = 0;
    for (int r = 0; r < n; ++r) {
        const ActionType t = m_actions.at(r).type;
        if (t == ActionType::Else) m_depth[r] = qMax(0, d - 1);
        else if (t == ActionType::EndIf || t == ActionType::EndGroup) { d = qMax(0, d - 1); m_depth[r] = d; }
        else {
            m_depth[r] = d;
            if (t == ActionType::If || t == ActionType::Group) ++d;
        }
    }
    const core::Player::Blocks b = core::Player::blocks(m_actions, 0, n - 1);
    for (int r = 0; r < n; ++r) {
        if (m_actions.at(r).type != ActionType::Group) continue;
        const int end = qMin(b.next.value(r, n), n);
        m_groupEnd.insert(r, end);
        if (m_actions.at(r).collapsed)
            for (int k = r + 1; k <= qMin(end, n - 1); ++k) m_folded[k] = true;   // its End Group too
    }
    emit dataChanged(index(0, 0), index(n - 1, ColCount - 1));
}

void ActionModel::setCollapsed(int row, bool collapsed)
{
    if (row < 0 || row >= m_actions.size() || m_actions.at(row).type != ActionType::Group || m_actions.at(row).collapsed == collapsed) return;
    m_actions[row].collapsed = collapsed;
    recompute();
    emit foldingChanged();
}

void ActionModel::setActions(const QList<core::Action>& a)
{
    beginResetModel();
    m_actions = a;
    m_playing = -1;
    endResetModel();
    recompute();
}

void ActionModel::insert(int row, const core::Action& a)
{
    row = qBound(0, row, int(m_actions.size()));
    beginInsertRows({}, row, row);
    m_actions.insert(row, a);
    endInsertRows();
    if (row < m_actions.size() - 1) emit dataChanged(index(row, ColNum), index(int(m_actions.size()) - 1, ColNum));
    if (isStructure(a.type) || !m_depth.isEmpty()) recompute();
}

void ActionModel::replace(int row, const core::Action& a)
{
    if (row < 0 || row >= m_actions.size()) return;
    const bool structural = isStructure(a.type) || isStructure(m_actions.at(row).type);
    m_actions[row] = a;
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
    if (structural) recompute();
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
    recompute();
}

void ActionModel::setPlayingRow(int row)
{
    const int old = m_playing;
    m_playing = row;
    if (old >= 0 && old < m_actions.size()) emit dataChanged(index(old, 0), index(old, ColCount - 1));
    if (row >= 0 && row < m_actions.size()) emit dataChanged(index(row, 0), index(row, ColCount - 1));
}
