#pragma once
#include <include/Ling.h>

class PinCanvas : public Ling::Node
{
public:
    explicit PinCanvas(Ling::WinBase* win);
    ~PinCanvas();
    ID2D1DeviceContext* startPaint();
    void finishPaint();
private:
    struct Buffer
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swap;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> target;
        winrt::Windows::UI::Composition::CompositionSurfaceBrush brush{ nullptr };
        UINT width{ 0 }, height{ 0 };
    };
    bool prepare(UINT width, UINT height);
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> ctx;
    Buffer displayed, pending;
    bool painting{ false };
};
