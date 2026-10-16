# Veyra — start nowej sesji (stan po 13.10.2026)

Wklej to (albo każ przeczytać ten plik) na początku nowej sesji. Szczegóły: `projekt.md` (sekcja „Stan”, wpisy z 13.10), plan: `PLAN_1.2.md`.

## Projekt i miejsca
- Veyra = własna powłoka Windows zamiast explorer.exe (w kodzie „Exploder”). C++20, D3D11 + D2D + DirectComposition.
- Kod: `X:\projekty\Windows - Exploder.exe rework` (udział sieciowy \\192.168.0.119\pendrive, nie repo git).
- GitHub: https://github.com/Promileczek/Veyra (main). Commity z klonu w scratchpadzie sesji (pull --rebase → kopia `src apps res tools CMakeLists.txt build.bat projekt.md VERSION PLAN_1.2.md` → commit z dopiskiem Co-Authored-By / Claude-Session → push). README należy do użytkownika.
- Wydania: v1.0, v1.1, v1.1.5, **v1.1.5.2 (ostatnie)** — pliki `VeyraSetup.exe` i `Veyra-<wersja>.zip` (6 programów, tapety, CZYTAJ.txt). Tworzone przez API GitHuba tokenem z `git credential fill`. Opis zawsze: instalacja / nowe / naprawione / czego brak / znane błędy.
- Wersja: plik `VERSION` (makro VEYRA_VERSION) + FILEVERSION/„FileVersion” w `apps/*/*.rc`.

## Budowanie i testy
- Build do folderu w scratchpadzie: bat z vswhere → vcvars64 → `cmake -S X:\… -B <scratch>\b -G Ninja -DCMAKE_BUILD_TYPE=Release` → `cmake --build`. (`build.bat` projektu buduje do `%LOCALAPPDATA%\Exploder\build`.)
- Cele: exploder, exploder-terminal, -calc, -store, -settings, **veyra-setup → VeyraSetup.exe** (programy i tapety w środku jako RCDATA, `setup_payload.rc` z CMake).
- Instalacja na tym komputerze: uruchomić VeyraSetup.exe i „kliknąć” Zainstaluj (PostMessage na 537,410 DIP); wybór Minimal/Full kliknięciem kart (170,190 / 470,190).
- Powłokę zamykać `taskkill /f` (kod 1). Stop-Process (kod -1) i kod 0 → Windows sam wznawia Eksplorator.
- Zrzuty tylko okien Veyry (PrintWindow / składanie okien procesu exploder) — nie całego ekranu.
- Sesja Claude Code ma NO_COLOR=1 itp. — Veyra przy starcie odtwarza środowisko użytkownika (Environment::ResetFromUser), więc to już nie przecieka.

## Stan funkcji (1.1.5.2)
- **Instalator** (`apps/setup`): Minimal / Full, „Używaj Veyry od razu”, Program Files\Veyra, UAC tylko dla plików i HKLM (`/wykonaj …`; od rundy 3 z 13.10.2026 w buildzie także zadanie Harmonogramu `\Veyra\Veyra Update` — `/zadanie`, `/wykonaj zadanie <SID>`, niezainstalowane), deinstalator (`/odinstaluj`, kopia w %TEMP%), wpis w Aplikacjach Windows. Minimal startuje z HKCU Run, Full jako HKCU Winlogon Shell. Zamykanie Veyry komunikatem „VeyraQuit”.
- **Minimal**: Eksplorator w tle (pasek/pulpit schowane — `src/shell/ExplorerUi`), zasobnik Veyry przed Eksploratorem (TrayService, jak ManagedShell), klawisz Win → menu Veyry (maska 0xE8), działają aplikacje UWP, GSMTC, powiadomienia Windows. Panel Pliki jest; okien Pliki, kropek i aplikacji Veyry nie ma (oprócz Ustawień: Ekran + Informacje).
- **Full**: bez Eksploratora; Kalkulator Veyry zamiast systemowego (IFEO calc.exe + ms-calculator:, Kalkulator ze Sklepu odinstalowany — deinstalacja go przywraca).
- **Skala Veyry** (`src/core/UiScale.h`): auto (dopasowanie do ekranu, 1600×900 DIP) albo 75–175%, zmiana na żywo (komunikat „VeyraUiScaleChanged”).
- **Ustawienia**: Personalizacja (Twoje tapety do 3 + tapeta sprzed Veyry, tapety Veyry), Pulpit i okna, **Ekran** (skala, monitory: EDID/Hz/skalowanie, rozdzielczość i Hz z potwierdzeniem 15 s, układanie przeciąganiem), **Aplikacje** (aplikacje Veyry + zainstalowane programy z wyszukiwaniem i Odinstaluj), System, **Aktualizacje** (auto-sprawdzanie), **Informacje** (Veyra, komputer, skróty).
- **Powiadomienie o aktualizacji** (`src/shell/Notification`): sprawdzanie minutę po starcie i co 6 h, karta w prawym górnym rogu, wjazd/wyjazd z animacją, 12 s. Test: `updateNotifyTest=1` w settings.txt.
- **Win+V** — historia schowka (Full, `src/taskbar/ClipboardPanel`).
- Dock: ikony UWP z AUMID bez płytki; kropki macOS jadą z przeciąganym oknem; górny pasek: zwarty blok muzyki na środku.
- Aktualizator (`apps/settings/Updater.h`): taskkill przez `cmd /c` (błąd z 1.1.5 naprawiony w 1.1.5.2), przy błędzie zawsze uruchamia Veyrę; **stan 1.1.5.2 (zainstalowane wydanie)**: w Program Files skrypt przez UAC, potem Veyra przez `runas /trustlevel:0x20000`. **W buildzie po 1.1.5.2 (niezainstalowane, 13.10.2026)**: skrypt z konta użytkownika, tylko podmiana plików jako administrator (`-CopyFrom`, RunAs), bez `runas /trustlevel`; przy zadaniu Harmonogramu i oficjalnym wydaniu bez UAC (prośby „pobierz” → „zainstaluj”); Full zamyka Eksplorator wchodzący na miejsce powłoki (`ExplorerUi::CloseExplorerShell`). Szczegóły: projekt.md, Runda 3.
- Diagnostyka: `VEYRA_VT_LOG=plik` — surowe wyjście konsoli w Terminalu.

## Stan tego komputera
- Zainstalowana Veyra **Full 1.1.5** z poprawionym skryptem aktualizacji (do testu powiadomienia o 1.1.5.2; użytkownik zaktualizował przez powiadomienie — wtedy wrócił Eksplorator, błąd wpisany w PLAN_1.2.md).
- Nowszy build (runda 3, 13.10.2026: poprawka Eksploratora po aktualizacji, zadanie Harmonogramu, przewijanie w Terminalu) jest zbudowany, ale nie zainstalowany.
- uiScale=auto. Monitor 24B31H 1920×1080 120 Hz. Laptop użytkownika: 1920×1080 przy 150%.

## Ustalenia i ograniczenia
- Przy każdej nowej funkcji pytać: Minimal, Full czy obie.
- Bez explorer.exe jako powłoki nie działają: aplikacje UWP (0x87b20c15), GSMTC („Teraz odtwarzane” — klasa NowPlayingSessionManager z ImmersiveShellBrokers, NPSM.dll/twinui.appcore.dll), powiadomienia Windows.
- Okna UAC i ekranu Ctrl+Alt+Del nie da się podmienić (zabezpieczony pulpit). Rozpracowywania i uruchamiania wewnętrznych komponentów powłoki Windows nie robimy.
- Styl: grafit, akcent #61B3FF, płasko, nieprzezroczyste okna aplikacji, kropki po prawej. Odpowiedzi po polsku, zwięźle.

## Co dalej
Lista w **PLAN_1.2.md**: błędy (m.in. Eksplorator wraca po aktualizacji z UAC, znikające/puste kropki, obwódki ikon i paska, tray bez nazw, Ustawienia nachodzą na kropki, kalkulator na pełnym ekranie, odstęp folderów) i zadania (UAC raz przy instalacji + zadanie Harmonogramu, animacje, bateria, Menedżer zadań Veyry, ekran opcji jak Ctrl+Alt+Del, nowy układ Ustawień, Konta „w budowie”, centrum powiadomień i szybkie funkcje, urządzenia audio, kursor, grupowanie okien w docku, ikona Veyry i folderu, przyciąganie okien, wybór zawartości Sklepu, wyłączanie statystyk, muzyka w Full — decyzja).
