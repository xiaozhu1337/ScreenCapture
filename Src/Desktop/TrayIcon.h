#pragma once
#include <Windows.h>
#include <shellapi.h>
#include <functional>

// A hidden top-level window is required to receive TaskbarCreated broadcasts.
class TrayIcon
{
public:
    using Notify = BOOL (WINAPI*)(DWORD, PNOTIFYICONDATAW);
    explicit TrayIcon(std::function<void(bool, bool)> onMouse,
        Notify notify = Shell_NotifyIconW);
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    HWND window() const { return hwnd; }
private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    void restore();
    HWND hwnd{};
    UINT taskbarCreated{};
    NOTIFYICONDATAW data{};
    std::function<void(bool, bool)> onMouse;
    Notify notify;
    bool failureLogged{};
};
