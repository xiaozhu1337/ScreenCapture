#pragma once
#include <include/Ling.h>

struct Shortcut
{
    UINT key{ 0 };
    UINT modifiers{ 0 };

    static Shortcut parse(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), ::towlower);
        Shortcut result;
        for (const auto& part : Ling::Util::splitStr(text, L'+')) {
            if (part == L"ctrl") result.modifiers |= MOD_CONTROL;
            else if (part == L"alt") result.modifiers |= MOD_ALT;
            else if (part == L"shift") result.modifiers |= MOD_SHIFT;
            else if (part == L"win" || part == L"lwin" || part == L"rwin") result.modifiers |= MOD_WIN;
            else {
                if (result.key || part.empty()) return {};
                result.key = Ling::Util::strToKey(part);
                if (!result.key) return {};
            }
        }
        return result;
    }

    bool matches(UINT pressedKey, UINT pressedModifiers) const
    {
        return key != 0 && key == pressedKey && modifiers == pressedModifiers;
    }

    static UINT currentModifiers()
    {
        UINT result{ 0 };
        if (GetKeyState(VK_CONTROL) & 0x8000) result |= MOD_CONTROL;
        if (GetKeyState(VK_MENU) & 0x8000) result |= MOD_ALT;
        if (GetKeyState(VK_SHIFT) & 0x8000) result |= MOD_SHIFT;
        if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) result |= MOD_WIN;
        return result;
    }
};
