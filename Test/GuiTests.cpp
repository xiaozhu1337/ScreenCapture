#include "pch.h"
#include "Setting.h"
#include "Lang.h"
#include "History.h"
#include "Win/WinCap.h"
#include "Win/WinPin.h"
#include "Win/PinCanvas.h"
#include "Win/WinSetting.h"
#include "Win/Shortcut.h"
#include "Tool/ToolCap.h"
#include "Tool/ToolMain.h"
#include "Tool/ToolSub.h"
#include "Tool/ToolSlider.h"
#include "Tool/ToolRegistry.h"
#include "Shape/ShapeLine.h"
#include <array>
#include <fstream>
#include <stdexcept>

namespace {
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    class ScopedShift
    {
    public:
        ScopedShift()
        {
            require(GetKeyboardState(previous.data()), "Keyboard state must be available");
            auto pressed = previous;
            pressed[VK_SHIFT] |= 0x80;
            require(SetKeyboardState(pressed.data()), "Shift state must be available");
        }
        ~ScopedShift() { SetKeyboardState(previous.data()); }
    private:
        std::array<BYTE, 256> previous{};
    };

    void pump()
    {
        const auto until = GetTickCount64() + 200;
        MSG message;
        do {
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessage(&message);
            }
            Sleep(1);
        } while (GetTickCount64() < until);
    }

    void waitForNextStroke()
    {
        // Use the same clock as double-click detection; Sleep alone can round to its boundary.
        const auto until = GetTickCount64() + GetDoubleClickTime() + 1;
        while (GetTickCount64() <= until) Sleep(1);
    }

    HCURSOR requestCursor(WinPin* pin)
    {
        SendMessage(pin->hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(pin->hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        return GetCursor();
    }

    void wheel(WinPin* pin, POINT pos, short delta)
    {
        ClientToScreen(pin->hwnd, &pos);
        SendMessage(pin->hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)), MAKELPARAM(pos.x, pos.y));
    }

    int cursorSize(HCURSOR cursor)
    {
        ICONINFO info{};
        require(GetIconInfo(cursor, &info), "Cursor bitmap must be available");
        BITMAP bitmap{};
        const auto result = GetObject(info.hbmColor, sizeof(bitmap), &bitmap);
        if (info.hbmColor) DeleteObject(info.hbmColor);
        if (info.hbmMask) DeleteObject(info.hbmMask);
        require(result && !info.fIcon && bitmap.bmWidth == bitmap.bmHeight,
            "Line cursor must be a square color cursor");
        require(info.xHotspot == bitmap.bmWidth / 2 && info.yHotspot == bitmap.bmHeight / 2,
            "The line cursor hotspot must stay at the dot center");
        return bitmap.bmWidth;
    }

    DWORD cursorCenterColor(HCURSOR cursor)
    {
        ICONINFO info{};
        require(GetIconInfo(cursor, &info), "Cursor pixels must be available");
        BITMAP bitmap{};
        GetObject(info.hbmColor, sizeof(bitmap), &bitmap);
        BITMAPINFO header{};
        header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        header.bmiHeader.biWidth = bitmap.bmWidth;
        header.bmiHeader.biHeight = -bitmap.bmHeight;
        header.bmiHeader.biPlanes = 1;
        header.bmiHeader.biBitCount = 32;
        header.bmiHeader.biCompression = BI_RGB;
        std::vector<DWORD> pixels(bitmap.bmWidth * bitmap.bmHeight);
        auto dc = GetDC(nullptr);
        const auto result = GetDIBits(dc, info.hbmColor, 0, bitmap.bmHeight, pixels.data(), &header, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
        require(result == bitmap.bmHeight, "Cursor color bitmap must be readable");
        return pixels[(bitmap.bmHeight / 2) * bitmap.bmWidth + bitmap.bmWidth / 2] & 0xFFFFFF;
    }

    template<typename T> T* findWindow()
    {
        for (auto window : Ling::App::get()->windows) {
            if (auto found = dynamic_cast<T*>(window); found && IsWindow(found->hwnd)) return found;
        }
        return nullptr;
    }

    Ling::Button* findButton(Ling::Node* node, const std::wstring& id)
    {
        if (auto button = dynamic_cast<Ling::Button*>(node); button && button->id == id) return button;
        for (auto& child : node->children) {
            if (auto found = findButton(child.get(), id)) return found;
        }
        return nullptr;
    }

    ToolSlider* findSlider(Ling::Node* node)
    {
        if (auto slider = dynamic_cast<ToolSlider*>(node)) return slider;
        for (auto& child : node->children) {
            if (auto slider = findSlider(child.get())) return slider;
        }
        return nullptr;
    }

    auto sliderThumb(ToolSlider* slider)
    {
        using namespace winrt::Windows::UI::Composition;
        return slider->visual.Children().First().Current().as<ShapeVisual>().Shapes().GetAt(1).as<CompositionSpriteShape>();
    }

    void click(Ling::Node* node, const std::wstring& id)
    {
        auto button = findButton(node, id);
        require(button != nullptr, "Expected UI button is missing");
        require(button->w > 0 && button->h > 0 && button->x >= 0 && button->y >= 0
            && button->x + button->w <= button->win->w && button->y + button->h <= button->win->h,
            "Clicked UI button must be visible inside its window");
        POINT point{ static_cast<LONG>(button->x + button->w / 2), static_cast<LONG>(button->y + button->h / 2) };
        button->win->onMouseDown(point, false);
        button->win->onMouseUp(point, false);
    }

    void selectRegion(WinCap* cap, const RECT& region)
    {
        POINT start{ region.left - cap->x, region.top - cap->y };
        POINT end{ region.right - cap->x, region.bottom - cap->y };
        cap->onMouseDown(start, false);
        cap->onMouseMove(end);
        cap->onMouseUp(end, false);
        pump();
    }

    void snapshot(Ling::WinBase* window, const wchar_t* name)
    {
        pump();
        RECT rect;
        GetWindowRect(window->hwnd, &rect);
        const int width = rect.right - rect.left, height = rect.bottom - rect.top;
        HDC screen = GetDC(nullptr), buffer = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
        auto old = SelectObject(buffer, bitmap);
        BitBlt(buffer, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY);
        SelectObject(buffer, old);
        BITMAPINFO info{};
        info.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB };
        std::vector<BYTE> pixels(width * height * 4);
        GetDIBits(buffer, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        ReleaseDC(nullptr, screen);
        BITMAPFILEHEADER header{ 0x4D42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + pixels.size()),
            0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) };
        auto directory = std::filesystem::current_path() / L"__tmp";
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / name, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
        output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    }

    void openTab(WinSetting* settings, size_t index)
    {
        auto button = dynamic_cast<Ling::Button*>(settings->body->children[0]->children[index].get());
        require(button != nullptr, "Settings navigation button is missing");
        button->onClick(button);
        pump();
    }

    std::vector<BYTE> renderShape(ShapeBase* shape, D2D1_SIZE_U size)
    {
        auto ctx = Ling::D2D::get()->deviceContext;
        Microsoft::WRL::ComPtr<ID2D1Image> previous;
        ctx->GetTarget(previous.GetAddressOf());
        D2D1_MATRIX_3X2_F transform;
        ctx->GetTransform(&transform);
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> target, readable;
        auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        winrt::check_hresult(ctx->CreateBitmap(size, nullptr, 0, properties, target.GetAddressOf()));
        properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        winrt::check_hresult(ctx->CreateBitmap(size, nullptr, 0, properties, readable.GetAddressOf()));
        ctx->SetTarget(target.Get());
        ctx->SetTransform(D2D1::Matrix3x2F::Identity());
        ctx->BeginDraw();
        ctx->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
        shape->paint(ctx.Get());
        auto result = ctx->EndDraw();
        ctx->SetTarget(previous.Get());
        ctx->SetTransform(transform);
        winrt::check_hresult(result);
        winrt::check_hresult(readable->CopyFromBitmap(nullptr, target.Get(), nullptr));
        D2D1_MAPPED_RECT mapped{};
        winrt::check_hresult(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped));
        std::vector<BYTE> pixels(size.width * size.height * 4);
        for (UINT y = 0; y < size.height; ++y) {
            std::copy_n(mapped.bits + y * mapped.pitch, size.width * 4, pixels.data() + y * size.width * 4);
        }
        winrt::check_hresult(readable->Unmap());
        return pixels;
    }
}

void testGuiFeatures()
{
    require(Shortcut::currentModifiers() == 0, "GUI tests require no held modifier keys");
    auto cap = WinCap::get();
    require(cap && IsWindow(cap->hwnd), "Capture window must open");
    cap->onKeyDown(VK_F3);
    require(!WinPin::hasWindow(), "Pin shortcut must not pin before selecting a region");
    Setting::get()->setShortcutKey(L"pin", { L"F8" });
    RECT region{ cap->x + 180, cap->y + 120, cap->x + 780, cap->y + 440 };
    selectRegion(cap, region);
    cap->onKeyDown(VK_F3);
    require(!WinPin::hasWindow(), "Old pin shortcut must stop working after reconfiguration");
    cap->onKeyDown(VK_F8);
    cap->onKeyDown(VK_F8);
    auto pin = findWindow<WinPin>();
    require(pin && pin->screenImg, "Custom pin shortcut must create a pin window with captured pixels");
    require(pin->screenImg->GetPixelSize().width == 600, "Pinned pixels must match the selected width");
    size_t pinCount = 0;
    for (auto window : Ling::App::get()->windows) if (dynamic_cast<WinPin*>(window)) ++pinCount;
    require(pinCount == 1, "Repeated key events must not create duplicate pins");
    pump();
    const auto originalImage = pin->screenImg.Get();
    auto pinCanvas = dynamic_cast<PinCanvas*>(pin->body->children.front().get());
    require(pinCanvas && pinCanvas->visual.Brush(), "Pin canvas must display a presented frame");
    const auto originalSurface = pinCanvas->visual.Brush();
    POINT anchor{ 90, 60 }, screenAnchor = anchor;
    ClientToScreen(pin->hwnd, &screenAnchor);
    wheel(pin, anchor, WHEEL_DELTA);
    require(std::abs(pin->scale - 1.1f) < 0.0001f, "Plain wheel up must enlarge a pin without Ctrl");
    require(pinCanvas->visual.Brush() == originalSurface,
        "Small zoom changes must reuse the displayed buffer without resizing or clearing its surface");
    RECT zoomedRect{};
    GetWindowRect(pin->hwnd, &zoomedRect);
    require(zoomedRect.right - zoomedRect.left == 660, "Wheel zoom must resize the pin window");
    require(std::abs(zoomedRect.left + std::lround(anchor.x * pin->scale) - screenAnchor.x) <= 1
        && std::abs(zoomedRect.top + std::lround(anchor.y * pin->scale) - screenAnchor.y) <= 1,
        "Zoom must preserve the image point under the pointer");
    require(pin->screenImg.Get() == originalImage && pin->screenImg->GetPixelSize().width == 600,
        "Zoom must keep the original image pixels and dimensions");
    wheel(pin, POINT{ 99, 66 }, -WHEEL_DELTA);
    require(std::abs(pin->scale - 1.f) < 0.0001f, "Wheel down must reverse wheel up");
    wheel(pin, anchor, WHEEL_DELTA / 2);
    require(std::abs(pin->scale - std::sqrt(1.1f)) < 0.0001f, "High-resolution wheel deltas must preserve partial steps");
    wheel(pin, anchor, -WHEEL_DELTA / 2);
    require(std::abs(pin->scale - 1.f) < 0.0001f, "Partial wheel steps must reverse symmetrically");
    wheel(pin, anchor, 0);
    require(std::abs(pin->scale - 1.f) < 0.0001f, "Zero wheel delta must not zoom");
    for (int i = 0; i < 8; ++i) {
        wheel(pin, anchor, WHEEL_DELTA);
        require(pinCanvas->visual.Brush() != nullptr, "Growing the buffer must always leave a presented frame attached");
    }
    const auto grownSurface = pinCanvas->visual.Brush();
    require(grownSurface != originalSurface, "Zoom beyond buffer capacity must publish a new painted surface");
    for (int i = 0; i < 8; ++i) {
        wheel(pin, anchor, -WHEEL_DELTA);
        require(pinCanvas->visual.Brush() == grownSurface,
            "Continuous shrinking must retain buffer capacity and avoid rebuilding the displayed surface");
    }
    require(std::abs(pin->scale - 1.f) < 0.0001f && pin->screenImg.Get() == originalImage,
        "Continuous zoom must preserve the original image and reverse symmetrically");
    pin->close();
    pump();

    Setting::get()->setCaptureToolbar({ L"line", L"mark" });
    Setting::get()->setToolbarBottom(true);
    WinCap::init();
    cap = WinCap::get();
    MONITORINFO info{ sizeof(MONITORINFO) };
    GetMonitorInfo(MonitorFromWindow(cap->hwnd, MONITOR_DEFAULTTOPRIMARY), &info);
    auto work = info.rcWork;
    region = { work.left + 80, work.bottom - 240, std::min(work.left + 680, work.right - 20), work.bottom - 8 };
    SetForegroundWindow(cap->hwnd);
    require(GetForegroundWindow() == cap->hwnd, "Escape regression test must start in the foreground");
    selectRegion(cap, region);
    auto toolbar = findWindow<ToolCap>();
    require(toolbar && findButton(toolbar->body.get(), L"line"), "Configured line must appear in the first toolbar");
    require(!findButton(toolbar->body.get(), L"video"), "Removed menu item must disappear");
    require(toolbar->y >= region.top && toolbar->y + toolbar->h <= work.bottom,
        "Capture toolbar near screen bottom must stay inside the bottom of the selection");
    snapshot(toolbar, L"gui-capture-toolbar.bmp");
    click(toolbar->body.get(), L"line");
    pump();
    pin = findWindow<WinPin>();
    require(pin && pin->toolMain->curId == L"line" && pin->toolSub->hasContent(),
        "Promoted line must select line and display its style controls");
    require(pin->toolMain->y >= region.top && pin->toolSub->y + pin->toolSub->h <= work.bottom,
        "Main and sub toolbar group must stay visible at the bottom");
    require(GetFocus() == pin->hwnd || GetFocus() == pin->toolMain->hwnd,
        "Selecting line must keep keyboard focus on a window that handles Escape");
    require(GetForegroundWindow() == pin->hwnd || GetForegroundWindow() == pin->toolMain->hwnd,
        "Escape regression test must own the foreground window");
    const auto emptyPin = pin->hwnd;
    INPUT escape[2]{};
    for (auto& input : escape) {
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_ESCAPE;
    }
    escape[1].ki.dwFlags = KEYEVENTF_KEYUP;
    require(SendInput(2, escape, sizeof(INPUT)) == 2, "Escape input must reach the foreground window");
    pump();
    require(!IsWindow(emptyPin), "Escape must close the new line window before drawing any strokes");
    WinCap::init();
    cap = WinCap::get();
    selectRegion(cap, region);
    toolbar = findWindow<ToolCap>();
    require(toolbar != nullptr, "Capture toolbar must reopen after Escape");
    click(toolbar->body.get(), L"line");
    pump();
    pin = findWindow<WinPin>();
    require(pin != nullptr, "Line window must reopen after Escape");
    const auto originalLineWidth = pin->toolSub->getSliderVal();
    pin->toolSub->setShapeSliderVal(L"line", 2.f * pin->toolSub->dpi);
    auto thinCursor = requestCursor(pin);
    require(thinCursor && thinCursor != LoadCursor(nullptr, IDC_CROSS), "Line drawing must use a dot cursor");
    const auto thinCursorSize = cursorSize(thinCursor);
    require(requestCursor(pin) == thinCursor, "An unchanged line width must reuse its cursor");
    pin->toolSub->setShapeSliderVal(L"line", 16.f * pin->toolSub->dpi);
    require(cursorSize(requestCursor(pin)) > thinCursorSize, "Increasing line width must enlarge the dot cursor");
    const auto originalColor = pin->toolSub->getSelectedColorValue();
    const std::array<std::wstring, 5> penColors{ L"red", L"green", L"blue", L"black", L"white" };
    for (const auto& colorName : penColors) {
        click(pin->toolSub->body.get(), L"color-" + colorName);
        const auto color = pin->toolSub->getSelectedColorValue();
        const auto cursor = requestCursor(pin);
        require(cursorCenterColor(cursor) == (color >> 8), "The line cursor must match the selected pen color");
        require(requestCursor(pin) == cursor, "An unchanged width and color must reuse the native cursor");
    }
    click(pin->toolSub->body.get(), L"color-red");
    require(pin->toolSub->getSelectedColorValue() == originalColor, "Restoring the palette selection must restore the pen color");
    auto widthSlider = findSlider(pin->toolSub->body.get());
    require(widthSlider != nullptr, "Line controls must contain a width slider");
    float previousRadius = 0.f;
    for (const float width : { 1.f, 30.f, 60.f }) {
        pin->toolSub->setShapeSliderVal(L"line", width * pin->toolSub->dpi);
        const auto thumb = sliderThumb(widthSlider);
        const auto radius = thumb.Geometry().as<winrt::Windows::UI::Composition::CompositionEllipseGeometry>().Radius();
        const auto center = thumb.Offset();
        require(radius.x > previousRadius && radius.x == radius.y,
            "The slider dot must grow throughout the full line-width range");
        const auto expectedDiameter = static_cast<int>(std::max(3.f, std::ceil(width * pin->toolSub->dpi * pin->scale)));
        require(radius.x * 2.f == expectedDiameter && cursorSize(requestCursor(pin)) == ((expectedDiameter + 4) | 1),
            "The slider dot and native cursor must use the same actual pixel diameter");
        require(center.x - radius.x >= 0.f && center.x + radius.x <= widthSlider->w
            && center.y - radius.y >= 0.f && center.y + radius.y <= widthSlider->h,
            "The slider dot must remain fully visible at both ends of the track");
        previousRadius = radius.x;
    }
    snapshot(pin->toolSub.get(), L"gui-line-controls-wide.bmp");
    const auto sliderY = static_cast<int>(widthSlider->y + widthSlider->h / 2.f);
    const auto sliderLeft = static_cast<int>(widthSlider->x + 1.f);
    const auto sliderMiddle = static_cast<int>(widthSlider->x + widthSlider->w / 2.f);
    const auto sliderRight = static_cast<int>(widthSlider->x + widthSlider->w + 20.f);
    const auto sliderWindow = pin->toolSub->hwnd;
    SendMessage(sliderWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(sliderLeft, sliderY));
    require(widthSlider->getValue() == 1.f && GetCapture() == sliderWindow,
        "Pressing the slider's left end must select minimum width and capture the mouse");
    SendMessage(sliderWindow, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(sliderMiddle, sliderY));
    require(std::abs(widthSlider->getValue() - 30.f) <= 1.f,
        "Dragging through the center must use a stable value mapping as the dot grows");
    SendMessage(sliderWindow, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(sliderRight, sliderY));
    require(widthSlider->getValue() == 60.f, "Dragging beyond the track must clamp to maximum line width");
    SendMessage(sliderWindow, WM_LBUTTONUP, 0, MAKELPARAM(sliderRight, sliderY));
    require(GetCapture() != sliderWindow, "Releasing the slider must release mouse capture");
    POINT sliderPoint{ sliderMiddle, sliderY };
    ClientToScreen(sliderWindow, &sliderPoint);
    SendMessage(sliderWindow, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
        MAKELPARAM(sliderPoint.x, sliderPoint.y));
    require(widthSlider->getValue() == 59.f && std::abs(pin->scale - 1.f) < 0.0001f,
        "Wheel input over the width slider must adjust pen width without zooming the pin");
    pin->toolSub->setShapeSliderVal(L"line", originalLineWidth);
    auto drawingCursor = requestCursor(pin);
    pin->onMouseDown(POINT{ 40, 40 }, false);
    require(requestCursor(pin) == drawingCursor, "The dot cursor must remain active while drawing a line");
    wheel(pin, POINT{ 40, 40 }, WHEEL_DELTA);
    require(std::abs(pin->scale - 1.f) < 0.0001f, "Wheel input must not change coordinates during a stroke");
    pin->onMouseMove(POINT{ 150, 100 });
    pin->onMouseUp(POINT{ 150, 100 }, false);
    require(pin->history->shapes.size() == 1 && dynamic_cast<ShapeLine*>(pin->history->shapes[0].get()),
        "Promoted line must support actual drawing");
    require(pin->shapeHover == nullptr, "Releasing the mouse must finish the active line stroke");
    auto firstLine = pin->history->shapes.front().get();
    const auto imageSize = pin->screenImg->GetPixelSize();
    const auto firstPixels = renderShape(firstLine, imageSize);
    require(std::any_of(firstPixels.begin(), firstPixels.end(), [](BYTE value) { return value != 0; }),
        "The completed line must render visible pixels");
    // No mouse move between release and the next press: a completed stroke must not remain active.
    waitForNextStroke();
    {
        ScopedShift shift;
        pin->onMouseDown(POINT{ 250, 40 }, false);
        pin->onMouseMove(POINT{ 320, 100 });
        pin->onMouseUp(POINT{ 320, 100 }, false);
    }
    require(pin->history->shapes.size() == 2, "Each mouse press must create a separate line stroke");
    require(renderShape(firstLine, imageSize) == firstPixels, "Drawing another stroke must not change the completed line");
    // A new stroke may start at an old endpoint and cross an old line.
    waitForNextStroke();
    pin->onMouseMove(POINT{ 150, 100 });
    pin->onMouseDown(POINT{ 150, 100 }, false);
    pin->onMouseMove(POINT{ 40, 40 });
    pin->onMouseUp(POINT{ 40, 40 }, false);
    require(pin->history->shapes.size() == 3, "Starting on an old line must draw a new independent stroke");
    require(renderShape(firstLine, imageSize) == firstPixels, "Overlapping strokes must leave the old line unchanged");
    waitForNextStroke();
    pin->onMouseDown(POINT{ 400, 140 }, false);
    pin->onMouseUp(POINT{ 400, 140 }, false);
    require(pin->history->shapes.size() == 3 && pin->history->shapes.front().get() == firstLine,
        "An empty stroke must be discarded without deleting a completed line");
    for (const auto& shape : pin->history->shapes) {
        require(!shape->isUndo, "A completed new stroke must be active in history");
    }
    pin->history->undo();
    require(pin->history->shapes.back()->isUndo && !firstLine->isUndo,
        "Undo must remove only the latest completed stroke");
    pin->history->redo();
    require(!pin->history->shapes.back()->isUndo, "Redo must restore the latest independent stroke");
    pin->toolSub->setShapeSliderVal(L"line", 8.f * pin->toolSub->dpi);
    const auto configuredWidth = pin->toolSub->getSliderVal();
    const auto normalCursorSize = cursorSize(requestCursor(pin));
    wheel(pin, POINT{ 200, 100 }, WHEEL_DELTA * 2);
    require(std::abs(pin->scale - 1.21f) < 0.0001f, "Wheel zoom must work with line selected and multiple notches");
    require(cursorSize(requestCursor(pin)) > normalCursorSize, "The dot cursor must follow the displayed stroke scale");
    const auto zoomedRadius = sliderThumb(widthSlider).Geometry()
        .as<winrt::Windows::UI::Composition::CompositionEllipseGeometry>().Radius().x;
    const auto zoomedDiameter = static_cast<int>(std::max(3.f, std::ceil(configuredWidth * pin->scale)));
    require(zoomedRadius * 2.f == zoomedDiameter && cursorSize(requestCursor(pin)) == ((zoomedDiameter + 4) | 1),
        "Zoom must keep the slider dot at the exact native cursor diameter");
    require(cursorCenterColor(requestCursor(pin)) == (originalColor >> 8), "Zoom must preserve the selected cursor color");
    require(pin->toolSub->getSliderVal() == configuredWidth && pin->history->shapes.size() == 3,
        "Wheel zoom must not change the pen width or annotation history");
    require(renderShape(firstLine, imageSize) == firstPixels, "Zoom must keep completed annotation pixels unchanged");
    waitForNextStroke();
    {
        ScopedShift shift;
        pin->onMouseDown(POINT{ 242, 121 }, false);
        pin->onMouseMove(POINT{ 363, 182 });
        pin->onMouseUp(POINT{ 363, 182 }, false);
    }
    require(pin->history->shapes.size() == 4, "Drawing on a zoomed image must add an independent stroke");
    const auto zoomedStroke = renderShape(pin->history->shapes.back().get(), imageSize);
    require(zoomedStroke[(125 * imageSize.width + 250) * 4 + 3] != 0,
        "Drawing at zoomed coordinates must map back to original image coordinates");
    wheel(pin, POINT{ 242, 121 }, -WHEEL_DELTA * 2);
    require(std::abs(pin->scale - 1.f) < 0.0001f, "Multiple wheel steps must reverse symmetrically");
    require(sliderThumb(widthSlider).Geometry().as<winrt::Windows::UI::Composition::CompositionEllipseGeometry>().Radius().x * 2.f
        == std::max(3.f, std::ceil(configuredWidth)), "Reversing zoom must restore the exact slider dot diameter");
    pin->toolSub->setShapeSliderVal(L"line", originalLineWidth);
    snapshot(pin->toolSub.get(), L"gui-line-controls.bmp");
    snapshot(pin->toolMain.get(), L"gui-main-toolbar.bmp");
    pin->toolMain->selectTool(L"rect");
    pump();
    auto rectSlider = findSlider(pin->toolSub->body.get());
    require(rectSlider != nullptr, "Rectangle controls must retain a working size slider");
    const auto rectRadius = sliderThumb(rectSlider).Geometry()
        .as<winrt::Windows::UI::Composition::CompositionEllipseGeometry>().Radius().x;
    rectSlider->setValue(rectSlider->getValue() + 2.f);
    require(sliderThumb(rectSlider).Geometry().as<winrt::Windows::UI::Composition::CompositionEllipseGeometry>().Radius().x == rectRadius,
        "Other tools must keep their original fixed slider dot size");
    for (const auto& tool : { L"rect", L"ellipse", L"arrow", L"number", L"line", L"text", L"mosaic", L"eraser" }) {
        pin->toolMain->selectTool(tool);
        pump();
        require(GetFocus() == pin->toolMain->hwnd,
            "Switching drawing tools must keep keyboard focus on the main toolbar");
    }
    pin->close();
    pump();

    Setting::get()->setShortcutKey(L"pin", {});
    WinCap::init();
    cap = WinCap::get();
    selectRegion(cap, region);
    cap->onKeyDown(VK_F8);
    require(!WinPin::hasWindow() && IsWindow(cap->hwnd), "Cleared pin shortcut must not trigger");
    cap->close();
    pump();

    Setting::get()->setCaptureToolbar(defaultCaptureToolbar());
    WinSetting::init();
    auto settings = findWindow<WinSetting>();
    require(settings != nullptr, "Settings must open");
    snapshot(settings, L"gui-common.bmp");
    click(settings->body.get(), L"toolbarBottom");
    require(!Setting::get()->getToolbarBottom(), "Bottom setting switch must update configuration");
    openTab(settings, 1);
    click(settings->body.get(), L"pin");
    settings->onKeyDown(VK_F9);
    settings->onKeyUp(VK_F9);
    require(Setting::get()->getShortcutKey(L"pin") == L"F9", "Shortcut settings must save captured key");
    snapshot(settings, L"gui-shortcuts.bmp");
    click(settings->body.get(), L"clearPinShortcut");
    require(Setting::get()->getShortcutKey(L"pin").empty(), "Clear button must disable pin shortcut");
    openTab(settings, 2);
    auto reorder = findButton(settings->body.get(), L"down-mark");
    require(reorder && reorder->w > 0 && reorder->x + reorder->w <= settings->w,
        "Menu editing controls must fit inside the window");
    auto last = findButton(settings->body.get(), L"toggle-eraser");
    require(last && last->h > 0 && last->y + last->h <= settings->h,
        "All menu items must fit inside the window");
    snapshot(settings, L"gui-toolbar.bmp");
    click(settings->body.get(), L"toggle-line");
    require(Setting::get()->getCaptureToolbar().back() == L"line", "Add button must promote line");
    pump();
    for (int i = 0; i < 5; ++i) {
        click(settings->body.get(), L"up-line");
        pump();
    }
    require(Setting::get()->getCaptureToolbar().front() == L"line", "Reordering buttons must change toolbar order");
    click(settings->body.get(), L"toggle-video");
    pump();
    auto items = Setting::get()->getCaptureToolbar();
    require(std::find(items.begin(), items.end(), L"video") == items.end(), "Remove button must remove menu item");
    click(settings->body.get(), L"resetToolbar");
    require(Setting::get()->getCaptureToolbar() == defaultCaptureToolbar(), "Reset button must restore defaults");
    openTab(settings, 3);
    settings->close();
    pump();

    Setting::get()->getConfigObj().GetNamedObject(L"common").SetNamedValue(
        L"language", winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"en-US"));
    Lang::dispose();
    Lang::init();
    WinSetting::init();
    settings = findWindow<WinSetting>();
    snapshot(settings, L"gui-common-en.bmp");
    openTab(settings, 1);
    snapshot(settings, L"gui-shortcuts-en.bmp");
    openTab(settings, 2);
    last = findButton(settings->body.get(), L"toggle-eraser");
    require(last && last->w > 0 && last->x + last->w <= settings->w && last->y + last->h <= settings->h,
        "English menu controls must fit inside the window");
    snapshot(settings, L"gui-toolbar-en.bmp");
    click(settings->body.get(), L"toggle-line");
    require(Setting::get()->getCaptureToolbar().back() == L"line", "English menu must support actual mouse clicks");
    settings->close();
    pump();
}
