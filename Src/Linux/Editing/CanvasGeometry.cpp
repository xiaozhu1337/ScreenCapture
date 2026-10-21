#include "Canvas.h"
#include <QPainter>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
QPointF radial(qreal radius, qreal degrees)
{
    const qreal angle = degrees * std::numbers::pi / 180;
    return {radius * std::cos(angle), -radius * std::sin(angle)};
}
}
QPainterPath Canvas::outline(const Stroke& stroke) const
{
    QPainterPath path;
    const auto bounds = stroke.bounds();
    if (stroke.tool == Tool::Rectangle) path.addRect(bounds);
    else if (stroke.tool == Tool::Ellipse) path.addEllipse(bounds);
    else if (stroke.tool == Tool::Arrow) {
        path.moveTo(stroke.start);
        const auto delta = stroke.end - stroke.start;
        const qreal length = std::hypot(delta.x(), delta.y());
        if (length >= 1) {
            const QPointF unit = delta / length, normal(-unit.y(), unit.x());
            const qreal a = 4 * stroke.style.size * stroke.ratio, b = a / 4, c = 2 * a / 3;
            path.lineTo(stroke.end - a * unit - b * normal);
            path.lineTo(stroke.end - (a + b) * unit - c * normal);
            path.lineTo(stroke.end);
            path.lineTo(stroke.end - (a + b) * unit + c * normal);
            path.lineTo(stroke.end - a * unit + b * normal);
        }
        path.closeSubpath();
    } else if (stroke.tool == Tool::Number) {
        path.arcMoveTo(bounds, stroke.angle + 10);
        path.arcTo(bounds, stroke.angle + 10, 170);
        path.arcTo(bounds, stroke.angle + 180, 170);
        path.lineTo(stroke.start + radial(stroke.radius * 4 / 3, stroke.angle));
        path.closeSubpath();
    }
    return path;
}
QList<QPointF> Canvas::handles(const Stroke& stroke) const
{
    if (stroke.tool == Tool::Text) return {};
    if (stroke.tool == Tool::Number) return {stroke.start, stroke.start + radial(stroke.radius * 4 / 3, stroke.angle),
        stroke.start + radial(stroke.radius, stroke.angle + 180)};
    if (stroke.tool == Tool::Arrow || stroke.freehand()) return {stroke.start, stroke.end};
    const auto b = stroke.bounds();
    return {b.topLeft(), {b.center().x(), b.top()}, b.topRight(), {b.right(), b.center().y()},
        b.bottomRight(), {b.center().x(), b.bottom()}, b.bottomLeft(), {b.left(), b.center().y()}};
}
int Canvas::handleAt(const Stroke& stroke, QPointF point) const
{
    if (stroke.tool == Tool::Text) {
        const qreal half = 4 * stroke.ratio;
        const auto bounds = stroke.bounds();
        if (!bounds.adjusted(-half, -half, half, half).contains(point)) return -1;
        return bounds.adjusted(half, half, -half, -half).contains(point) ? 1 : 8;
    }
    const qreal half = 3 * stroke.ratio;
    const auto points = handles(stroke);
    for (int i = 0; i < points.size(); ++i)
        if (qAbs(point.x() - points[i].x()) < half && qAbs(point.y() - points[i].y()) < half) return i;
    return -1;
}
bool Canvas::hits(const Stroke& stroke, QPointF point) const
{
    if (stroke.tool == Tool::Number) return false;
    if (stroke.tool == Tool::Text) return handleAt(stroke, point) >= 0;
    if (stroke.tool == Tool::Arrow) return outline(stroke).contains(point);
    const qreal width = stroke.style.size * stroke.ratio;
    if (stroke.freehand()) {
        const qreal outer = width / 2 + stroke.ratio, inner = outer - 3 * stroke.ratio;
        qreal distance = std::numeric_limits<qreal>::max();
        for (int i = 1; i < stroke.points.size(); ++i) {
            const auto a = stroke.points[i - 1], ab = stroke.points[i] - a, ap = point - a;
            const qreal length2 = QPointF::dotProduct(ab, ab);
            const qreal t = length2 == 0 ? 0 : qBound(0., QPointF::dotProduct(ap, ab) / length2, 1.);
            distance = qMin(distance, QLineF(point, a + t * ab).length());
        }
        return distance <= outer && distance >= inner;
    }
    const qreal half = stroke.style.fill || stroke.tool == Tool::Mosaic || stroke.tool == Tool::Eraser
        ? 2 * stroke.ratio : width / 2 + stroke.ratio;
    const auto bounds = stroke.bounds();
    if (stroke.tool == Tool::Ellipse) {
        const auto delta = point - bounds.center();
        const qreal rx = bounds.width() / 2, ry = bounds.height() / 2;
        const auto inside = [delta](qreal x, qreal y) {
            return x > 0 && y > 0 && delta.x() * delta.x() / (x * x) + delta.y() * delta.y() / (y * y) <= 1;
        };
        return inside(rx + half, ry + half) && !inside(qMax(0., rx - half), qMax(0., ry - half));
    }
    return bounds.adjusted(-half, -half, half, half).contains(point)
        && (point.x() <= bounds.left() + half || point.x() >= bounds.right() - half
            || point.y() <= bounds.top() + half || point.y() >= bounds.bottom() - half);
}
int Canvas::hitTest(QPointF point) const
{
    if (tool == Tool::Pen || tool == Tool::Select) return -1;
    for (int i = strokes.size() - 1; i >= 0; --i)
        if (!strokes[i].undone && (handleAt(strokes[i], point) >= 0 || hits(strokes[i], point))) return i;
    return -1;
}
void Canvas::drawHandles(QPainter& painter, const Stroke& stroke) const
{
    painter.setBrush(Qt::NoBrush);
    if (stroke.tool == Tool::Text) {
        painter.setPen(QPen(stroke.style.color, stroke.ratio, Qt::DashLine));
        painter.drawRect(stroke.bounds());
        return;
    }
    painter.setPen(QPen(Qt::black, stroke.ratio));
    const qreal radius = 3 * stroke.ratio;
    for (const auto point : handles(stroke))
        painter.drawRect(QRectF(point - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)));
}
