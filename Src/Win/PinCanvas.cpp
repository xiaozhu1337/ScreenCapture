#include "pch.h"
#include "PinCanvas.h"

PinCanvas::PinCanvas(Ling::WinBase* win) : Ling::Node(win) {}

PinCanvas::~PinCanvas()
{
    if (ctx) {
        if (painting) ctx->EndDraw();
        ctx->SetTarget(nullptr);
    }
}

bool PinCanvas::prepare(UINT width, UINT height)
{
    auto d2d = Ling::D2D::get();
    if (!ctx && FAILED(d2d->d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf()))) return false;
    if (displayed.swap && width <= displayed.width && height <= displayed.height) {
        ctx->SetTarget(displayed.target.Get());
        return true;
    }

    // Reuse capacity when shrinking. Grow a separate buffer without discarding the visible frame.
    Buffer next;
    next.width = std::max(displayed.width, (width + 255) / 256 * 256);
    next.height = std::max(displayed.height, (height + 255) / 256 * 256);
    next.swap = d2d->createSwapChain(next.width, next.height);
    if (!next.swap) return false;
    Microsoft::WRL::ComPtr<IDXGISurface> back;
    if (FAILED(next.swap->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf())))) return false;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    if (FAILED(ctx->CreateBitmapFromDxgiSurface(back.Get(), &properties, next.target.GetAddressOf()))) return false;
    auto surface = d2d->createSurfaceForSwapChain(win->compositor, next.swap.Get());
    if (!surface) return false;
    next.brush = win->compositor.CreateSurfaceBrush(surface);
    next.brush.Stretch(winrt::Windows::UI::Composition::CompositionStretch::None);
    next.brush.HorizontalAlignmentRatio(0.f);
    next.brush.VerticalAlignmentRatio(0.f);
    pending = std::move(next);
    ctx->SetTarget(pending.target.Get());
    return true;
}

ID2D1DeviceContext* PinCanvas::startPaint()
{
    if (painting || w < 1.f || h < 1.f || !prepare(static_cast<UINT>(w), static_cast<UINT>(h))) return nullptr;
    ctx->BeginDraw();
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    painting = true;
    return ctx.Get();
}

void PinCanvas::finishPaint()
{
    if (!painting) return;
    painting = false;
    auto& buffer = pending.swap ? pending : displayed;
    const auto drawResult = ctx->EndDraw();
    if (SUCCEEDED(drawResult) && SUCCEEDED(buffer.swap->Present(0, 0))) {
        if (pending.swap) {
            // Attach only a fully painted and presented buffer; keep the previous brush until here.
            visual.Brush(pending.brush);
            displayed = std::move(pending);
        }
    }
    pending = {};
}
