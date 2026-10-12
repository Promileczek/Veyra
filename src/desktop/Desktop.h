#pragma once
#include "../core/Graphics.h"
#include "AppLauncher.h"
#include "FilesWidget.h"
#include "StatsWidget.h"
#include <functional>
#include "DesktopItems.h"
#include <shobjidl.h>
#include <string>
#include <thread>
#include <vector>

// Pulpit: tapeta + ikony na siatce. Okno na samym dole Z-order, zarejestrowane jako okno powłoki
// (SetShellWindow) — dzięki temu explorer.exe uruchomiony do otwarcia folderu nie przejmuje powłoki.
class Desktop {
public:
    // openFolder: otwiera folder w oknie Explodera (Ten komputer, Kosz, foldery z panelu)
    bool Create(HINSTANCE inst, Graphics& g, std::function<void(ComPtr<IShellItem>)> openFolder);
    void Destroy();

private:
    enum class Mode { None, Pending, Dragging, Selecting };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    bool CreateResources();
    void DrawWallpaper();
    RECT IconBounds() const;  // px, obejmuje ikony (także przeciągane) i ramkę zaznaczania
    void UpdateWorkArea();
    void Refresh();
    void AssignPositions();
    void RequestRedraw();
    void Render();

    D2D1_RECT_F CellRect(int col, int row) const;
    D2D1_RECT_F ContentRect(const DesktopItem& item) const;
    int ItemAt(float x, float y) const;
    D2D1_POINT_2F ToDip(LPARAM lp) const;
    ID2D1Bitmap* IconFor(DesktopItem& item);
    IDWriteTextLayout* LabelFor(DesktopItem& item);

    void SelectOnly(int index);
    void DropDragged();
    void UpdateRubberBand();
    void OpenSelected();
    void DeleteSelected(bool permanent);
    void StartRename(int index);
    void CommitRename(bool accept);
    void ShowItemMenu(int index, POINT screen);
    void ShowDesktopMenu(POINT screen);
    void NewFolder();
    void ChooseWallpaper();

    void StartWatcher();
    void StopWatcher();

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    Graphics* g_ = nullptr;
    // Dwie warstwy DirectComposition (oszczędność pamięci vs. swapchain z 2 buforami + kopia tapety):
    //  - tapeta: zwykła powierzchnia, rysowana raz przy zmianie tapety,
    //  - ikony: powierzchnia wirtualna — pamięć tylko na kafelki, w których faktycznie coś jest.
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDCompositionTarget> compTarget_;
    ComPtr<IDCompositionVisual> root_, wallVisual_, iconVisual_;
    ComPtr<IDCompositionSurface> wallSurface_;
    ComPtr<IDCompositionVirtualSurface> iconSurface_;
    RECT lastBounds_{};  // obszar warstwy ikon zajęty przy poprzednim rysowaniu (px)
    ComPtr<IDWriteTextFormat> labelFormat_;
    ComPtr<IDWriteInlineObject> ellipsis_;

    UINT dpi_ = 96;
    RECT monitor_{};
    float widthDip_ = 0.f, heightDip_ = 0.f;
    D2D1_RECT_F work_{};  // obszar roboczy (bez docka) w DIP, względem okna
    int cols_ = 1, rows_ = 1;
    bool redrawPending_ = false;
    bool rendering_ = false;  // blokada ponownego wejścia w Render (wywołania powłoki przetwarzają komunikaty)

    AppLauncher launcher_;
    FilesWidget files_;
    StatsWidget stats_;
    float statsHeight_ = 0.f;
    std::function<void(ComPtr<IShellItem>)> openFolder_;
    bool filesCapture_ = false;
    float launcherTop_ = 0.f;
    std::vector<DesktopItem> items_;
    IconLayout layout_;

    Mode mode_ = Mode::None;
    D2D1_POINT_2F downPt_{}, curPt_{};
    int downItem_ = -1;
    int hover_ = -1;
    bool trackingMouse_ = false;

    HWND renameEdit_ = nullptr;
    int renameItem_ = -1;
    std::wstring renameOriginal_;

    IContextMenu2* activeMenu_ = nullptr;  // podmenu ("Wyślij do", "Otwórz za pomocą") wymagają przekazywania komunikatów

    std::thread watcher_;
    HANDLE stopEvent_ = nullptr;
};
