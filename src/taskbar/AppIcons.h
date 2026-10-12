#pragma once
#include "../core/Graphics.h"
#include <string>

// Ikony wbudowane, rysowane wektorowo (ostre w każdym rozmiarze, styl Windows 11).
enum class BuiltinIcon { None, Console, PowerShell };

struct AppIcon {
    ComPtr<ID2D1Bitmap> bitmap;
    BuiltinIcon builtin = BuiltinIcon::None;
    std::wstring appName;  // nazwa programu (opis z exe), pokazywana w dymku nad ikoną
};

// Kolejność wyboru ikony dla okna:
//   1. własny PNG użytkownika: %LOCALAPPDATA%\Exploder\icons\<nazwa exe bez .exe>.png
//   2. ikona wbudowana (konsola, PowerShell)
//   3. duża ikona z pliku exe procesu
//   4. ikona okna (WM_GETICON / klasa okna)
AppIcon ResolveAppIcon(HWND hwnd, int sizePx, IWICImagingFactory* wic, ID2D1DeviceContext* dc);

// Bitmapa ikony zasobnika (cache w TrayIcon::bitmap; bitmapy D2D są wspólne dla kontekstów jednego urządzenia).
struct TrayIcon;
ID2D1Bitmap* TrayIconBitmap(TrayIcon& icon, IWICImagingFactory* wic, ID2D1DeviceContext* dc);

void DrawBuiltinIcon(ID2D1DeviceContext* dc, BuiltinIcon kind, const D2D1_RECT_F& r, float opacity);

std::wstring CustomIconsDir();

// Nazwa programu okna (opis z exe, np. "Discord") — z pamięcią podręczną, tania przy częstym wołaniu.
std::wstring AppNameForWindow(HWND hwnd);
