#pragma once
#include <windows.h>

// Veyra Minimal: explorer.exe zostaje powłoką Windows w tle (aplikacje UWP — Kalkulator, Ustawienia, Sklep —
// powiadomienia, skróty Win+klawisz działają tylko z nim). Veyra chowa jego pasek zadań (autoukrywanie + ukrycie)
// i pulpit (Progman), a rysuje swoje. Przy wyjściu (też przy awarii) wszystko wraca.
namespace ExplorerUi {
bool EnsureRunning();  // uruchamia explorer.exe, gdy nie działa, i czeka na jego pasek
void Hide();
void KeepHidden();  // co sekundę: Eksplorator pokazuje pasek/pulpit po restarcie, Win+D itp.
void Restore();
bool IsExplorerWindow(HWND h);  // okno procesu explorer.exe (nie nasze Shell_TrayWnd)
}  // namespace ExplorerUi
