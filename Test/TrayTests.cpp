#include "pch.h"
#include "Desktop/TrayIcon.h"
#include <stdexcept>

namespace {
    bool rejectRegistration{};
    int registrations{};

    BOOL WINAPI notify(DWORD operation, PNOTIFYICONDATAW data)
    {
        if (operation == NIM_ADD) ++registrations;
        if (rejectRegistration && operation != NIM_DELETE) return FALSE;
        return Shell_NotifyIconW(operation, data);
    }

    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    bool registered(HWND window)
    {
        NOTIFYICONIDENTIFIER icon{ sizeof(icon) };
        icon.hWnd = window;
        RECT rect{};
        return SUCCEEDED(Shell_NotifyIconGetRect(&icon, &rect));
    }

    void removeIcon(HWND window)
    {
        NOTIFYICONDATAW data{ sizeof(data) };
        data.hWnd = window;
        require(Shell_NotifyIconW(NIM_DELETE, &data), "Cannot remove test tray icon");
    }

    void pumpUntilRegistered(HWND window)
    {
        const auto deadline = GetTickCount64() + 5000;
        while (!registered(window) && GetTickCount64() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        require(registered(window), "Tray did not recover within five seconds");
    }
}

void testTrayRecovery()
{
    const auto recreated = RegisterWindowMessageW(L"TaskbarCreated");
    HWND oldWindow{};
    {
        bool clicked = false;
        TrayIcon tray([&](bool down, bool right) { clicked = down && right; }, notify);
        oldWindow = tray.window();
        require(registered(oldWindow), "Initial tray registration failed");
        require(!IsWindowVisible(oldWindow), "Tray owner must remain hidden");
        require(GetAncestor(oldWindow, GA_ROOT) == oldWindow && !GetParent(oldWindow),
            "Tray owner must be a top-level broadcast recipient");
        SendMessageW(oldWindow, WM_APP + 100, 0, WM_RBUTTONDOWN);
        require(clicked, "Tray mouse callback was lost");
        removeIcon(oldWindow);
        require(!registered(oldWindow), "Test icon must be absent before recovery");
        // Broadcast via the real top-level window path, as Explorer does.
        require(PostMessageW(HWND_BROADCAST, recreated, 0, 0), "Cannot broadcast taskbar recreation");
        pumpUntilRegistered(oldWindow);
        const auto previousAdds = registrations;
        SendMessageW(oldWindow, recreated, 0, 0);
        require(registrations == previousAdds, "Duplicate broadcasts must not add duplicate icons");

        removeIcon(oldWindow);
        rejectRegistration = true;
        SendMessageW(oldWindow, recreated, 0, 0);
        require(!registered(oldWindow), "Failed registration should remain pending");
        rejectRegistration = false;
        pumpUntilRegistered(oldWindow);
    }
    require(!IsWindow(oldWindow) && !registered(oldWindow), "Disposal must remove window and icon");
    rejectRegistration = true;
    {
        TrayIcon tray([](bool, bool) {}, notify);
        require(!registered(tray.window()), "Startup registration failure was not simulated");
        rejectRegistration = false;
        pumpUntilRegistered(tray.window());
    }
}
