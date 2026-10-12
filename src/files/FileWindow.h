#pragma once
#include "../core/Graphics.h"
#include "../core/TrafficLights.h"
#include "FileView.h"
#include <functional>
#include <string>
#include <vector>

// Okno Explodera (menedżer plików w stylu Findera): kropki macOS (zamknij / minimalizuj / maksymalizuj),
// pasek boczny (ulubione, dyski, Kosz), wstecz/dalej/w górę, nowy folder, siatka plików (FileView).
void OpenFileWindow(HINSTANCE inst, Graphics& g, ComPtr<IShellItem> folder);
// Okno Explodera w trybie wyboru: dwuklik na pliku → onPick(plik) i okno się zamyka
void OpenFilePicker(HINSTANCE inst, Graphics& g, ComPtr<IShellItem> folder, const std::wstring& prompt,
                    std::function<void(ComPtr<IShellItem>)> onPick);
void CloseAllFileWindows();

class FileWindow {
public:
    bool Create(HINSTANCE inst, Graphics& g, ComPtr<IShellItem> folder);
    void SetPicker(const std::wstring& prompt, std::function<void(ComPtr<IShellItem>)> onPick);
    HWND Hwnd() const { return hwnd_; }

private:
    struct SidebarEntry {
        std::wstring label;
        std::wstring parsing;  // puste = nagłówek sekcji
        ComPtr<IShellItem> item;
        ComPtr<ID2D1Bitmap> icon;
        bool iconLoaded = false;
        ComPtr<IWICBitmapSource> pendingIcon;
    };
    enum Hit { HitNone = -1, HitBack = 100, HitForward, HitUp, HitNewFolder };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void BuildSidebar();
    void Layout();
    void Render();
    void RequestRedraw();
    int HitTest(D2D1_POINT_2F p) const;  // przyciski paska narzędzi / wpisy paska bocznego (>= 0)
    D2D1_POINT_2F ToDip(LPARAM lp) const;
    D2D1_POINT_2F Lights() const;  // środek pierwszej kropki

    HWND hwnd_ = nullptr;
    HINSTANCE inst_ = nullptr;
    Graphics* g_ = nullptr;
    Surface surface_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> titleFormat_, sideFormat_, sectionFormat_, glyphFormat_, statusFormat_;
    FileView view_;
    std::vector<SidebarEntry> sidebar_;

    UINT dpi_ = 96;
    float widthDip_ = 0.f, heightDip_ = 0.f;
    bool redrawPending_ = false;
    bool rendering_ = false;
    bool trackingMouse_ = false;
    bool hoverLights_ = false;
    bool active_ = true;
    int hover_ = HitNone;
    bool viewCapture_ = false;
    std::wstring shownTitle_;
    std::wstring pickPrompt_;  // tryb wyboru: podpowiedź w pasku stanu
};
