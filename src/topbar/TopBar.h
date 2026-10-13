#pragma once
#include "../core/Graphics.h"
#include "../shell/MediaService.h"
#include "../shell/SystemStatus.h"
#include "../taskbar/Theme.h"
#include "StatusPopups.h"

// Górny pasek (pływający, szklany): sieć po lewej, "teraz odtwarzane" na środku, głośność po prawej.
class TopBar {
public:
    bool Create(HINSTANCE inst, Graphics& g);
    void Destroy();

private:
    enum Hit { HitNone = -1, HitNetwork, HitPrev, HitPlay, HitNext, HitMedia, HitVolume };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    bool CreateResources();
    void ReserveSpace();
    void Layout();
    float TextWidth(const std::wstring& text, IDWriteTextFormat* format) const;
    void Render();
    void RequestRedraw();
    void UpdateCover(const MediaState& media);
    void UpdateFullscreen(HWND foreground);
    int HitTest(LPARAM lp) const;
    RECT ScreenRect(const D2D1_RECT_F& r) const;
    void Glyph(wchar_t glyph, const D2D1_RECT_F& r, const D2D1_COLOR_F& color);
    void Text(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& r, const D2D1_COLOR_F& color);

    HWND hwnd_ = nullptr;
    Graphics* g_ = nullptr;
    Surface surface_;
    TaskbarTheme theme_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> glyphFormat_, textFormat_, smallFormat_;
    ComPtr<IDWriteInlineObject> ellipsis_;
    ComPtr<ID2D1Bitmap> cover_;
    unsigned coverVersion_ = ~0u;

    MediaService media_;
    MediaState mediaState_;
    NetworkStatus network_;
    NetworkInfo net_;
    VolumeControl volume_;
    VolumePopup volumePopup_;
    NetworkPopup networkPopup_;

    UINT dpi_ = 96;
    float widthDip_ = 0.f, heightDip_ = 0.f;
    bool appbar_ = false;
    bool redrawPending_ = false;
    bool trackingMouse_ = false;
    int hover_ = HitNone;
    bool hiddenForFullscreen_ = false;

    D2D1_RECT_F netRect_{}, mediaRect_{}, coverRect_{}, titleRect_{}, prevRect_{}, playRect_{}, nextRect_{},
        progressRect_{}, volumeRect_{};
};
