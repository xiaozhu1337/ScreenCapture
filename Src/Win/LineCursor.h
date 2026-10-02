#pragma once
#include <Windows.h>

class LineCursor
{
public:
    LineCursor() = default;
    ~LineCursor();
    LineCursor(const LineCursor&) = delete;
    LineCursor& operator=(const LineCursor&) = delete;
    HCURSOR get(float width);
private:
    HCURSOR cursor{ nullptr };
    int diameter{ 0 };
};
