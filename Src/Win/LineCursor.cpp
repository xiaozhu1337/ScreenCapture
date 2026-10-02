#include "pch.h"
#include "LineCursor.h"

LineCursor::~LineCursor()
{
    if (cursor) {
        if (GetCursor() == cursor) SetCursor(LoadCursor(nullptr, IDC_ARROW));
        DestroyCursor(cursor);
    }
}

HCURSOR LineCursor::get(float width)
{
    const int nextDiameter = std::max(3, static_cast<int>(std::ceil(width)));
    if (cursor && diameter == nextDiameter) return cursor;

    // An odd bitmap places the hotspot exactly at the center of the dot.
    const int size = (nextDiameter + 4) | 1;
    const int center = size / 2;
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    void* bits = nullptr;
    auto bitmap = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return LoadCursor(nullptr, IDC_ARROW);
    auto pixels = static_cast<DWORD*>(bits);
    const float radius = nextDiameter / 2.f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x - center), dy = static_cast<float>(y - center);
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float outer = std::clamp(radius + 0.5f - distance, 0.f, 1.f);
            const float inner = std::clamp(radius - 0.5f - distance, 0.f, 1.f);
            const DWORD alpha = static_cast<DWORD>(std::lround(outer * 255.f));
            // Premultiplied alpha: a black dot with a one-pixel white edge.
            const DWORD color = static_cast<DWORD>(std::lround((outer - inner) * 255.f));
            pixels[y * size + x] = (alpha << 24) | (color << 16) | (color << 8) | color;
        }
    }
    const int maskStride = ((size + 15) / 16) * 2;
    std::vector<BYTE> maskBits(maskStride * size, 0);
    auto mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO info{};
    info.xHotspot = info.yHotspot = center;
    info.hbmColor = bitmap;
    info.hbmMask = mask;
    auto next = mask ? static_cast<HCURSOR>(CreateIconIndirect(&info)) : nullptr;
    if (mask) DeleteObject(mask);
    DeleteObject(bitmap);
    if (!next) return LoadCursor(nullptr, IDC_ARROW);
    if (cursor) {
        if (GetCursor() == cursor) SetCursor(next);
        DestroyCursor(cursor);
    }
    cursor = next;
    diameter = nextDiameter;
    return cursor;
}
