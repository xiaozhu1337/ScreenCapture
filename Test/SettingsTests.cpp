#include "pch.h"
#include "Setting.h"
#include "Lang.h"
#include "Win/Shortcut.h"
#include "Tool/ToolPlacement.h"
#include <iostream>
#include <stdexcept>

namespace {
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void testShortcuts()
    {
        auto setting = Setting::get();
        require(setting->getShortcutKey(L"pin") == L"F3", "Old configurations should default to F3");
        require(Shortcut::parse(L"Ctrl+Alt+F8").matches(VK_F8, MOD_CONTROL | MOD_ALT), "Combined pin shortcut");
        require(!Shortcut::parse(L"Ctrl+Alt+F8").matches(VK_F8, MOD_CONTROL), "Missing modifier must not trigger");
        require(!Shortcut::parse(L"F3").matches(VK_F3, MOD_SHIFT), "Extra modifier must not trigger");
        require(Shortcut::parse(L"RWin+F8").matches(VK_F8, MOD_WIN), "Windows modifier normalization");
        require(!Shortcut::parse(L"").matches(VK_F3, 0), "Disabled shortcut must not trigger");
        require(!Shortcut::parse(L"Ctrl+Unknown").key, "Unknown key must not trigger");
        require(!Shortcut::parse(L"Ctrl+F3+F4").key, "Multiple normal keys must be rejected");
        setting->setShortcutKey(L"pin", { L"Ctrl", L"Alt", L"F8" });
        require(setting->getShortcutKey(L"cap") == L"Ctrl+Alt+A", "Pin must preserve capture shortcut");
        Setting::dispose();
        Setting::init();
        require(Setting::get()->getShortcutKey(L"pin") == L"Ctrl+Alt+F8", "Custom pin shortcut must persist");
        Setting::get()->setShortcutKey(L"pin", {});
        Setting::dispose();
        Setting::init();
        require(Setting::get()->getShortcutKey(L"pin").empty(), "Disabled pin shortcut must stay disabled");
        require(Lang::get(L"shortcut.pin") == L"钉住当前选区：", "Embedded Chinese shortcut translation");
        Setting::get()->setLang(L"en-US");
        require(Lang::get(L"shortcut.pin") == L"Pin selection:", "Embedded English shortcut translation");
    }

    void testPlacement()
    {
        auto setting = Setting::get();
        require(!setting->getToolbarBottom(), "Old configurations should keep automatic placement");
        setting->setToolbarBottom(true);
        Setting::dispose();
        Setting::init();
        require(Setting::get()->getToolbarBottom(), "Bottom placement setting must persist");
        const RECT work{ 0, 0, 1920, 1000 };
        auto below = positionToolBar({ 200, 100, 900, 900 }, work, { 300, 40 }, 40, 5, { 3, 3 }, true);
        require(below.x == 600 && below.y == 905, "Toolbar should use available space below selection");
        auto automatic = positionToolBar({ 200, 400, 900, 995 }, work, { 300, 40 }, 40, 5, { 3, 3 }, false);
        require(automatic.y == 355, "Automatic mode should use available space above selection");
        auto bottom = positionToolBar({ 200, 400, 900, 995 }, work, { 300, 40 }, 40, 5, { 3, 3 }, true);
        require(bottom.x == 597 && bottom.y == 952, "Bottom mode must overlay bottom instead of jumping above");
        auto sub = positionToolBar({ 200, 400, 900, 995 }, work, { 300, 100 }, 100, 5, { 0, 5 }, true);
        require(sub.y == 890 && sub.y + 100 <= work.bottom, "Expanded toolbar group must remain visible at bottom");
        auto negative = positionToolBar({ -1900, 1050, -1880, 1070 }, { -1920, 0, 0, 1080 },
            { 400, 60 }, 60, 5, { 3, 3 }, true);
        require(negative.x == -1920 && negative.y == 1007, "Toolbar must handle negative monitor origins");
        auto outside = positionToolBar({ -900, 4000, -800, 5000 }, work, { 300, 60 }, 60, 5, { 3, 3 }, true);
        require(outside.x == 0 && outside.y == 940, "Offscreen selection must clamp toolbar to work area");
    }
}

int main()
{
    wchar_t executable[MAX_PATH]{};
    GetModuleFileName(nullptr, executable, MAX_PATH);
    auto config = std::filesystem::path(executable).parent_path() / L"config.json";
    require(!std::filesystem::exists(config), "Tests require an isolated target directory without config.json");
    Ling::Util::saveFile(config.wstring(), LR"({"common":{"language":"zh-CN"},"shortcutKey":{"cap":"Ctrl+Alt+A"}})");
    int result = 0;
    try {
        require(Ling::init(), "Ling initialization failed");
        Setting::init();
        Lang::init();
        testShortcuts();
        testPlacement();
        std::cout << "Shortcut and toolbar placement tests passed\n";
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    Lang::dispose();
    Setting::dispose();
    Ling::dispose();
    std::filesystem::remove(config);
    return result;
}
