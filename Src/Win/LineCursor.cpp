#include "pch.h"
#include "LineCursor.h"

LineCursor::~LineCursor()
{
    if (cursor) {
        if (GetCursor() == cursor) SetCursor(LoadCursor(nullptr, IDC_ARROW));
        DestroyCursor(cursor);
    }
}

HCURSOR LineCursor::get(float width, DWORD rgba)
{
    const int nextDiameter = std::max(3, static_cast<int>(std::ceil(width)));
    if (cursor && diameter == nextDiameter && color == rgba) return cursor;

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
    const DWORD red = (rgba >> 24) & 255, green = (rgba >> 16) & 255, blue = (rgba >> 8) & 255;
    const float opacity = (rgba & 255) / 255.f;
    const float edgeColor = (299 * red + 587 * green + 114 * blue >= 128000) ? 0.f : 255.f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x - center), dy = static_cast<float>(y - center);
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float outer = std::clamp(radius + 0.5f - distance, 0.f, 1.f);
            const float inner = std::clamp(radius - 0.5f - distance, 0.f, 1.f);
            const float fill = inner * opacity;
            const float edge = outer - inner;
            const DWORD alpha = static_cast<DWORD>(std::lround((fill + edge) * 255.f));
            // Premultiplied selected color with a contrasting one-pixel edge.
            const DWORD r = static_cast<DWORD>(std::lround(fill * red + edge * edgeColor));
            const DWORD g = static_cast<DWORD>(std::lround(fill * green + edge * edgeColor));
            const DWORD b = static_cast<DWORD>(std::lround(fill * blue + edge * edgeColor));
            pixels[y * size + x] = (alpha << 24) | (r << 16) | (g << 8) | b;
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
    color = rgba;
    return cursor;
}
