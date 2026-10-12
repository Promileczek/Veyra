#pragma once
#include "../core/Graphics.h"
#include "../shell/TrayService.h"
#include "Theme.h"
#include <functional>
#include <vector>

// Wysuwany panel nad przyciskiem "Tray" w docku: lista ikon zasobnika z opisami.
// Kliknięcia w wiersz idą do programu tak, jakby kliknięto jego ikonę w zasobniku.
class TrayFlyout {
public:
    bool Create(HINSTANCE inst, Graphics& g, TrayService& tray, const TaskbarTheme& theme,
                std::function<void()> onVisibilityChange);
    void Toggle(const RECT& anchor, UINT dpi);  // anchor: przycisk "Tray" we współrzędnych ekranu
    void Hide();
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void Refresh();  // zawartość zasobnika się zmieniła

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    bool CreateResources();
    void Place();
    void Render();
    int HitTest(LPARAM lp) const;
    void Send(int row, UINT mouseMsg);

    HWND hwnd_ = nullptr;
    Graphics* g_ = nullptr;
    TrayService* tray_ = nullptr;
    const TaskbarTheme* theme_ = nullptr;
    std::function<void()> onVisibilityChange_;

    Surface surface_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> titleFormat_;
    ComPtr<IDWriteTextFormat> rowFormat_;
    ComPtr<IDWriteInlineObject> ellipsis_;

    RECT anchor_{};
    UINT dpi_ = 96;
    float widthDip_ = 0.f;
    float heightDip_ = 0.f;
    int hover_ = -1;
    bool trackingMouse_ = false;
    std::vector<size_t> rows_;  // indeksy widocznych ikon w tray_->Icons()
};
