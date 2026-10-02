#pragma once
#include <string>
#include <vector>

struct CaptureToolDefinition
{
    std::wstring id;
    std::wstring icon;
    std::wstring label;
    bool drawing;
};

inline const std::vector<CaptureToolDefinition>& captureToolDefinitions()
{
    static const std::vector<CaptureToolDefinition> tools{
        { L"mark", L"\ue97f", L"cap.mark", false },
        { L"long", L"\ue73e", L"cap.long", false },
        { L"video", L"\ue660", L"cap.video", false },
        { L"ocr", L"\ue67b", L"cap.ocr", false },
        { L"qrcode", L"\ue71e", L"cap.qrcode", false },
        { L"rect", L"\ue8e8", L"tool.rect", true },
        { L"ellipse", L"\ue6bc", L"tool.ellipse", true },
        { L"arrow", L"\ue603", L"tool.arrow", true },
        { L"number", L"\ue776", L"tool.number", true },
        { L"line", L"\ue601", L"tool.line", true },
        { L"text", L"\ue6ec", L"tool.text", true },
        { L"mosaic", L"\ue82e", L"tool.mosaic", true },
        { L"eraser", L"\ue6be", L"tool.eraser", true },
    };
    return tools;
}

inline const CaptureToolDefinition* findCaptureTool(const std::wstring& id)
{
    for (const auto& tool : captureToolDefinitions()) {
        if (tool.id == id) return &tool;
    }
    return nullptr;
}

inline std::vector<std::wstring> defaultCaptureToolbar()
{
    return { L"mark", L"long", L"video", L"ocr", L"qrcode" };
}
