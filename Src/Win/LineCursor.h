#pragma once
#include <Windows.h>

class LineCursor
{
public:
    LineCursor() = default;
    ~LineCursor();
    LineCursor(const LineCursor&) = delete;
    LineCursor& operator=(const LineCursor&) = delete;
    HCURSOR get(float width, DWORD rgba);
private:
    HCURSOR cursor{ nullptr };
    int diameter{ 0 };
    DWORD color{ 0 };
};
