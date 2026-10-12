#pragma once
#include "../core/Graphics.h"
#include "AppPicker.h"
#include <shobjidl.h>
#include <functional>
#include <string>
#include <vector>

// Panel na środku pulpitu: przypięte przez użytkownika aplikacje/pliki/foldery + wyszukiwarka.
//  - bez wpisanego tekstu: przypięte (domyślnie Kosz i Ten komputer) + kafelek "Dodaj",
//  - z tekstem: wyniki z przypiętych i ze wszystkich aplikacji (lista ładowana dopiero przy pierwszym szukaniu).
// Przypięte: %LOCALAPPDATA%\Exploder\launcher.txt (jedna nazwa powłoki w linii).
class AppLauncher {
public:
    // requestRedraw: przerysuj; sizeChanged: zmieniła się wysokość panelu (trzeba przeliczyć szkło pod nim)
    void Init(Graphics& g, std::function<void()> requestRedraw, std::function<void()> sizeChanged);
    void Load();
    float PreferredHeight(float width, float maxHeight) const;
    void SetBounds(const D2D1_RECT_F& r) { bounds_ = r; }
    const D2D1_RECT_F& Bounds() const { return bounds_; }
    bool Contains(D2D1_POINT_2F p) const;
    bool Searching() const { return !query_.empty(); }

    void PrepareIcons(float dpi);  // przed rysowaniem (patrz FileView::PrepareIcons)
    void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float dpi);

    void OnMouseMove(D2D1_POINT_2F p);
    void OnMouseLeave();
    void OnClick(D2D1_POINT_2F p, HWND owner);
    void OnRightClick(D2D1_POINT_2F p, POINT screen, HWND owner);
    void OnWheel(int delta);
    void OnChar(wchar_t ch, HWND owner);  // pisanie na pulpicie od razu trafia do wyszukiwarki
    void SetFocused(bool focused);

    std::function<void()> openFiles;                       // kafelek "Pliki" — przywraca panel plików
    std::function<void(ComPtr<IShellItem>)> openFolder;   // foldery (Ten komputer, Kosz…) w oknie Explodera

private:
    struct Entry {
        ComPtr<IShellItem> item;
        std::wstring key;  // nazwa powłoki (ścieżka, ::{CLSID}, aplikacja z folderu Aplikacje)
        std::wstring name, lower;
        ComPtr<ID2D1Bitmap> icon;
        bool iconLoaded = false;
        ComPtr<IWICBitmapSource> pendingIcon;
        ComPtr<IDWriteTextLayout> label;
    };
    static constexpr int kAddTile = -2;    // w view_: kafelek "Dodaj"
    static constexpr int kFilesTile = -3;  // w view_: kafelek "Pliki" (panel po lewej)
    ComPtr<ID2D1Bitmap> filesIcon_;

    static bool MakeEntry(ComPtr<IShellItem> item, Entry& out);
    void LoadAllApps();
    void SavePinned() const;
    void Pin(ComPtr<IShellItem> item);
    void Unpin(const std::wstring& key);
    bool IsPinned(const std::wstring& key) const;
    void OpenPicker(HWND owner, const RECT& anchor);
    void RebuildView();
    void Launch(Entry& entry);
    Entry* EntryAt(int viewIndex);

    D2D1_RECT_F SearchRect() const;
    D2D1_RECT_F GridRect() const;
    int Columns() const;
    int VisibleRows() const;
    int ViewIndexAt(D2D1_POINT_2F p) const;  // indeks w view_ albo -1
    D2D1_RECT_F CellRect(int visibleIndex) const;

    Graphics* g_ = nullptr;
    std::function<void()> requestRedraw_, sizeChanged_;
    ComPtr<IDWriteTextFormat> labelFormat_, searchFormat_, glyphFormat_;
    ComPtr<IDWriteInlineObject> ellipsis_;

    D2D1_RECT_F bounds_{};
    std::vector<Entry> pinned_;
    std::vector<Entry> allApps_;
    bool allAppsLoaded_ = false;
    // Widok: wartości >= 0 to indeks w pinned_, <= -100 to (-100 - indeks) w allApps_, kAddTile = "Dodaj"
    std::vector<int> view_;
    std::wstring query_;
    int scrollRow_ = 0;
    int hover_ = -1;
    bool focused_ = false;
    AppPicker picker_;
    bool pickerCreated_ = false;
};
