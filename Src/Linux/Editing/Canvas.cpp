#include "Canvas.h"
#include <QApplication>
#include <QPainter>
#include <QPlainTextEdit>
#include <cmath>

Canvas::Canvas(QImage value, QWidget* parent) : QWidget(parent), image(std::move(value))
{
    image.setDevicePixelRatio(1);
    setMinimumSize(1, 1);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setTool(Tool::Select);
}
QRectF Canvas::imageRect() const
{
    if (pinned) {
        const QRectF area = pinnedArea.isValid() ? pinnedArea : QRectF(rect());
        const qreal scale = qMin(area.width() / image.width(), area.height() / image.height());
        return QRectF(area.topLeft(), QSizeF(image.size()) * scale);
    }
    QSize size = image.size();
    size.scale(this->size(), Qt::KeepAspectRatio);
    size = size.expandedTo(QSize(1, 1));
    return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}
void Canvas::setPinnedArea(QRectF area)
{
    pinnedArea = area;
    layoutText();
    updateCursor();
    update();
}
QPointF Canvas::imagePoint(QPointF point) const
{
    const auto area = imageRect();
    return {qBound(0., (point.x() - area.x()) * image.width() / area.width(), double(image.width() - 1)),
            qBound(0., (point.y() - area.y()) * image.height() / area.height(), double(image.height() - 1))};
}
Canvas::Tool Canvas::fromId(const QString& id)
{
    if (id == "line" || id == "pen") return Tool::Pen;
    if (id == "rect" || id == "rectangle") return Tool::Rectangle;
    if (id == "ellipse") return Tool::Ellipse;
    if (id == "arrow") return Tool::Arrow;
    if (id == "number") return Tool::Number;
    if (id == "text") return Tool::Text;
    if (id == "mosaic") return Tool::Mosaic;
    if (id == "eraser") return Tool::Eraser;
    return Tool::Select;
}
QString Canvas::toolId(Tool value)
{
    switch (value) {
    case Tool::Pen: return "line";
    case Tool::Rectangle: return "rect";
    case Tool::Ellipse: return "ellipse";
    case Tool::Arrow: return "arrow";
    case Tool::Number: return "number";
    case Tool::Text: return "text";
    case Tool::Mosaic: return "mosaic";
    case Tool::Eraser: return "eraser";
    default: return {};
    }
}
void Canvas::setTool(Tool value)
{
    finishText();
    tool = value;
    dragging = editingExisting = false;
    active = hover = hoverHandle = -1;
    wheelRemainder = 0;
    updateCursor();
    update();
}
void Canvas::setStyle(const ToolStyle& value)
{
    activeStyle = value;
    color = value.color;
    lineWidth = value.size;
    if (textEditor && textIndex >= 0) {
        strokes[textIndex].style = value;
        layoutText();
    }
    updateCursor();
    update();
}
void Canvas::discardUndone()
{
    while (!strokes.isEmpty() && strokes.last().undone) strokes.removeLast();
    hover = hoverHandle = -1;
}
void Canvas::undo()
{
    finishText();
    if (dragging) return;
    for (int i = strokes.size() - 1; i >= 0; --i) {
        if (strokes[i].undone) continue;
        strokes[i].undone = true;
        break;
    }
    hover = hoverHandle = -1;
    lastInserted = false;
    updateCursor();
    update();
}
void Canvas::redo()
{
    finishText();
    if (dragging) return;
    for (auto& stroke : strokes) {
        if (!stroke.undone) continue;
        stroke.undone = false;
        break;
    }
    hover = hoverHandle = -1;
    lastInserted = false;
    updateCursor();
    update();
}
QImage Canvas::result() const
{
    const_cast<Canvas*>(this)->finishText();
    QImage output = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& stroke : strokes) if (!stroke.undone) drawStroke(painter, stroke);
    painter.end();
    return selection.isEmpty() ? output : output.copy(selection);
}
void Canvas::crop()
{
    if (selection.isEmpty()) return;
    finishText();
    image = result();
    selection = {};
    strokes.clear();
    active = hover = hoverHandle = -1;
    update();
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
    for (int i = 0; i < strokes.size(); ++i)
        if (i != textIndex && !strokes[i].undone) drawStroke(painter, strokes[i]);
    if (dragging && !editingExisting && tool != Tool::Select) drawStroke(painter, current);
    if (hover >= 0 && hover < strokes.size() && !dragging && !textEditor) drawHandles(painter, strokes[hover]);
    if (textEditor && textIndex >= 0) drawHandles(painter, strokes[textIndex]);
    if (!selection.isEmpty()) {
        QPainterPath shade;
        shade.addRect(QRectF(image.rect()));
        shade.addRect(QRectF(selection));
        painter.fillPath(shade, QColor(0, 0, 0, 120));
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        painter.drawRect(selection);
    }
    if (pinned) {
        painter.resetTransform();
        painter.setPen(QPen(QColor("#1677ff"), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}
void Canvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutText();
    updateCursor();
}
void Canvas::leaveEvent(QEvent* event)
{
    if (!dragging) { hover = hoverHandle = -1; updateCursor(); update(); }
    QWidget::leaveEvent(event);
}
void Canvas::updateCursor()
{
    if (hover >= 0 && hover < strokes.size() && tool != Tool::Pen && tool != Tool::Select) {
        const auto& stroke = strokes[hover];
        if (stroke.tool == Tool::Text) {
            setCursor(hoverHandle == 8 ? Qt::SizeAllCursor : Qt::IBeamCursor);
        } else if (hoverHandle < 0 || stroke.freehand() || stroke.tool == Tool::Arrow || stroke.tool == Tool::Number) {
            setCursor(Qt::SizeAllCursor);
        } else {
            const Qt::CursorShape cursors[]{Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
            setCursor(cursors[hoverHandle % 4]);
        }
        return;
    }
    if (tool == Tool::Pen) {
        const qreal diameter = qMax(3., std::ceil(lineWidth * pixelRatio * imageRect().width() / image.width()));
        const int extent = (qMin(1024, int(diameter)) + 4) | 1;
        const qreal dpr = devicePixelRatioF();
        QPixmap pixels(QSize(extent, extent) * dpr);
        pixels.setDevicePixelRatio(dpr);
        pixels.fill(Qt::transparent);
        QPainter painter(&pixels);
        painter.setRenderHint(QPainter::Antialiasing);
        const int brightness = 299 * color.red() + 587 * color.green() + 114 * color.blue();
        painter.setPen(QPen(brightness >= 128000 ? Qt::black : Qt::white, 1));
        QColor ink = color;
        if (activeStyle.transparent) ink.setAlphaF(0.5);
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(extent / 2, extent / 2), diameter / 2., diameter / 2.);
        painter.end();
        setCursor(QCursor(pixels, extent / 2, extent / 2));
    } else setCursor(tool == Tool::Text ? Qt::IBeamCursor : tool == Tool::Select && pinned ? Qt::ArrowCursor : Qt::CrossCursor);
}
