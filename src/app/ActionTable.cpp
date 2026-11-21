#include "ActionTable.h"

#include <QApplication>
#include <QHeaderView>
#include <QMouseEvent>
#include <QRubberBand>
#include <QScrollBar>

ActionTable::ActionTable(QWidget* parent) : QTableView(parent)
{
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
        setHover(m_active ? -1 : indexAt(p).row());
        QTableView::mouseMoveEvent(e);
        return;
    }
    m_last = p;
    if (!m_active) {
        if ((contentPos(p) - m_origin).manhattanLength() < QApplication::startDragDistance()) return;
        m_active = true;
        if (!m_band) m_band = new QRubberBand(QRubberBand::Rectangle, viewport());
        m_band->show();
        setHover(-1);
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
        e->accept();
        return;
    }
    QTableView::mouseReleaseEvent(e);
}

void ActionTable::leaveEvent(QEvent* e)
{
    setHover(-1);
    QTableView::leaveEvent(e);
}

bool ActionTable::viewportEvent(QEvent* e)
{
    if (e->type() == QEvent::Leave) setHover(-1);
    return QTableView::viewportEvent(e);
}

void ActionTable::setHover(int row)
{
    if (row == m_hover) return;
    m_hover = row;
    emit hoveredRow(row);
}
