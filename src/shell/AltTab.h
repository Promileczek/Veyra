#pragma once
#include "../core/Graphics.h"
#include "../taskbar/AppIcons.h"
#include <dwmapi.h>
#include <string>
#include <vector>

// Własny Alt+Tab (bez explorera systemowy jest w stylu Windows 95): szklany panel z podglądami okien
// na żywo (miniatury DWM), nazwą i ikoną. Przechwytywany hakiem klawiatury niskiego poziomu.
//  Alt+Tab / Alt+Shift+Tab, strzałki — wybór; puszczenie Alt — przełącz; Esc — anuluj; klik — przełącz.
//  Krótkie Alt+Tab (bez przytrzymania) przełącza od razu, bez pokazywania panelu.
class AltTab {
public:
    bool Install(HINSTANCE inst, Graphics& g);
    void Uninstall();

private:
    struct Card {
        HWND hwnd = nullptr;
        std::wstring title;
        AppIcon icon;
        HTHUMBNAIL thumb = nullptr;
        D2D1_RECT_F rect{};  // DIP
    };

    static LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    bool OnKey(WPARAM wp, const KBDLLHOOKSTRUCT& k);  // true = połknij klawisz

    void Open(bool backwards);
    void Move(int delta);
    void ShowPanel();
    void Commit(int index);
    void Close();
    void Layout();
    void Render();
    void ReleaseThumbnails();

    HWND hwnd_ = nullptr;
    HHOOK hook_ = nullptr;
    Graphics* g_ = nullptr;
    Surface surface_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> titleFormat_;

    bool active_ = false;
    bool rendering_ = false;
    bool shown_ = false;
    std::vector<Card> cards_;
    int selected_ = 0;
    int hover_ = -1;
    int columns_ = 1;
    UINT dpi_ = 96;
    float widthDip_ = 0.f, heightDip_ = 0.f;
};
