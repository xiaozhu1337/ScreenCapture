#include "Canvas.h"
#include "Ui.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <cmath>
#include <numbers>

void Canvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        finishText();
        if (rightClicked) rightClicked();
        else { selection = {}; update(); }
        return;
    }
    if (event->button() != Qt::LeftButton || !imageRect().contains(event->position().toPoint())) return;
    const auto point = imagePoint(event->position());
    if (textEditor) {
        const int textHandle = handleAt(strokes[textIndex], point);
        if (textHandle == 1) { textEditor->setFocus(); return; }
        finishText();
        if (textHandle != 8) { setFocus(); return; }
    }
    setFocus();
    origin = lastPoint = point;
    mouseOrigin = event->position();
    lastGlobal = event->globalPosition();
    hasDragged = selectionStarted = editingExisting = lastInserted = false;
    if (tool == Tool::Select) { dragging = true; return; }
    hover = hitTest(point);
    if (hover >= 0) {
        active = hover;
        auto& stroke = strokes[active];
        handle = handleAt(stroke, point);
        if (handle < 0) handle = 8;
        if (stroke.tool == Tool::Text && handle == 1) { textInserted = false; editText(active); return; }
        anchor = point;
        const auto controls = handles(stroke);
        if (handle < controls.size()) {
            if (stroke.tool == Tool::Arrow) anchor = controls[1 - handle];
            else if (!stroke.freehand() && stroke.tool != Tool::Number) anchor = controls[(handle + 4) % 8];
        }
        editingExisting = dragging = true;
        updateCursor();
        return;
    }
    discardUndone();
    current = {};
    current.tool = tool;
    current.start = current.end = anchor = point;
    current.style = activeStyle;
    current.style.color = color;
    current.style.size = lineWidth;
    current.ratio = pixelRatio;
    current.preview = tool == Tool::Mosaic || tool == Tool::Eraser;
    if (current.freehand()) {
        current.points.append(point);
        if (event->modifiers().testFlag(Qt::ShiftModifier)) current.points.append(point);
        current.rebuildPath();
        handle = 1;
    } else handle = 4;
    if (tool == Tool::Number) {
        for (const auto& stroke : strokes)
            if (!stroke.undone && stroke.tool == Tool::Number) current.number = qMax(current.number, stroke.number + 1);
        current.radius = lineWidth * pixelRatio;
        handle = 0;
    }
    if (tool == Tool::Text) {
        active = strokes.size();
        strokes.append(current);
        textInserted = true;
        editText(active);
    } else dragging = true;
    update();
}
void Canvas::dragStroke(Stroke& stroke, QPointF point, bool shift)
{
    if (stroke.tool == Tool::Mosaic || stroke.tool == Tool::Eraser) stroke.preview = true;
    if (stroke.tool == Tool::Number) {
        if (handle == 0) stroke.translate(point - lastPoint);
        else if (handle == 1) {
            const auto delta = point - stroke.start;
            stroke.angle = -std::atan2(delta.y(), delta.x()) * 180 / std::numbers::pi;
        } else if (handle == 2) {
            const auto range = Ui::toolSize("number");
            const int size = qBound(range.minimum, qRound(qMax(8 * stroke.ratio,
                QLineF(point, stroke.start).length()) / stroke.ratio), range.maximum);
            stroke.radius = size * stroke.ratio;
            activeStyle.size = lineWidth = size;
            if (sizeChanged) sizeChanged("number", size);
        }
    } else if (handle == 8) stroke.translate(point - lastPoint);
    else if (stroke.freehand()) {
        if (shift) stroke.points[handle == 0 ? 0 : stroke.points.size() - 1] = point;
        else if (handle == 0) stroke.points.prepend(point);
        else stroke.points.append(point);
        stroke.rebuildPath();
    } else if (stroke.tool == Tool::Arrow) {
        if (shift) {
            const auto delta = point - anchor;
            const auto x = qAbs(delta.x()), y = qAbs(delta.y());
            if (y <= x * 0.41421356237) point.setY(anchor.y());
            else if (x <= y * 0.41421356237) point.setX(anchor.x());
            else point = anchor + QPointF(std::copysign(qMax(x, y), delta.x()), std::copysign(qMax(x, y), delta.y()));
        }
        if (handle == 0) stroke.start = point;
        else stroke.end = point;
    } else {
        auto bounds = stroke.bounds();
        if (handle % 2 == 0) bounds = QRectF(anchor, point).normalized();
        else if (handle == 1 || handle == 5) {
            bounds.setTop(qMin(anchor.y(), point.y()));
            bounds.setBottom(qMax(anchor.y(), point.y()));
        } else {
            bounds.setLeft(qMin(anchor.x(), point.x()));
            bounds.setRight(qMax(anchor.x(), point.x()));
        }
        if (shift) bounds.setSize(QSizeF(qMax(bounds.width(), bounds.height()), qMax(bounds.width(), bounds.height())));
        stroke.start = bounds.topLeft();
        stroke.end = bounds.bottomRight();
    }
}
void Canvas::mouseMoveEvent(QMouseEvent* event)
{
    const auto point = imagePoint(event->position());
    if (!dragging) {
        const int next = hitTest(point);
        const int nextHandle = next < 0 ? -1 : handleAt(strokes[next], point);
        if (hover != next || hoverHandle != nextHandle) {
            hover = next;
            hoverHandle = nextHandle;
            updateCursor();
            update();
        }
        return;
    }
    if (pinned && tool == Tool::Select) {
        const auto delta = (event->globalPosition() - lastGlobal).toPoint();
        lastGlobal = event->globalPosition();
        hasDragged |= !delta.isNull();
        if (moved && !delta.isNull()) moved(delta);
        return;
    }
    if (point == lastPoint) return;
    hasDragged = true;
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
    } else dragStroke(editingExisting ? strokes[active] : current, point, event->modifiers().testFlag(Qt::ShiftModifier));
    lastPoint = point;
    update();
}
void Canvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !dragging) return;
    mouseMoveEvent(event);
    dragging = false;
    if (mouseGrabber() == this) releaseMouse();
    if (tool == Tool::Select) {
        if (pinned && !hasDragged && clicked) clicked();
        update();
        return;
    }
    if (!editingExisting && (hasDragged || tool == Tool::Number)) {
        active = strokes.size();
        strokes.append(current);
        lastInserted = true;
    }
    if (editingExisting || lastInserted) {
        auto& stroke = strokes[active];
        stroke.preview = false;
        if (stroke.tool == Tool::Mosaic && hasDragged) buildMosaic(active);
        hover = hitTest(imagePoint(event->position()));
        hoverHandle = hover < 0 ? -1 : handleAt(strokes[hover], imagePoint(event->position()));
    }
    editingExisting = false;
    updateCursor();
    update();
}
void Canvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || textEditor) return;
    if (lastInserted && !strokes.isEmpty() && strokes.last().tool == Tool::Number) strokes.removeLast();
    lastInserted = false;
    dragging = editingExisting = false;
    hover = hoverHandle = -1;
    if (copied) copied();
    update();
}
void Canvas::wheelEvent(QWheelEvent* event)
{
    if (dragging) return;
    wheelRemainder += event->angleDelta().y();
    const int steps = wheelRemainder / 120;
    wheelRemainder %= 120;
    if (!steps) return;
    if (tool == Tool::Select) { if (zoomed) zoomed(steps, event->position().toPoint()); return; }
    const auto id = toolId(tool);
    const auto range = Ui::toolSize(id);
    activeStyle.size = lineWidth = qBound(range.minimum, lineWidth + steps, range.maximum);
    if (textEditor && tool == Tool::Text) { strokes[textIndex].style.size = lineWidth; layoutText(); }
    if (sizeChanged) sizeChanged(id, lineWidth);
    updateCursor();
    update();
}
void Canvas::keyPressEvent(QKeyEvent* event)
{
    if (textEditor) { event->accept(); return; }
    if (event->key() == Qt::Key_Delete && !dragging && hover >= 0) {
        strokes.removeAt(hover);
        hover = hoverHandle = -1;
        lastInserted = false;
        updateCursor();
        update();
    } else QWidget::keyPressEvent(event);
}
