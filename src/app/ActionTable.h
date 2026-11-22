// The step list. Adds what a file manager does: drag a selection box from anywhere in the list (also the empty
// space below the steps), and a light highlight on the row under the pointer. Reports when the pointer is over a
// step's Value, for the preview.
#pragma once

#include <QItemSelectionModel>
#include <QTableView>
#include <QTimer>

class QRubberBand;

class ActionTable : public QTableView {
    Q_OBJECT
public:
    explicit ActionTable(QWidget* parent = nullptr);
    bool selecting() const { return m_band != nullptr && m_active; }
    int hoverRow() const { return m_hover; }   // the row under the pointer (-1: none)

signals:
    void hoveredValue(int row);   // the pointer is over this step's Value; -1: it isn't

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool viewportEvent(QEvent* e) override;   // the pointer leaving the rows' area
    void scrollContentsBy(int dx, int dy) override;   // scrolling moves other rows under the pointer

private:
    QPoint contentPos(QPoint viewportPos) const;   // viewport -> content coordinates (survives scrolling)
    void updateBand();
    void setHover(const QModelIndex& index);   // invalid: none

    QRubberBand* m_band = nullptr;
    bool m_pressed = false, m_active = false;
    QPoint m_origin;                     // content coordinates
    QPoint m_last;                       // viewport coordinates of the pointer
    QItemSelection m_before;             // kept when Ctrl or Shift is held: the box adds to it
    QTimer m_scroll;                     // scrolls while the box is dragged past the top or bottom edge
    int m_hover = -1, m_hoverValue = -1;
};
