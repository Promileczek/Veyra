#pragma once
#include "Graphics.h"
#include <string>

// Szklane okienko wysuwane pod przyciskiem (głośność, sieć…). Aktywne po otwarciu — kliknięcie
// gdziekolwiek indziej albo Esc je zamyka. Podklasa podaje rozmiar i rysuje zawartość (w DIP).
// windowMode_ (ustawione przed Create, np. "Uruchom"): zwykłe okno Veyry — nieprzezroczyste, kropki w prawym górnym
// rogu, przeciąganie za górny pas, minimalizacja do docka; nie znika po kliknięciu obok, Esc/kropka/Alt+F4 chowa.
class Popup {
public:
    virtual ~Popup() = default;
    bool Create(HINSTANCE inst, Graphics& g, const wchar_t* className);
    void Toggle(const RECT& anchor, UINT dpi);  // anchor: przycisk (piksele ekranu); okienko pod nim (albo nad, gdy brak miejsca)
    void Hide();
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void Redraw();    // zawartość się zmieniła
    void Relayout();  // zmienił się też rozmiar

protected:
    virtual D2D1_SIZE_F ContentSize() = 0;
    virtual void Paint(ID2D1DeviceContext* dc) = 0;
    virtual void OnShow() {}
    virtual void OnMouseMove(D2D1_POINT_2F) {}
    virtual void OnMouseDown(D2D1_POINT_2F) {}
    virtual void OnMouseUp(D2D1_POINT_2F) {}
    virtual void OnWheel(int) {}
    virtual void OnChar(wchar_t) {}
    virtual void OnTimer(UINT_PTR) {}

    // Pomocnicze do rysowania
    void Fill(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0.f);
    void Text(ID2D1DeviceContext* dc, const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    void Glyph(ID2D1DeviceContext* dc, wchar_t g, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    static bool Inside(const D2D1_RECT_F& r, D2D1_POINT_2F p) {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }
    void Capture(bool on);

    HWND hwnd_ = nullptr;
    Graphics* g_ = nullptr;
    bool windowMode_ = false;
    const wchar_t* windowTitle_ = L"";  // tytuł okna (dock, Alt+Tab) w windowMode_
    UINT dpi_ = 96;
    D2D1_SIZE_F size_{};
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> titleFormat_, textFormat_, smallFormat_, glyphFormat_;

    static constexpr D2D1_COLOR_F kText{1.f, 1.f, 1.f, 1.f};
    static constexpr D2D1_COLOR_F kDim{1.f, 1.f, 1.f, 0.6f};
    static constexpr D2D1_COLOR_F kHover{1.f, 1.f, 1.f, 0.10f};
    static constexpr D2D1_COLOR_F kAccent{0.38f, 0.70f, 1.f, 1.f};

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void Place();
    void Render();
    D2D1_POINT_2F ToDip(LPARAM lp) const;
    D2D1_POINT_2F Lights() const { return {size_.width - 58.f, 20.f}; }  // środek pierwszej kropki (windowMode_)

    Surface surface_;
    RECT anchor_{};
    bool trackingMouse_ = false;
    bool captured_ = false;
    bool placed_ = false;       // windowMode_: pozycja wybrana — kolejne otwarcia tam, gdzie użytkownik je zostawił
    bool active_ = true;        // windowMode_
    bool hoverLights_ = false;  // windowMode_
};
