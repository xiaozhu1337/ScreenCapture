#include "Canvas.h"
#include <QApplication>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

Canvas::Canvas(QImage value, QWidget* parent) : QWidget(parent), image(std::move(value))
{
    image.setDevicePixelRatio(1);
    setMinimumSize(240, 160);
    setMouseTracking(true);
    setTool(Tool::Select);
}

QRect Canvas::imageRect() const
{
    QSize size = image.size();
    size.scale(this->size(), Qt::KeepAspectRatio);
    size = size.expandedTo(QSize(1, 1));
    return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

QPointF Canvas::imagePoint(QPointF point) const
{
    const auto area = imageRect();
    return QPointF(qBound(0., (point.x() - area.x()) * image.width() / area.width(), double(image.width() - 1)),
                   qBound(0., (point.y() - area.y()) * image.height() / area.height(), double(image.height() - 1)));
}

void Canvas::drawStroke(QPainter& painter, const Stroke& stroke) const
{
    painter.setPen(QPen(stroke.color, stroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QRectF rect = QRectF(stroke.start, stroke.end).normalized();
    switch (stroke.tool) {
    case Tool::Pen:
        if (stroke.path.isEmpty()) painter.drawPoint(stroke.start);
        else painter.drawPath(stroke.path);
        break;
    case Tool::Rectangle: painter.drawRect(rect); break;
    case Tool::Ellipse: painter.drawEllipse(rect); break;
    case Tool::Arrow: {
        painter.drawLine(stroke.start, stroke.end);
        const double angle = std::atan2(stroke.end.y() - stroke.start.y(), stroke.end.x() - stroke.start.x());
        const double length = qMax(12, stroke.width * 4);
        for (const double offset : {-0.5, 0.5})
            painter.drawLine(stroke.end, stroke.end - QPointF(std::cos(angle + offset), std::sin(angle + offset)) * length);
        break;
    }
    case Tool::Text: {
        auto font = painter.font();
        font.setPixelSize(qMax(16, stroke.width * 6));
        painter.setFont(font);
        painter.drawText(stroke.start, stroke.text);
        break;
    }
    default: break;
    }
}

QImage Canvas::result() const
{
    QImage output = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& stroke : strokes) drawStroke(painter, stroke);
    painter.end();
    return selection.isEmpty() ? output : output.copy(selection);
}

void Canvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(40, 40, 40));
    const auto area = imageRect();
    painter.translate(area.topLeft());
    painter.scale(double(area.width()) / image.width(), double(area.height()) / image.height());
    painter.drawImage(QPoint(), image);
    painter.setClipRect(image.rect());
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& stroke : strokes) drawStroke(painter, stroke);
    if (dragging && tool != Tool::Select) drawStroke(painter, current);
    if (!selection.isEmpty()) {
        QPainterPath shade;
        shade.addRect(QRectF(image.rect()));
        shade.addRect(QRectF(selection));
        painter.fillPath(shade, QColor(0, 0, 0, 120));
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        painter.drawRect(selection);
    }
}

void Canvas::setTool(Tool value)
{
    tool = value;
    dragging = false;
    setCursor(tool == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
    update();
}

void Canvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        selection = {};
        update();
        return;
    }
    if (event->button() != Qt::LeftButton || !imageRect().contains(event->position().toPoint())) return;
    origin = imagePoint(event->position());
    mouseOrigin = event->position();
    selectionStarted = false;
    current = {tool, {}, origin, origin, color, lineWidth, {}};
    current.path.moveTo(origin);
    if (tool == Tool::Text) {
        bool ok = false;
        current.text = QInputDialog::getText(this, tr("文本标注"), tr("输入文本"), QLineEdit::Normal, {}, &ok);
        if (ok && !current.text.isEmpty()) { strokes.append(current); undone.clear(); }
    } else dragging = true;
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging) return;
    current.end = imagePoint(event->position());
    if (tool == Tool::Select) {
        if (!selectionStarted && (event->position() - mouseOrigin).manhattanLength() < QApplication::startDragDistance()) return;
        selectionStarted = true;
        const auto area = imageRect();
        const QRectF bounds = QRectF(mouseOrigin, event->position()).normalized();
        const QPoint first(std::floor((bounds.left() - area.left()) * image.width() / area.width()),
                           std::floor((bounds.top() - area.top()) * image.height() / area.height()));
        const QPoint last(std::ceil((bounds.right() - area.left() + 1) * image.width() / area.width()) - 1,
                          std::ceil((bounds.bottom() - area.top() + 1) * image.height() / area.height()) - 1);
        selection = QRect(first, last).intersected(image.rect());
    } else if (tool == Tool::Pen) current.path.lineTo(current.end);
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !dragging) return;
    mouseMoveEvent(event);
    dragging = false;
    if (tool != Tool::Select) { strokes.append(current); undone.clear(); }
    update();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && tool == Tool::Select && copied) copied();
}

void Canvas::crop()
{
    if (selection.isEmpty()) return;
    image = result();
    selection = {};
    strokes.clear();
    undone.clear();
    update();
}

void Canvas::undo()
{
    if (!strokes.isEmpty()) undone.append(strokes.takeLast());
    update();
}

void Canvas::redo()
{
    if (!undone.isEmpty()) strokes.append(undone.takeLast());
    update();
}
