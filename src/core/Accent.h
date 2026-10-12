#pragma once
#include <windows.h>

// Rozmycie tła okna liczone przez DWM (tanie: bez własnego przechwytywania ekranu).
enum class Backdrop { None, Blur, Acrylic };

// tintAbgr: kolor nakładki w formacie 0xAABBGGRR.
bool SetBackdrop(HWND hwnd, Backdrop kind, DWORD tintAbgr);
