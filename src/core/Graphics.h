#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;

// Jedno wspólne urządzenie graficzne dla wszystkich okien powłoki (oszczędność RAM/VRAM).
class Graphics {
public:
    bool Init();

    ID3D11Device* D3D() const { return d3d_.Get(); }
    IDXGIFactory2* DXGIFactory() const { return dxgiFactory_.Get(); }
    ID2D1Device* D2D() const { return d2dDevice_.Get(); }
    IDWriteFactory* DWrite() const { return dwrite_.Get(); }
    IDCompositionDevice* DComp() const { return dcomp_.Get(); }
    IWICImagingFactory* WIC() const { return wic_.Get(); }

private:
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGIFactory2> dxgiFactory_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDCompositionDevice> dcomp_;
    ComPtr<IWICImagingFactory> wic_;
};

// Powierzchnia rysowania jednego okna: swapchain z przezroczystością + DirectComposition + kontekst D2D.
class Surface {
public:
    bool Create(Graphics& g, HWND hwnd, UINT width, UINT height);
    bool Resize(UINT width, UINT height);
    ID2D1DeviceContext* Context() const { return dc_.Get(); }
    ID2D1DeviceContext* Begin();
    bool End();  // false = urządzenie utracone, trzeba odtworzyć

private:
    bool CreateTarget();

    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<IDCompositionTarget> compTarget_;
    ComPtr<IDCompositionVisual> visual_;
    IDCompositionDevice* dcomp_ = nullptr;
};
