#include "pch.h"
#include "Setting.h"
#include "Lang.h"
#include "History.h"
#include "Win/WinCap.h"
#include "Win/WinPin.h"
#include "Win/WinSetting.h"
#include "Win/Shortcut.h"
#include "Tool/ToolCap.h"
#include "Tool/ToolMain.h"
#include "Tool/ToolSub.h"
#include "Tool/ToolRegistry.h"
#include "Shape/ShapeLine.h"
#include <fstream>
#include <stdexcept>

namespace {
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

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
    pin->onMouseDown(POINT{ 40, 40 }, false);
    pin->onMouseMove(POINT{ 150, 100 });
    pin->onMouseUp(POINT{ 150, 100 }, false);
    require(pin->history->shapes.size() == 1 && dynamic_cast<ShapeLine*>(pin->history->shapes[0].get()),
        "Promoted line must support actual drawing");
    pin->history->undo();
    require(pin->history->shapes[0]->isUndo, "Undo must work after entering through a promoted tool");
    pin->history->redo();
    require(!pin->history->shapes[0]->isUndo, "Redo must work after entering through a promoted tool");
    snapshot(pin->toolSub.get(), L"gui-line-controls.bmp");
    snapshot(pin->toolMain.get(), L"gui-main-toolbar.bmp");
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
