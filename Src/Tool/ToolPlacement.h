#pragma once
#include <include/Ling.h>

inline D2D1_POINT_2F positionToolBar(const RECT& region, const RECT& work,
    D2D1_SIZE_F size, float requiredHeight, float gap, D2D1_POINT_2F inset, bool alwaysBottom)
{
    float x = region.right - size.width;
    float y;
    if (region.bottom + gap + requiredHeight <= work.bottom) {
        y = region.bottom + gap;
    }
    else if (!alwaysBottom && region.top - gap - requiredHeight >= work.top) {
        y = region.top - gap - size.height;
    }
    else {
        x -= inset.x;
        y = region.bottom - size.height - inset.y;
    }
    x = std::clamp(x, static_cast<float>(work.left), std::max(static_cast<float>(work.left), work.right - size.width));
    y = std::clamp(y, static_cast<float>(work.top), std::max(static_cast<float>(work.top), work.bottom - size.height));
    return { x, y };
}
