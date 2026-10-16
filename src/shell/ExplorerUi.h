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
// Strażnik (osobny proces "exploder.exe --straznik <pid>"): Veyrę zabito (Menedżer zadań) — pasek i pulpit Eksploratora
// z powrotem; stan paska sprzed Veyry zapisany w settings.txt (minimalTaskbarState)
void RestoreAfterKill();
bool IsExplorerWindow(HWND h);  // okno procesu explorer.exe (nie nasze Shell_TrayWnd)

// Veyra Full jako powłoka Windows (HKCU Winlogon Shell → ten exe). Eksplorator potrafi wtedy wejść na miejsce powłoki,
// gdy Veyry chwilę nie ma (np. przy aktualizacji) — i stoi potem jego pasek obok Veyry.
bool IsConfiguredShell();
bool CloseExplorerShell();  // zamyka explorer.exe z paskiem zadań; true = był
}  // namespace ExplorerUi
