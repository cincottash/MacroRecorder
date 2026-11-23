#include "ActionTable.h"

#include "Look.h"

#include <QApplication>
#include <QCursor>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QHeaderView>
#include <QMouseEvent>
#include <QRubberBand>
#include <QScrollBar>

namespace {

// Paints the hover highlight under each cell of the row the pointer is over: the theme's accent, faint, so it
// reads as "here" and never as "selected".
class HoverDelegate : public QStyledItemDelegate {
public:
    explicit HoverDelegate(ActionTable* t) : QStyledItemDelegate(t), m_t(t) {}
    void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const override
    {
        if (i.row() == m_t->hoverRow() && !m_t->selecting()) {
            QColor c = o.palette.color(QPalette::Highlight);
            const bool selected = o.state & QStyle::State_Selected;
            if (!selected) {
                c.setAlphaF(look::values().hover);
                p->fillRect(o.rect, c);
            }
            QStyledItemDelegate::paint(p, o, i);
            if (selected) p->fillRect(o.rect, QColor(255, 255, 255, look::values().hoverOnSelected));   // slightly lighter
            return;
        }
        QStyledItemDelegate::paint(p, o, i);
    }

private:
    ActionTable* m_t;
};

// The selection box: the style's own, or the theme's accent at the strength set in Look.h.
class SelectionBand : public QRubberBand {
public:
    explicit SelectionBand(QWidget* parent) : QRubberBand(QRubberBand::Rectangle, parent) {}

protected:
    void paintEvent(QPaintEvent* e) override
    {
        const look::Values& v = look::values();
        if (v.bandFill < 0) { QRubberBand::paintEvent(e); return; }
        QPainter p(this);
        QColor fill = palette().color(QPalette::Highlight), line = fill;
        fill.setAlphaF(v.bandFill);
        line.setAlphaF(v.bandBorder);
        p.fillRect(rect(), fill);
        p.setPen(line);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        // Over steps that are already selected the accent edge disappears into the selection: a faint line in the
        // selection's text color just inside it keeps the box's edge visible there too
        QColor inner = palette().color(QPalette::HighlightedText);
        inner.setAlphaF(v.bandInner);
        p.setPen(inner);
        if (width() > 4 && height() > 4) p.drawRect(rect().adjusted(1, 1, -2, -2));
    }
};

}  // namespace

ActionTable::ActionTable(QWidget* parent) : QTableView(parent)
{
    setItemDelegate(new HoverDelegate(this));
    setMouseTracking(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);   // smooth scrolling while dragging the box
    m_scroll.setInterval(30);
    connect(&m_scroll, &QTimer::timeout, this, [this] {
        if (!m_active) return;
        const int h = viewport()->height(), edge = 12;
        int step = 0;
        if (m_last.y() < edge) step = -qMax(4, (edge - m_last.y()) / 2);
        else if (m_last.y() > h - edge) step = qMax(4, (m_last.y() - (h - edge)) / 2);
        if (!step) return;
        QScrollBar* bar = verticalScrollBar();
        const int before = bar->value();
        bar->setValue(before + step);
        if (bar->value() != before) updateBand();
    });
}

QPoint ActionTable::contentPos(QPoint p) const { return p + QPoint(horizontalOffset(), verticalOffset()); }

void ActionTable::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QTableView::mousePressEvent(e); return; }
    m_pressed = true;
    m_active = false;
    m_origin = contentPos(e->position().toPoint());
    m_last = e->position().toPoint();
    m_before = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) ? selectionModel()->selection() : QItemSelection();
    QTableView::mousePressEvent(e);   // a plain click: selects the row (Ctrl toggles, Shift extends); empty space clears
}

void ActionTable::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint p = e->position().toPoint();
    if (!m_pressed || !(e->buttons() & Qt::LeftButton)) {
        setHover(m_active ? QModelIndex() : indexAt(p));
        QTableView::mouseMoveEvent(e);
        return;
    }
    m_last = p;
    if (!m_active) {
        if ((contentPos(p) - m_origin).manhattanLength() < QApplication::startDragDistance()) return;
        m_active = true;
        if (!m_band) m_band = new SelectionBand(viewport());
        m_band->show();
        setHover({});
        m_scroll.start();
    }
    updateBand();
}

void ActionTable::updateBand()
{
    const QRect box = QRect(m_origin, contentPos(m_last)).normalized();
    m_band->setGeometry(box.translated(-horizontalOffset(), -verticalOffset()).intersected(viewport()->rect().adjusted(0, 0, 1, 1)));
    // Every visible row the box touches is selected (plus what was selected before, with Ctrl or Shift held).
    QItemSelection sel = m_before;
    const bool inColumns = box.left() < horizontalHeader()->length();
    QHeaderView* rows = verticalHeader();
    const int cols = model() ? model()->columnCount() : 0;
    int runStart = -1, lastTouched = -1;
    auto close = [&](int end) {
        if (runStart >= 0) sel.select(model()->index(runStart, 0), model()->index(end, cols - 1));
        runStart = -1;
    };
    for (int r = 0; model() && r < model()->rowCount(); ++r) {
        if (isRowHidden(r)) { close(lastTouched); continue; }   // hidden by the Show filter: never selected unseen
        const int y = rows->sectionPosition(r), h = rows->sectionSize(r);
        const bool touched = inColumns && y + h > box.top() && y <= box.bottom();
        if (touched) {
            if (runStart < 0) runStart = r;
            lastTouched = r;
        } else if (runStart >= 0) {
            close(lastTouched);
        }
    }
    close(lastTouched);
    selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void ActionTable::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) m_pressed = false;
    if (m_active && e->button() == Qt::LeftButton) {
        m_active = false;
        m_scroll.stop();
        if (m_band) m_band->hide();
        setState(QAbstractItemView::NoState);
        setHover(indexAt(e->position().toPoint()));
        e->accept();
        return;
    }
    QTableView::mouseReleaseEvent(e);
}

void ActionTable::leaveEvent(QEvent* e)
{
    setHover({});
    QTableView::leaveEvent(e);
}

bool ActionTable::viewportEvent(QEvent* e)
{
    if (e->type() == QEvent::Leave) setHover({});
    return QTableView::viewportEvent(e);
}

void ActionTable::scrollContentsBy(int dx, int dy)
{
    QTableView::scrollContentsBy(dx, dy);
    if (m_hover >= 0 && !m_active) {
        const QPoint p = viewport()->mapFromGlobal(QCursor::pos());
        setHover(viewport()->rect().contains(p) ? indexAt(p) : QModelIndex());
    }
}

void ActionTable::setHover(const QModelIndex& index)
{
    const int row = index.isValid() ? index.row() : -1;
    if (row == m_hover) return;
    m_hover = row;
    viewport()->update();   // repaint the old and new highlighted rows
    emit hoveredRow(row);
}
