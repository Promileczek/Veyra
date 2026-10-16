#include "Graphics.h"
#include <algorithm>

bool Graphics::Init() {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (FAILED(d3d_.As(&dxgiDevice))) return false;
    dxgiDevice->SetMaximumFrameLatency(1);

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory_)))) return false;

    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf()))))
        return false;
    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) return false;

    if (!dwrite_ &&
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
    if (!wic_ && FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&wic_))))
        return false;
    return true;
}

bool Surface::Create(Graphics& g, HWND hwnd, UINT width, UINT height) {
    dcomp_ = g.DComp();
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = std::max(width, 1u);
    desc.Height = std::max(height, 1u);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(g.DXGIFactory()->CreateSwapChainForComposition(g.D3D(), &desc, nullptr, &swap_)))
        return false;
    if (FAILED(g.D2D()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) return false;
    if (!CreateTarget()) return false;

    if (FAILED(dcomp_->CreateTargetForHwnd(hwnd, TRUE, &compTarget_))) return false;
    if (FAILED(dcomp_->CreateVisual(&visual_))) return false;
    visual_->SetContent(swap_.Get());
    compTarget_->SetRoot(visual_.Get());
    return SUCCEEDED(dcomp_->Commit());
}

void Surface::Appear(float seconds, float fromScale, float cx, float cy, float fromDy) {
    if (!visual_ || !dcomp_ || seconds <= 0.f) return;
    // ease-out (sześcienne): e(t) = 3u − 3u² + u³, u = t/d → wartość = a + b·e(t)
    const float d = seconds;
    auto ramp = [&](float from, float to) -> ComPtr<IDCompositionAnimation> {
        ComPtr<IDCompositionAnimation> a;
        if (FAILED(dcomp_->CreateAnimation(&a))) return nullptr;
        const float b = to - from;
        a->AddCubic(0.0, from, 3.f * b / d, -3.f * b / (d * d), b / (d * d * d));
        a->End(d, to);
        return a;
    };
    ComPtr<IDCompositionEffectGroup> effect;
    ComPtr<IDCompositionScaleTransform> scale;
    ComPtr<IDCompositionTranslateTransform> move;
    ComPtr<IDCompositionTransform> group;
    const auto opacity = ramp(0.f, 1.f), size = ramp(fromScale, 1.f), offset = ramp(fromDy, 0.f);
    if (!opacity || !size || !offset || FAILED(dcomp_->CreateEffectGroup(&effect)) || FAILED(dcomp_->CreateScaleTransform(&scale)) ||
        FAILED(dcomp_->CreateTranslateTransform(&move)))
        return;
    effect->SetOpacity(opacity.Get());
    scale->SetCenterX(cx);
    scale->SetCenterY(cy);
    scale->SetScaleX(size.Get());
    scale->SetScaleY(size.Get());
    move->SetOffsetY(offset.Get());
    IDCompositionTransform* parts[] = {scale.Get(), move.Get()};
    if (FAILED(dcomp_->CreateTransformGroup(parts, 2, &group))) return;
    visual_->SetEffect(effect.Get());
    visual_->SetTransform(group.Get());
    dcomp_->Commit();
}

bool Surface::CreateTarget() {
    ComPtr<IDXGISurface> buffer;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
    const auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(buffer.Get(), &props, &target_))) return false;
    dc_->SetTarget(target_.Get());
    return true;
}

bool Surface::Resize(UINT width, UINT height) {
    if (!swap_) return false;
    dc_->SetTarget(nullptr);
    target_.Reset();
    if (FAILED(swap_->ResizeBuffers(0, std::max(width, 1u), std::max(height, 1u), DXGI_FORMAT_UNKNOWN, 0)))
        return false;
    return CreateTarget();
}

ID2D1DeviceContext* Surface::Begin() {
    if (!dc_ || !target_) return nullptr;
    dc_->BeginDraw();
    return dc_.Get();
}

bool Surface::End() {
    if (FAILED(dc_->EndDraw())) return false;
    const HRESULT hr = swap_->Present(1, 0);
    return hr != DXGI_ERROR_DEVICE_REMOVED && hr != DXGI_ERROR_DEVICE_RESET;
}
