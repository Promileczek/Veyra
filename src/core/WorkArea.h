#pragma once
#include <windows.h>

// Obszar roboczy monitora głównego, gdy explorer.exe nie działa (wtedy AppBar API nie działa i nikt inny
// go nie ustawi). Każdy pasek zgłasza, ile pikseli zajmuje przy swojej krawędzi.
namespace WorkArea {
void Reserve(UINT edge, int pixels);  // edge: ABE_TOP / ABE_BOTTOM
void Restore();                       // cały monitor (przy wyjściu)

// Okno na pełnym ekranie (gra, film) — wtedy paski chowamy pod spód. Potrzebne bez explorera,
// bo tylko on wysyła ABN_FULLSCREENAPP.
bool IsFullscreenWindow(HWND hwnd);
}
