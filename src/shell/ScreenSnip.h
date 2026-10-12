#pragma once
#include <windows.h>

// Wycinek ekranu (Win+Shift+S): zamrożony, przyciemniony ekran z paskiem u góry
// [Wycinek] [Cały ekran] [✕]. Wycinek = zaznacz prostokąt myszą; Cały ekran = od razu cały zrzut.
// Wynik trafia do schowka. Esc albo prawy przycisk = anuluj. Pamięć (2 kopie ekranu) tylko na czas wycinania.
class ScreenSnip {
public:
    void Start(HINSTANCE inst);
    bool Active() const { return hwnd_ != nullptr; }

private:
    enum Button { BtnNone = -1, BtnRegion, BtnFull, BtnClose, BtnCount };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    RECT Selection() const;
    void Invalidate(const RECT& a, const RECT& b);
    void LayoutToolbar();
    int ButtonAt(POINT p) const;
    void PaintToolbar(HDC dc);
    void CopyToClipboard(const RECT& area);
    void Finish(bool copy);

    HWND hwnd_ = nullptr;
    RECT screen_{};  // wirtualny ekran (wszystkie monitory)
    HDC shotDC_ = nullptr, dimDC_ = nullptr;
    HBITMAP shot_ = nullptr, dim_ = nullptr;
    HGDIOBJ shotOld_ = nullptr, dimOld_ = nullptr;
    HFONT textFont_ = nullptr, glyphFont_ = nullptr;
    POINT start_{}, end_{};
    bool dragging_ = false;

    RECT toolbar_{};
    RECT buttons_[BtnCount]{};
    int hover_ = BtnNone;
};
