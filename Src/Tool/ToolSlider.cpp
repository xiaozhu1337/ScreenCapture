#include "pch.h"
#include "ToolSlider.h"

ToolSlider::ToolSlider(Ling::WinBase* win) : Ling::Node(win)
{
    setSize(80.f, 20.f);
    shapes = win->compositor.CreateShapeVisual();
    track = win->compositor.CreateRoundedRectangleGeometry();
    thumb = win->compositor.CreateEllipseGeometry();
    trackShape = win->compositor.CreateSpriteShape(track);
    thumbShape = win->compositor.CreateSpriteShape(thumb);
    auto brush = win->compositor.CreateColorBrush(Ling::Color(0x888888FF).getUIColor());
    trackShape.FillBrush(brush);
    thumbShape.FillBrush(brush);
    shapes.Shapes().Append(trackShape);
    shapes.Shapes().Append(thumbShape);
    visual.Children().InsertAtTop(shapes);

    auto weakThis = getWeakThis();
    downToken = win->onMouseDown.add([this, weakThis](POINT pos, bool right) {
        if (!weakThis.lock() || right || !visual.IsVisible() || !isPosIn(pos)) return;
        SetCapture(this->win->hwnd);
        dragging = true;
        setValue(getValueAt(static_cast<float>(pos.x)));
    });
    moveToken = win->onMouseMove.add([this, weakThis](POINT pos) {
        if (!weakThis.lock() || !dragging) return;
        if (GetCapture() != this->win->hwnd) { dragging = false; return; }
        setValue(getValueAt(static_cast<float>(pos.x)));
    });
    upToken = win->onMouseUp.add([this, weakThis](POINT, bool) {
        if (!weakThis.lock() || !dragging) return;
        dragging = false;
        if (GetCapture() == this->win->hwnd) ReleaseCapture();
    });
    wheelToken = win->onMouseWheel.add([this, weakThis](POINT pos, float space) {
        if (!weakThis.lock() || !visual.IsVisible() || !isPosIn(pos)) return;
        const float delta = step > 0.f ? step : (maxValue - minValue) / 20.f;
        setValue(value + space / (60.f * this->win->dpi) * delta);
    });
}

ToolSlider::~ToolSlider()
{
    win->onMouseDown.remove(downToken);
    win->onMouseMove.remove(moveToken);
    win->onMouseUp.remove(upToken);
    win->onMouseWheel.remove(wheelToken);
    if (dragging && GetCapture() == win->hwnd) ReleaseCapture();
}

float ToolSlider::snap(float next) const
{
    if (maxValue <= minValue) return minValue;
    next = std::clamp(next, minValue, maxValue);
    if (step > 0.f) next = minValue + std::round((next - minValue) / step) * step;
    return std::clamp(next, minValue, maxValue);
}

void ToolSlider::setRange(float min, float max)
{
    minValue = min;
    maxValue = max;
    const float old = value;
    value = snap(value);
    syncVisual();
    if (value != old) onValueChanged(this, value);
}

void ToolSlider::setValue(float next)
{
    next = snap(next);
    if (next == value) return;
    value = next;
    syncVisual();
    onValueChanged(this, value);
}

void ToolSlider::setStep(float next)
{
    step = next;
    setValue(value);
}

void ToolSlider::setSizePreview(bool enabled)
{
    sizePreview = enabled;
    syncVisual();
}

float ToolSlider::maxRadius() const
{
    const float available = std::max(0.f, std::min(w, h) * 0.5f - win->dpi);
    return std::min((sizePreview ? 12.f : 3.f) * win->dpi, available);
}

float ToolSlider::inset() const
{
    // Reserve the largest dot throughout a drag so the value mapping never shifts.
    return maxRadius() + (sizePreview ? win->dpi : 0.f);
}

float ToolSlider::getValueAt(float px) const
{
    const float padding = inset(), length = w - 2.f * padding;
    if (length <= 0.f || maxValue <= minValue) return minValue;
    return snap(minValue + std::clamp((px - x - padding) / length, 0.f, 1.f) * (maxValue - minValue));
}

void ToolSlider::layout()
{
    Ling::Node::layout();
    syncVisual();
}

void ToolSlider::syncVisual()
{
    if (w <= 0.f || h <= 0.f) return;
    const float padding = inset(), length = w - 2.f * padding;
    if (length <= 0.f) return;
    const float ratio = maxValue > minValue ? (value - minValue) / (maxValue - minValue) : 0.f;
    const float largest = maxRadius(), smallest = std::min(2.f * win->dpi, largest);
    const float radius = sizePreview ? smallest + ratio * (largest - smallest) : largest;
    shapes.Size({ w, h });
    track.Size({ length, win->dpi });
    track.CornerRadius({ win->dpi * 0.5f, win->dpi * 0.5f });
    trackShape.Offset({ padding, (h - win->dpi) * 0.5f });
    thumb.Radius({ radius, radius });
    thumbShape.Offset({ padding + ratio * length, h * 0.5f });
}
