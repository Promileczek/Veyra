#define OEMRESOURCE
#include "VeyraCursor.h"
#include "../core/Settings.h"
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {

static VeyraCursor* s_instance = nullptr;

// Konwersja bitmapy WIC PBGRA32 do natywnego kursora Win32 z kanałem alfa
HCURSOR WicToCursor(IWICBitmap* wicBitmap, int size, int hx, int hy) {
    if (!wicBitmap) return nullptr;
    WICRect rect{0, 0, size, size};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(wicBitmap->Lock(&rect, WICBitmapLockRead, &lock))) return nullptr;
    UINT cbBufferSize = 0;
    BYTE* pvData = nullptr;
    if (FAILED(lock->GetDataPointer(&cbBufferSize, &pvData)) || !pvData) return nullptr;
    UINT stride = 0;
    lock->GetStride(&stride);

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;  // top-down DIB
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* dibBits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hbmColor = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &dibBits, nullptr, 0);
    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, nullptr);
    ReleaseDC(nullptr, hdc);

    if (!hbmColor || !dibBits) {
        if (hbmColor) DeleteObject(hbmColor);
        if (hbmMask) DeleteObject(hbmMask);
        return nullptr;
    }

    memcpy(dibBits, pvData, size * stride);

    ICONINFO ii{};
    ii.fIcon = FALSE;
    ii.xHotspot = hx;
    ii.yHotspot = hy;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;

    HCURSOR hCur = CreateIconIndirect(&ii);
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    return hCur;
}

// Tworzy strzałkę kursora Veyry w dowolnym rozmiarze
HCURSOR CreateVeyraArrow(int size) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return nullptr;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bmp)))
        return nullptr;
    ComPtr<ID2D1Factory> d2d;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), reinterpret_cast<void**>(d2d.GetAddressOf()))))
        return nullptr;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return nullptr;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0.f));

    const float s = static_cast<float>(size) / 32.0f;
    const float ox = 2.0f * s, oy = 2.0f * s;

    ComPtr<ID2D1PathGeometry> path;
    d2d->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);

    // Kształt eleganckiej, smukłej strzałki nowoczesnego systemu
    const D2D1_POINT_2F pts[] = {
        {ox + 0.0f * s,  oy + 0.0f * s},
        {ox + 0.0f * s,  oy + 21.0f * s},
        {ox + 5.5f * s,  oy + 16.5f * s},
        {ox + 9.5f * s,  oy + 24.5f * s},
        {ox + 12.5f * s, oy + 23.0f * s},
        {ox + 8.5f * s,  oy + 15.5f * s},
        {ox + 15.0f * s, oy + 15.5f * s}
    };
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 1; i < 7; ++i) sink->AddLine(pts[i]);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    // 1. Cień (półprzezroczysty, lekko rozmyty pod kątem)
    ComPtr<ID2D1SolidColorBrush> shadowBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.38f), &shadowBrush);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(1.5f * s, 1.5f * s));
    rt->FillGeometry(path.Get(), shadowBrush.Get());
    rt->DrawGeometry(path.Get(), shadowBrush.Get(), 2.0f * s);

    // 2. Czysty biały obrys (kontrast na ciemnych tłach)
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.f), &borderBrush);
    rt->DrawGeometry(path.Get(), borderBrush.Get(), 2.0f * s);

    // 3. Wypełnienie ciemnym grafitem Veyry (#181920)
    ComPtr<ID2D1SolidColorBrush> bodyBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.10f, 0.13f, 1.f), &bodyBrush);
    rt->FillGeometry(path.Get(), bodyBrush.Get());

    // 4. Sygnaturowy akcent błękitny Veyry (#61B3FF)
    ComPtr<ID2D1SolidColorBrush> accentBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.70f, 1.0f, 1.f), &accentBrush);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ox + 5.0f * s, oy + 11.0f * s), 1.6f * s, 1.6f * s), accentBrush.Get());

    rt->EndDraw();
    return WicToCursor(bmp.Get(), size, static_cast<int>(ox), static_cast<int>(oy));
}

// Tworzy kursor dłoni (linki, przyciski)
HCURSOR CreateVeyraHand(int size) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return nullptr;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bmp)))
        return nullptr;
    ComPtr<ID2D1Factory> d2d;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), reinterpret_cast<void**>(d2d.GetAddressOf()))))
        return nullptr;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return nullptr;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0.f));

    const float s = static_cast<float>(size) / 32.0f;
    const float ox = 4.0f * s, oy = 2.0f * s;
    const int hx = static_cast<int>(ox + 6.0f * s);
    const int hy = static_cast<int>(oy + 1.0f * s);

    ComPtr<ID2D1PathGeometry> path;
    d2d->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);

    const D2D1_POINT_2F pts[] = {
        {ox + 6.0f * s,  oy + 0.0f * s},   // czubek palca wskazującego
        {ox + 9.0f * s,  oy + 0.0f * s},
        {ox + 9.0f * s,  oy + 10.0f * s},
        {ox + 12.0f * s, oy + 10.0f * s},  // środkowy
        {ox + 12.0f * s, oy + 18.0f * s},
        {ox + 15.0f * s, oy + 12.0f * s},  // serdeczny
        {ox + 15.0f * s, oy + 18.0f * s},
        {ox + 18.0f * s, oy + 14.0f * s},  // mały
        {ox + 18.0f * s, oy + 22.0f * s},
        {ox + 12.0f * s, oy + 26.0f * s},  // nadgarstek prawo
        {ox + 4.0f * s,  oy + 26.0f * s},  // nadgarstek lewo
        {ox + 1.0f * s,  oy + 18.0f * s},  // kciuk dół
        {ox + 0.0f * s,  oy + 13.0f * s},  // kciuk góra
        {ox + 4.0f * s,  oy + 11.0f * s},  // staw kciuka
        {ox + 6.0f * s,  oy + 10.0f * s}   // podstawa wskazującego
    };

    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 1; i < 15; ++i) sink->AddLine(pts[i]);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    // Cień
    ComPtr<ID2D1SolidColorBrush> shadowBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.38f), &shadowBrush);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(1.5f * s, 1.5f * s));
    rt->FillGeometry(path.Get(), shadowBrush.Get());
    rt->DrawGeometry(path.Get(), shadowBrush.Get(), 2.0f * s);

    // Biały obrys
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.f), &borderBrush);
    rt->DrawGeometry(path.Get(), borderBrush.Get(), 2.0f * s);

    // Ciemne ciało
    ComPtr<ID2D1SolidColorBrush> bodyBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.10f, 0.13f, 1.f), &bodyBrush);
    rt->FillGeometry(path.Get(), bodyBrush.Get());

    // Akcent na dłoni
    ComPtr<ID2D1SolidColorBrush> accentBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.70f, 1.0f, 1.f), &accentBrush);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ox + 8.0f * s, oy + 17.0f * s), 1.6f * s, 1.6f * s), accentBrush.Get());

    rt->EndDraw();
    return WicToCursor(bmp.Get(), size, hx, hy);
}

// Tworzy kursor tekstu (I-Beam)
HCURSOR CreateVeyraIBeam(int size) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return nullptr;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bmp)))
        return nullptr;
    ComPtr<ID2D1Factory> d2d;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), reinterpret_cast<void**>(d2d.GetAddressOf()))))
        return nullptr;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return nullptr;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0.f));

    const float s = static_cast<float>(size) / 32.0f;
    const float cx = size / 2.0f, cy = size / 2.0f;

    ComPtr<ID2D1SolidColorBrush> borderBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 0.95f), &borderBrush);

    ComPtr<ID2D1SolidColorBrush> bodyBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.10f, 0.13f, 1.f), &bodyBrush);

    ComPtr<ID2D1SolidColorBrush> accentBrush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.70f, 1.0f, 1.f), &accentBrush);

    // Biały obrys (widoczny na ciemnych tłach)
    rt->DrawLine({cx - 5.0f * s, cy - 9.0f * s}, {cx + 5.0f * s, cy - 9.0f * s}, borderBrush.Get(), 3.0f * s);
    rt->DrawLine({cx - 5.0f * s, cy + 9.0f * s}, {cx + 5.0f * s, cy + 9.0f * s}, borderBrush.Get(), 3.0f * s);
    rt->DrawLine({cx, cy - 9.0f * s}, {cx, cy + 9.0f * s}, borderBrush.Get(), 3.0f * s);

    // Ciemny rdzeń (widoczny na jasnych tłach)
    rt->DrawLine({cx - 4.0f * s, cy - 9.0f * s}, {cx + 4.0f * s, cy - 9.0f * s}, bodyBrush.Get(), 1.5f * s);
    rt->DrawLine({cx - 4.0f * s, cy + 9.0f * s}, {cx + 4.0f * s, cy + 9.0f * s}, bodyBrush.Get(), 1.5f * s);
    rt->DrawLine({cx, cy - 9.0f * s}, {cx, cy + 9.0f * s}, bodyBrush.Get(), 1.5f * s);

    // Akcent na środku
    rt->FillEllipse(D2D1::Ellipse({cx, cy}, 1.2f * s, 1.2f * s), accentBrush.Get());

    rt->EndDraw();
    return WicToCursor(bmp.Get(), size, static_cast<int>(cx), static_cast<int>(cy));
}

} // namespace

VeyraCursor::~VeyraCursor() {
    Uninstall();
}

void VeyraCursor::Restore() {
    // Przywrócenie domyślnych kursorów systemowych z rejestru Windows
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
}

void VeyraCursor::LoadSettings() {
    veyraCursorEnabled_ = Settings::GetBool(L"veyraCursor", true);
    shakeToFindEnabled_ = Settings::GetBool(L"shakeToFind", true);
    shakeScale_ = std::clamp(_wtoi(Settings::Get(L"shakeScale", L"96").c_str()), 48, 128);
    baseSize_ = std::max(32, GetSystemMetrics(SM_CXCURSOR));
}

void VeyraCursor::CreateCachedCursors() {
    DestroyCachedCursors();
    hNormalArrow_ = CreateVeyraArrow(baseSize_);
    hNormalHand_ = CreateVeyraHand(baseSize_);
    hNormalIBeam_ = CreateVeyraIBeam(baseSize_);

    // Rozmiar powiększonego kursora z uwzględnieniem DPI
    const int scaleMultiplier = (baseSize_ > 32) ? (baseSize_ / 32) : 1;
    cachedEnlargedSize_ = shakeScale_ * scaleMultiplier;
    hEnlargedArrow_ = CreateVeyraArrow(cachedEnlargedSize_);
}

void VeyraCursor::DestroyCachedCursors() {
    if (hNormalArrow_) { DestroyCursor(hNormalArrow_); hNormalArrow_ = nullptr; }
    if (hNormalHand_) { DestroyCursor(hNormalHand_); hNormalHand_ = nullptr; }
    if (hNormalIBeam_) { DestroyCursor(hNormalIBeam_); hNormalIBeam_ = nullptr; }
    if (hEnlargedArrow_) { DestroyCursor(hEnlargedArrow_); hEnlargedArrow_ = nullptr; }
}

bool VeyraCursor::Install(HINSTANCE inst) {
    if (installed_) return true;
    inst_ = inst;
    s_instance = this;

    LoadSettings();
    CreateCachedCursors();

    if (veyraCursorEnabled_) {
        ApplyCursors();
    }

    // Timer próbkowania myszy do potrząsania (ok. 50 Hz, minimalny narzut CPU)
    timer_ = SetTimer(nullptr, 0, 20, TimerProc);
    installed_ = true;
    return true;
}

void VeyraCursor::Uninstall() {
    if (!installed_) return;
    if (timer_) {
        KillTimer(nullptr, timer_);
        timer_ = 0;
    }
    s_instance = nullptr;
    installed_ = false;

    // Przywróć systemowe kursory
    Restore();
    DestroyCachedCursors();
}

void VeyraCursor::ReloadSettings() {
    const bool oldVeyra = veyraCursorEnabled_;
    const int oldScale = shakeScale_;
    LoadSettings();

    if (oldScale != shakeScale_ || !hNormalArrow_) {
        CreateCachedCursors();
    }

    if (veyraCursorEnabled_ && !oldVeyra) {
        ApplyCursors();
    } else if (!veyraCursorEnabled_ && oldVeyra) {
        Restore();
    }
}

void VeyraCursor::ApplyCursors() {
    if (!hNormalArrow_) return;
    // SetSystemCursor niszczy przekazany kursor, dlatego ZAWSZE przekazujemy kopię (CopyCursor)
    SetSystemCursor(CopyCursor(hNormalArrow_), OCR_NORMAL);
    if (hNormalHand_) SetSystemCursor(CopyCursor(hNormalHand_), OCR_HAND);
    if (hNormalIBeam_) SetSystemCursor(CopyCursor(hNormalIBeam_), OCR_IBEAM);
}

void VeyraCursor::ApplyEnlargedCursor() {
    if (!hEnlargedArrow_) return;
    SetSystemCursor(CopyCursor(hEnlargedArrow_), OCR_NORMAL);
}

void VeyraCursor::RestoreNormalCursor() {
    if (veyraCursorEnabled_ && hNormalArrow_) {
        SetSystemCursor(CopyCursor(hNormalArrow_), OCR_NORMAL);
    } else {
        Restore();
    }
}

void CALLBACK VeyraCursor::TimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (s_instance) s_instance->OnTimer();
}

void VeyraCursor::OnTimer() {
    POINT pt{};
    if (!GetCursorPos(&pt)) return;
    const ULONGLONG now = GetTickCount64();

    if (!shakeToFindEnabled_) {
        if (isShaking_) {
            isShaking_ = false;
            RestoreNormalCursor();
        }
        return;
    }

    // Dodaj próbkę do bufora
    samples_.push_back({pt, now});
    while (!samples_.empty() && now - samples_.front().time > 350) {
        samples_.erase(samples_.begin());
    }

    if (!isShaking_) {
        if (CheckShakeCondition()) {
            isShaking_ = true;
            shakeEndTime_ = now + 700;  // trzymaj powiększenie przez min. 700 ms
            ApplyEnlargedCursor();
        }
    } else {
        // Gdy potrząsanie trwa, odświeżaj czas zakończenia
        if (samples_.size() >= 2) {
            const int dx = pt.x - samples_[samples_.size() - 2].pt.x;
            const int dy = pt.y - samples_[samples_.size() - 2].pt.y;
            if (std::abs(dx) > 10 || std::abs(dy) > 10) {
                shakeEndTime_ = now + 500;
            }
        }
        if (now > shakeEndTime_) {
            isShaking_ = false;
            RestoreNormalCursor();
        }
    }
}

bool VeyraCursor::CheckShakeCondition() const {
    if (samples_.size() < 6) return false;

    float totalDist = 0.f;
    int reversals = 0;
    int lastDirX = 0;
    int minX = samples_[0].pt.x, maxX = samples_[0].pt.x;
    int minY = samples_[0].pt.y, maxY = samples_[0].pt.y;

    for (size_t i = 1; i < samples_.size(); ++i) {
        const int dx = samples_[i].pt.x - samples_[i - 1].pt.x;
        const int dy = samples_[i].pt.y - samples_[i - 1].pt.y;
        totalDist += std::sqrt(static_cast<float>(dx * dx + dy * dy));

        if (samples_[i].pt.x < minX) minX = samples_[i].pt.x;
        if (samples_[i].pt.x > maxX) maxX = samples_[i].pt.x;
        if (samples_[i].pt.y < minY) minY = samples_[i].pt.y;
        if (samples_[i].pt.y > maxY) maxY = samples_[i].pt.y;

        if (std::abs(dx) > 12) {
            const int dirX = (dx > 0) ? 1 : -1;
            if (lastDirX != 0 && dirX != lastDirX) {
                reversals++;
            }
            lastDirX = dirX;
        }
    }

    float bboxDiag = std::sqrt(static_cast<float>((maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY)));
    if (bboxDiag < 1.0f) bboxDiag = 1.0f;

    // Próg potrząsania dostosowany do skali
    const float minDistance = 240.0f * (static_cast<float>(baseSize_) / 32.0f);
    return (reversals >= 3 && totalDist >= minDistance && (totalDist / bboxDiag) >= 1.5f);
}
