#pragma once
#include "../core/Graphics.h"
#include "../core/Tooltip.h"
#include "../shell/ScreenSnip.h"
#include "../shell/TrayService.h"
#include "../shell/WindowTracker.h"
#include "AppIcons.h"
#include "Theme.h"
#include "ClipboardPanel.h"
#include "RunBox.h"
#include "StartMenu.h"
#include "TrayFlyout.h"
#include <functional>
#include <unordered_map>
#include <vector>

// Grupa okien tego samego programu — jedna ikona w docku, kilka kropek pod nią; klik cykluje między oknami.
struct DockGroup {
    std::wstring key;              // nazwa programu (grupowanie)
    std::vector<HWND> windows;     // w kolejności z trackera
    bool active = false;           // któreś okno na pierwszym planie
    bool flashing = false;
};

// Pasek zadań: AppBar na dolnej krawędzi monitora głównego, przyciski okien, zegar.
// Dwa style (Theme.h): pełny pasek (Win11) albo pływający dock (macOS).
class Taskbar {
public:
    bool Create(HINSTANCE inst, Graphics& g);
    // Wywoływane tuż przed uruchomieniem explorer.exe przy wyjściu (np. zamknięcie pulpitu,
    // inaczej explorer zobaczy nasze okno powłoki i nie przejmie powłoki).
    void SetBeforeExplorer(std::function<void()> fn) { beforeExplorer_ = std::move(fn); }
    // Aplikacja Ustawienia zmieniła settings.txt
    void SetOnSettingsChanged(std::function<void()> fn) { onSettingsChanged_ = std::move(fn); }
    // Przełącznik "Kropki macOS w oknach" w menu docka
    void SetWindowDotsToggle(std::function<bool()> isOn, std::function<void(bool)> set) {
        dotsOn_ = std::move(isOn);
        setDots_ = std::move(set);
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    bool IsDock() const { return theme_.style == TaskbarStyle::Dock; }
    float DockWidthDip() const;

    bool CreateDeviceResources();
    void RecoverDevice();
    void ReserveSpace();    // rezerwuje obszar ekranu (AppBar) i ustawia okno
    void PositionWindow();  // dock zmienia szerokość razem z liczbą okien
    void ApplyWorkArea(bool restore);  // bez explorera nikt inny nie ustawi obszaru roboczego
    float TrayWidthDip() const;
    void ToggleTrayFlyout();
    void ToggleStartMenu();
    void DrawGlass(ID2D1DeviceContext* dc, const D2D1_ROUNDED_RECT& shape);
    void DrawVeyraLogo(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const D2D1_COLOR_F& color);  // logo „V” w docku
    static LRESULT CALLBACK WinKeyHook(int code, WPARAM wp, LPARAM lp);
    void ToggleDesktop();
    bool IconRectFor(HWND h, RECT* out) const;  // ikona okna w docku (piksele ekranu) — dla WindowAnim
    void UpdateTooltip();
    void HideMinimizedWindows(bool hide);
    static void ParkMinimized(HWND h);
    static void CALLBACK OnMinimize(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD);
    void RequestRedraw();
    void Layout();
    void Render();
    void ScheduleClock();

    int HitTest(LPARAM lp) const;
    void OnClick(int hit);
    void OnRightClick(int hit);
    void Quit(bool startExplorer);
    const AppIcon& IconFor(const TrackedWindow& w) const;
    const AppIcon& IconForHwnd(HWND h) const;
    void PrepareIcons();  // przed rysowaniem — ResolveAppIcon woła powłokę, która przetwarza komunikaty
    void BuildGroups();   // grupuje okna tego samego programu w jedną ikonę docka
    HWND GroupActivate(const struct DockGroup& g) const;  // okno do pokazania przy kliknięciu (cykl)
    void Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0.f);

    HWND hwnd_ = nullptr;
    Graphics* g_ = nullptr;
    Surface surface_;
    WindowTracker tracker_;
    TrayService tray_;
    TrayFlyout trayFlyout_;
    StartMenu startMenu_;
    RunBox runBox_;  // Win+R
    ClipboardPanel clip_;  // Win+V (Full)
    HHOOK winKeyHook_ = nullptr;
    bool winDown_ = false, winUsed_ = false;
    Tooltip tooltip_;
    ScreenSnip snip_;
    HINSTANCE inst_ = nullptr;
    TaskbarTheme theme_;
    std::function<void()> beforeExplorer_;
    std::function<void()> onSettingsChanged_;
    std::function<bool()> dotsOn_;
    std::function<void(bool)> setDots_;
    std::vector<HWND> hiddenByStart_;  // okna zminimalizowane przyciskiem Start (do przywrócenia)

    UINT dpi_ = 96;
    float widthDip_ = 0.f;
    float heightDip_ = 0.f;
    float iconSize_ = 0.f;
    RECT reserved_{};
    bool appbar_ = false;
    bool hiddenForFullscreen_ = false;
    int originalArrange_ = -1;
    HWINEVENTHOOK minimizeHook_ = nullptr;  // MINIMIZEDMETRICS::iArrange sprzed uruchomienia
    bool redrawPending_ = false;
    bool rendering_ = false;
    bool trackingMouse_ = false;
    int hover_ = -1;
    int pressed_ = -1;

    D2D1_RECT_F startRect_{};
    D2D1_RECT_F clockRect_{};
    D2D1_RECT_F dividerRect_{};
    std::vector<DockGroup> groups_;
    std::vector<D2D1_RECT_F> buttonRects_;  // jeden na grupę (groups_)
    D2D1_RECT_F trayRect_{};  // przycisk "Tray" (pusty, gdy zasobnik nieaktywny)

    ComPtr<ID2D1SolidColorBrush> brush_;
    // Szkło docka: rozmyty dolny pas tapety
    ComPtr<ID2D1Bitmap> glassStrip_;
    ComPtr<ID2D1Effect> glassBlur_;
    RECT glassMonitor_{};
    LONG glassStripTop_ = 0;
    ComPtr<IDWriteTextFormat> clockFormat_;
    std::unordered_map<HWND, AppIcon> icons_;
};
