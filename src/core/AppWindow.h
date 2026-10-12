#pragma once
#include "Graphics.h"
#include <string>

// Okno samodzielnej aplikacji Explodera (Terminal, Kalkulator, Sklep): bez ramki systemowej, kropki macOS w lewym
// górnym rogu, pasek tytułu do przeciągania, zmiana rozmiaru krawędziami, cień DWM, tło rozmyte albo jednolite.
// Podklasa rysuje całe wnętrze (DIP); kropki i tytuł dorysowuje baza na wierzchu.
class AppWindow {
public:
    virtual ~AppWindow() = default;
    struct Options {
        const wchar_t* className = L"ExploderApp";
        const wchar_t* title = L"";
        float width = 800.f, height = 520.f;        // DIP
        float minWidth = 320.f, minHeight = 240.f;  // DIP
        bool resizable = true;
        bool blur = false;  // rozmyte tło pod półprzezroczystym wnętrzem (Clear z alfą < 1)
        int iconId = 1;     // ikona z zasobów exe
    };
    bool Create(HINSTANCE inst, Graphics& g, const Options& o);
    HWND Hwnd() const { return hwnd_; }
    static int RunMessageLoop();

protected:
    virtual void Paint(ID2D1DeviceContext* dc) = 0;  // całe wnętrze; rozmiar w widthDip_/heightDip_
    virtual void OnMouseMove(D2D1_POINT_2F) {}
    virtual void OnMouseDown(D2D1_POINT_2F, bool /*right*/) {}
    virtual void OnMouseUp(D2D1_POINT_2F) {}
    virtual void OnDoubleClick(D2D1_POINT_2F) {}
    virtual void OnMouseLeave() {}
    virtual void OnWheel(int /*delta*/, D2D1_POINT_2F) {}
    virtual bool OnKeyDown(UINT /*vk*/) { return false; }  // true = obsłużone (bez WM_CHAR z TranslateMessage nic nie zmieniamy)
    virtual void OnChar(wchar_t) {}
    virtual void OnResize() {}
    virtual void OnTimer(UINT_PTR) {}
    virtual void OnActivate(bool) {}
    virtual bool OnMessage(UINT /*msg*/, WPARAM, LPARAM, LRESULT& /*result*/) { return false; }  // własne komunikaty
    virtual bool IsCaption(D2D1_POINT_2F p) const { return p.y < titleH_; }  // gdzie można chwycić okno

    void Redraw();  // przerysowanie zbiorcze (komunikat), bezpieczne z dowolnego miejsca
    void SetTitle(const std::wstring& t);
    void Fill(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0.f);
    void Text(ID2D1DeviceContext* dc, const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    ComPtr<IDWriteTextFormat> MakeFormat(const wchar_t* font, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) const;
    static bool Inside(const D2D1_RECT_F& r, D2D1_POINT_2F p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

    HWND hwnd_ = nullptr;
    HINSTANCE inst_ = nullptr;
    Graphics* g_ = nullptr;
    UINT dpi_ = 96;
    float widthDip_ = 0.f, heightDip_ = 0.f;
    float titleH_ = 40.f;    // wysokość paska tytułu (DIP)
    bool showTitle_ = true;  // tytuł wyśrodkowany w pasku
    bool active_ = true;
    ComPtr<ID2D1SolidColorBrush> brush_;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void Render();
    D2D1_POINT_2F ToDip(LPARAM lp) const;

    Surface surface_;
    ComPtr<IDWriteTextFormat> titleFormat_;
    std::wstring title_;
    Options options_;
    bool redrawPending_ = false;
    bool rendering_ = false;
    bool trackingMouse_ = false;
    bool hoverLights_ = false;
};
