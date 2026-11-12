#include "pch.h"
#include "TrayIcon.h"
#include "TrayLog.h"
#include <stdexcept>
#include <utility>

namespace {
    constexpr wchar_t windowClass[] = L"ScreenCapture.TrayIcon";
    constexpr UINT callbackMessage = WM_APP + 100;
    constexpr UINT_PTR retryTimer = 1;
}

TrayIcon::TrayIcon(std::function<void(bool, bool)> onMouse, Notify notify)
    : onMouse(std::move(onMouse)), notify(notify)
{
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = windowClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        TrayLog::error("Register tray window class failed", GetLastError());
        throw std::runtime_error("Cannot register tray window class");
    }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass,
        L"ScreenCapture tray", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!hwnd || !taskbarCreated) {
        TrayLog::error("Create tray window failed", GetLastError());
        if (hwnd) DestroyWindow(hwnd);
        throw std::runtime_error("Cannot create tray window");
    }
    // Explorer can run at a lower integrity level than the application.
    ChangeWindowMessageFilterEx(hwnd, taskbarCreated, MSGFLT_ALLOW, nullptr);
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = callbackMessage;
    data.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wcscpy_s(data.szTip, L"Screen Capture");
    restore();
}

TrayIcon::~TrayIcon()
{
    KillTimer(hwnd, retryTimer);
    notify(NIM_DELETE, &data);
    DestroyWindow(hwnd);
}

void TrayIcon::restore()
{
    // MODIFY makes duplicate broadcasts harmless; ADD recreates a missing icon.
    if (notify(NIM_MODIFY, &data) || notify(NIM_ADD, &data)) {
        KillTimer(hwnd, retryTimer);
        failureLogged = false;
        return;
    }
    if (!failureLogged) {
        TrayLog::error("Tray registration failed; retrying", GetLastError());
        failureLogged = true;
    }
    if (!SetTimer(hwnd, retryTimer, 2000, nullptr))
        TrayLog::error("Create tray retry timer failed", GetLastError());
}

LRESULT CALLBACK TrayIcon::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<TrayIcon*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        if (self->taskbarCreated && message == self->taskbarCreated) {
            self->restore();
            return 0;
        }
        if (message == WM_TIMER && wParam == retryTimer) {
            self->restore();
            return 0;
        }
        if (message == callbackMessage) {
            if (lParam == WM_LBUTTONDOWN || lParam == WM_LBUTTONUP ||
                lParam == WM_RBUTTONDOWN || lParam == WM_RBUTTONUP) {
                self->onMouse(lParam == WM_LBUTTONDOWN || lParam == WM_RBUTTONDOWN,
                    lParam == WM_RBUTTONDOWN || lParam == WM_RBUTTONUP);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
