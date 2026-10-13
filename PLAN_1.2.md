# Veyra 1.2 — plan

Stan wyjściowy: **1.1.5.2** (13.10.2026). Plik zbiera wszystko, co zostało otwarte po 1.1.x: nowe funkcje, błędy do naprawy i rzeczy do przetestowania.
Przy każdej funkcji jest proponowana wersja (**Full**, **Minimal** albo **obie**) — do potwierdzenia przed rozpoczęciem pracy.

## Nowe funkcje

### Muzyka (górny pasek)
- [ ] **Spotify Web API** — okładka, czas, pasek postępu, stan gra/pauza dla Spotify w przeglądarce, w programie i na telefonie. Raz: własny Client ID z developer.spotify.com wklejony w Ustawieniach + logowanie (PKCE). Odczyt bez Premium; sterowanie lokalnie (polecenia do okna Spotify / klawisze multimedialne), bo sterowanie przez API wymaga Premium. — **Full** (Minimal ma to z Windows)
- [ ] **Karty przeglądarki w tle** — odczyt kart Brave/Chrome/Edge przez UI Automation; karta, która gra dźwięk (oznaczenie „odtwarzanie dźwięku”), zamiast tylko aktywnej. — **Full**
- [ ] **Stan gra/pauza z miksera dźwięku** (Core Audio: kto faktycznie wydaje dźwięk). — **Full**
- [ ] **Miniatura YouTube** dla aktywnej karty (adres z paska adresu → publiczna miniatura filmu). — **Full**

### Ekran i wygląd
- [ ] **Wiele monitorów** — pulpit, dock i górny pasek na każdym monitorze albo wybór monitora głównego dla Veyry. — **obie**
- [ ] **Skalowanie Windows (DPI) dla każdego monitora** z Ustawień Veyry (100–175%), bo w Full Ustawienia Windows nie działają. — **obie**
- [ ] **Motywy** — co najmniej jasny / ciemny i kolor akcentu (dziś stały #61B3FF). — **obie**

### System
- [ ] **Powiadomienia programów (dymki zasobnika, NIF_INFO)** pokazywane kartą Veyry — ta sama karta co powiadomienie o aktualizacji (`src/shell/Notification`). — **Full** (Minimal ma powiadomienia Windows)
- [ ] **Odinstalowywanie aplikacji ze Sklepu Microsoft** w Ustawienia → Aplikacje (Get-AppxPackage / Remove-AppxPackage, bez systemowych). — **Full**
- [ ] **Odinstalowywanie w Sklepie Veyry** (winget uninstall przy zainstalowanych). — **Full**
- [ ] **Win+V**: przypinanie wpisów, nawigacja klawiaturą (strzałki, Enter). — **Full**

### Terminal
- [ ] Karty (kilka konsol w jednym oknie). — **Full**
- [ ] Emoji i znaki azjatyckie na dwie kratki. — **Full**
- [ ] Mysz w programach pełnoekranowych (TUI). — **Full**

## Błędy do naprawy

- [ ] Puste czarne okienka z samymi kropkami macOS w rogach ekranu (nakładki WindowDots dla niewidocznych okien).
- [ ] Statystyki „Dziś w aplikacjach” pokazują „Application Frame Host” zamiast nazwy aplikacji ze Sklepu (użyć AUMID jak w docku).
- [ ] Kropki macOS: kolor tła nie zawsze trafia (nieaktywne okna, Discord po przełączeniu); działają tylko w zwykłych oknach, Brave/Chrome/Edge i Discordzie (VS Code, Spotify i inne Electron — nie).
- [ ] Alt+Tab: ciemny podgląd zminimalizowanych przeglądarek.
- [ ] Szkło docka pokazuje rozmytą tapetę zamiast okien za dockiem.
- [ ] Minimal: pasek zadań Windows miga po zalogowaniu, zanim Veyra go schowa.
- [ ] Minimal: po zabiciu Veyry (Menedżer zadań) pasek Windows zostaje schowany — np. strażnik w osobnym procesie albo przywracanie przy następnym logowaniu.
- [ ] Sklep: postęp instalacji nie zawsze w procentach.

## Do przetestowania (zrobione, ale nie sprawdzone na żywo)

- [ ] Zmiana rozdzielczości i Hz z potwierdzeniem 15 s (Ustawienia → Ekran).
- [ ] Układanie monitorów przeciąganiem (potrzebne 2 monitory).
- [ ] Odinstalowywanie programu z Ustawienia → Aplikacje (także z UAC).
- [ ] Skalowanie „Auto” na laptopie 1920×1080 przy 150%.
- [ ] Kropki macOS jadące razem z przeciąganym oknem.
- [ ] Instalator i deinstalator przy włączonym UAC (okno zgody), na koncie bez stałych uprawnień administratora.
- [ ] Aktualizacja z Ustawień przy instalacji w Program Files na koncie ze zwykłymi uprawnieniami (`runas /trustlevel` po aktualizacji).
- [ ] Menu Wi-Fi na komputerze z kartą Wi-Fi.
- [ ] Blok muzyki w górnym pasku z grającym utworem (zwarty, na środku).

## Przed wydaniem 1.2

- [ ] `VERSION` = 1.2, wersje w `apps/*/*.rc` = 1,2,0,0 / „1.2”.
- [ ] Pełny build, `VeyraSetup.exe` + `Veyra-1.2.zip` (programy, instalator, tapety, CZYTAJ.txt).
- [ ] Test: czysta instalacja Minimal i Full, aktualizacja z 1.1.5.2 przez Ustawienia (powiadomienie → Aktualizuj), deinstalacja.
- [ ] Opis wydania: nowe / naprawione / czego brak / znane błędy; projekt.md aktualny.
