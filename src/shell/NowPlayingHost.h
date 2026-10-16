#pragma once

// Veyra Full bez Eksploratora: usługa „teraz odtwarzane” Windows (GSMTC — Spotify, YouTube, karty przeglądarki…).
// Normalnie uruchamia ją explorer.exe jako powłoka (NowPlayingSessionManager z NPSM.dll, klasa COM
// {BCBB9860-…} z grupy ImmersiveShellBrokers); bez niego programy nie mają gdzie zgłaszać muzyki, a GSMTC zwraca
// „Klasa niezarejestrowana”. Robimy to samo co Eksplorator:
//  - RegisterNowPlayingSessionManagerFactory z NPSM.dll (fabrykę przechwytujemy z jej CoRegisterClassObject —
//    sama rejestruje się jako wstrzymana, a CoResumeClassObjects odrzuca ją RPC_X_INVALID_BOUND),
//  - własna fabryka-nakładka zarejestrowana zamiast niej (QueryInterface i CreateInstance → fabryka NPSM),
//  - atrapa ImmersiveShell {C2F03A33-…} z IServiceProvider → IApplicationViewCollection (NPSMDesktopProvider
//    wymaga jej przy tworzeniu menedżera; wystarcza rejestracja nasłuchu zmian, reszta E_NOTIMPL).
// Nieudokumentowane — sprawdzone na Windows 10 22H2 (19045). Wyłącznik: settings.txt nowPlayingHost=0.
namespace NowPlayingHost {
bool Start();  // przed utworzeniem MediaService; false = nie wystartowało (zostaje tryb zastępczy)
void Stop();   // przed uruchomieniem Eksploratora jako powłoki i przy wyjściu
}  // namespace NowPlayingHost
