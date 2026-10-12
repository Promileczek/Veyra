#pragma once
#include "Graphics.h"
#include <string>

// Dymek z podpisem (styl macOS): szklany, zaokrąglony, nad wskazanym punktem. Nie przyjmuje kliknięć.
class Tooltip {
public:
    bool Create(HINSTANCE inst, Graphics& g);
    // anchorX: środek nad którym stoi dymek, bottomY: dolna krawędź dymku (piksele ekranu)
    void Show(const std::wstring& title, const std::wstring& subtitle, int anchorX, int bottomY, UINT dpi);
    void Hide();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void Render();
    float Measure(const std::wstring& text, IDWriteTextFormat* format) const;

    HWND hwnd_ = nullptr;
    Graphics* g_ = nullptr;
    Surface surface_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> titleFormat_, subFormat_;
    ComPtr<IDWriteInlineObject> ellipsis_;
    std::wstring title_, subtitle_;
    UINT dpi_ = 96;
    float widthDip_ = 0.f, heightDip_ = 0.f;
};
