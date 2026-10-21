#pragma once
#include <QPointF>
#include <QRectF>
#include <algorithm>

class Selection
{
public:
    explicit Selection(QSize size) : bounds(QPointF(), size) {}
    QRectF area;
    bool valid() const { return area.width() > 0 && area.height() > 0; }
    int hit(QPointF point) const
    {
        const int col = point.x() < area.left() ? 0 : point.x() > area.right() ? 2 : 1;
        const int row = point.y() < area.top() ? 0 : point.y() > area.bottom() ? 2 : 1;
        return row * 3 + col;
    }
    void begin(QPointF point, bool adjust)
    {
        origin = point;
        before = area;
        adjusting = adjust;
        direction = adjust ? hit(point) : -1;
        if (adjust && direction != 4) move(point);
    }
    void move(QPointF point)
    {
        point.setX(std::clamp(point.x(), bounds.left(), bounds.right()));
        point.setY(std::clamp(point.y(), bounds.top(), bounds.bottom()));
        if (!adjusting) { area = QRectF(origin, point).normalized().intersected(bounds); return; }
        if (direction == 4) {
            area = before.translated(point - origin);
            area.moveLeft(std::clamp(area.left(), bounds.left(), bounds.right() - area.width()));
            area.moveTop(std::clamp(area.top(), bounds.top(), bounds.bottom() - area.height()));
            return;
        }
        auto rect = before;
        const int col = direction % 3, row = direction / 3;
        if (col == 0) rect.setLeft(point.x());
        if (col == 2) rect.setRight(point.x());
        if (row == 0) rect.setTop(point.y());
        if (row == 2) rect.setBottom(point.y());
        area = rect.normalized();
        const qreal width = qMin(4., bounds.width()), height = qMin(4., bounds.height());
        if (area.width() < width) { if (col == 0) area.setLeft(area.right() - width); else area.setRight(area.left() + width); }
        if (area.height() < height) { if (row == 0) area.setTop(area.bottom() - height); else area.setBottom(area.top() + height); }
        if (area.left() < 0) area.moveLeft(0);
        if (area.top() < 0) area.moveTop(0);
        if (area.right() > bounds.right()) area.moveRight(bounds.right());
        if (area.bottom() > bounds.bottom()) area.moveBottom(bounds.bottom());
    }
private:
    QRectF bounds, before;
    QPointF origin;
    bool adjusting{false};
    int direction{-1};
};
