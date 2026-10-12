#pragma once
#include "../core/Graphics.h"
#include <shobjidl.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct DesktopItem {
    ComPtr<IShellItem> item;
    std::wstring key;   // pełna ścieżka albo ::{CLSID} — trwały identyfikator (zapis układu ikon)
    std::wstring name;  // nazwa wyświetlana (rozszerzenie wg ustawień Windows)
    int col = -1, row = -1;
    bool selected = false;
    bool iconLoaded = false;
    ComPtr<ID2D1Bitmap> icon;
    ComPtr<IDWriteTextLayout> label;
};

// Kosz + pulpit użytkownika + pulpit publiczny (bez plików ukrytych).
std::vector<DesktopItem> EnumerateDesktopItems();
std::wstring ItemKey(IShellItem* item);
bool IsRecycleBin(const DesktopItem& item);

// Miniatura (obrazy) albo ikona pliku, 32bppPBGRA.
ComPtr<IWICBitmapSource> LoadItemImage(IShellItem* item, int sizePx, IWICImagingFactory* wic);
// Aktualna tapeta Windows przeskalowana w trybie "wypełnij" do width × height, 32bppPBGRA.
ComPtr<IWICBitmapSource> LoadWallpaper(IWICImagingFactory* wic, UINT width, UINT height);

// Położenie ikon na siatce: %LOCALAPPDATA%\Exploder\desktop-layout.txt (kolumna \t wiersz \t klucz).
using IconLayout = std::map<std::wstring, std::pair<int, int>>;
IconLayout LoadIconLayout();
void SaveIconLayout(const IconLayout& layout);
