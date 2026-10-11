#include "pch.h"
#include <stdexcept>

void testStartup()
{
    auto app = Ling::App::get();
    app->appID = L"ScreenCaptureStartupTest-" + std::to_wstring(GetCurrentProcessId());
    if (app->refuseSecondInstance()) throw std::runtime_error("Startup test must be a new instance");
    auto window = FindWindowEx(HWND_MESSAGE, nullptr, L"STATIC", app->appID.c_str());
    DWORD process = 0;
    if (!window || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId())
        throw std::runtime_error("Startup must create a valid message window");
    UINT received = 0;
    const auto token = app->onHotKey.add([&received](UINT id) { received = id; });
    SendMessage(window, WM_HOTKEY, WM_APP + 77, 0);
    app->onHotKey.remove(token);
    if (received != 77) throw std::runtime_error("Startup message window must dispatch hotkeys");
    NOTIFYICONIDENTIFIER icon{ sizeof(icon) };
    icon.hWnd = window;
    RECT rect{};
    app->initTray(100, L"ScreenCapture startup test");
    const auto result = Shell_NotifyIconGetRect(&icon, &rect);
    app->disposeTray();
    if (FAILED(result)) throw std::runtime_error("Startup must register a working tray icon");
}
