#pragma once
#include "../core/Popup.h"
#include <shobjidl.h>
#include <functional>
#include <vector>

// Okienko "Dodaj do panelu" (kafelek + w panelu aplikacji): lista zainstalowanych aplikacji z wyszukiwaniem.
// Klik → onPick(aplikacja); "Inny plik…" → onBrowse (okno Explodera w trybie wyboru).
class AppPicker : public Popup {
public:
    struct App {
        std::wstring name, lower, key;
        ComPtr<IShellItem> item;
        bool pinned = false;
        bool iconLoaded = false;
        ComPtr<IWICBitmapSource> pendingIcon;
        ComPtr<ID2D1Bitmap> icon;
    };
    void Open(const RECT& anchor, UINT dpi, std::vector<App> apps);

    std::function<void(ComPtr<IShellItem>)> onPick;
    std::function<void()> onBrowse;

protected:
    D2D1_SIZE_F ContentSize() override;
    void Paint(ID2D1DeviceContext* dc) override;
    void OnMouseMove(D2D1_POINT_2F p) override;
    void OnMouseDown(D2D1_POINT_2F p) override;
    void OnWheel(int delta) override;
    void OnChar(wchar_t ch) override;

private:
    enum { HoverNone = -1, HoverBrowse = -2 };
    void Filter();
    void LoadVisibleIcons();  // poza rysowaniem (wywołania powłoki potrafią pompować komunikaty)
    int RowAt(D2D1_POINT_2F p) const;
    int VisibleRows() const;

    std::vector<App> apps_;
    std::vector<size_t> view_;  // indeksy w apps_ po filtrze
    std::wstring query_;
    int scroll_ = 0;
    int hover_ = HoverNone;
};
