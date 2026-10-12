# Windows - Exploder.exe rework

## Cel

Własna powłoka (shell) Windows zastępująca `explorer.exe`. System: Windows 10 Home 22H2 (19045).

## Wymagane moduły

1. **Pulpit** — tapeta, ikony (pliki/skróty z `%USERPROFILE%\Desktop` + `Public\Desktop`), przeciąganie, menu kontekstowe.
2. **Pasek zadań** — lista otwartych okien, przypinanie aplikacji, zegar, zasobnik systemowy (tray: głośność, sieć, ikony programów).
3. **Menu Start** — lista programów (skróty z folderów Start Menu + aplikacje UWP/Store), wyszukiwarka, wyłącz/uruchom ponownie/wyloguj.
4. **Menedżer plików** — przeglądanie dysków i folderów, kopiuj/wytnij/wklej/usuń, podgląd, zakładki.
5. **Powiadomienia** — wyświetlanie powiadomień (toast) i centrum powiadomień.
6. **Ustawienia** — panel ustawień własnej powłoki (tapeta, wygląd paska, autostart itp.).

## Wymaganie: niskie zużycie RAM (ustalone 11.10.2026)

Powłoka ma zabierać mało pamięci. Konsekwencje dla projektu:
- **Budżet docelowy:** cała powłoka w spoczynku (pulpit + pasek + tray) < ~50 MB prywatnej pamięci; menu Start / menedżer plików / powiadomienia ładowane na żądanie i zwalniane po zamknięciu.
- **Jeden proces C++** dla pulpitu, paska i tray — bez .NET/WPF w procesie powłoki. Narzędzia C# to osobne programy, uruchamiane tylko gdy są potrzebne (nie działają w tle).
- **Grafika:** Direct2D + DirectComposition, jedno wspólne urządzenie D3D11 dla wszystkich okien; shadery HLSL kompilowane wcześniej (pliki `.cso`), nie w locie.
- **Efekty szkła:** gdzie się da, korzystać z rozmycia DWM (acrylic/blur systemu) zamiast własnego przechwytywania ekranu — własny „Liquid Glass” z refrakcją kosztuje pamięć GPU i CPU, więc ma być opcjonalny (do wyłączenia w motywie).
- **Ikony i tapeta:** cache ikon z limitem rozmiaru, tapeta skalowana do rozdzielczości ekranu (nie trzymać oryginału 8K w pamięci).
- **Mierzyć na bieżąco:** każdy etap sprawdzać w Menedżerze zadań / `Get-Process` (Private Bytes, Working Set) i zapisywać wynik w sekcji „Stan”.

## Jak działa podmiana powłoki

- Windows uruchamia powłokę z rejestru `Winlogon\Shell`.
- Bezpiecznie: ustawienie **per użytkownik** w `HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon` → `Shell = ścieżka\do\naszego.exe`. Globalne `HKLM` zostaje `explorer.exe`.
- Na czas rozwoju: zabijamy `explorer.exe` i uruchamiamy naszą powłokę ręcznie — bez zmian w rejestrze.

## Rzeczy trudne / ryzyka

- **Tray** — programy wysyłają ikony do okna `Shell_TrayWnd` (`Shell_NotifyIcon` → `WM_COPYDATA`). Trzeba zarejestrować własne okno o tej klasie i obsłużyć protokół.
- **Lista okien** — `RegisterShellHookWindow` / `SetWinEventHook`, filtr okien „do paska” (jak robi explorer).
- **Obszar roboczy** — pasek jako AppBar (`SHAppBarMessage`), żeby zmaksymalizowane okna go nie zasłaniały.
- **Bez explorer.exe nie działają** (lub działają słabo): natywne toasty Windows, aplikacje UWP/Store, systemowe Ustawienia (`ms-settings:`), skróty Win+klawisz. Powiadomienia i skróty klawiszowe trzeba obsłużyć samemu.
- **Awaryjne wyjście** — zawsze musi być sposób na powrót do `explorer.exe` (skrót klawiszowy, Menedżer zadań → Uruchom `explorer.exe`, przywrócenie wpisu w rejestrze).

## Inspiracje / punkty odniesienia

- Cairo Desktop (C#/WPF) i biblioteka **ManagedShell** — gotowa obsługa tray, paska zadań, AppBar.
- Open-Shell, ExplorerPatcher — menu Start / pasek.

## Technologia (ustalone 11.10.2026)

- **C++** — główny silnik powłoki (Win32, okna, hooki, tray, AppBar, logika).
- **Direct2D / Direct3D + HLSL** — renderowanie UI, shadery i efekty graficzne (rozmycie, szkło, animacje).
- **C#** — narzędzia konfiguracyjne i pomocnicze (np. edytor motywów/ustawień), wsparcie dla części C++.
- **JSON** — konfiguracja wyglądu, ustawień i motywów.

### Narzędzia na komputerze (sprawdzone 11.10.2026)

- Brak Visual Studio / kompilatora MSVC i CMake — **trzeba doinstalować** (VS 2022 Build Tools: „Programowanie aplikacji klasycznych w C++” + Windows SDK).
- `dotnet` jest, ale bez SDK (tylko runtime) — do części C# trzeba doinstalować .NET 8 SDK.

## Wygląd (ustalone 11.10.2026)

Mieszanka stylów: **Windows 11 + Windows 10 + KDE Plasma + Liquid Glass (iOS)**.
- Liquid Glass → efekty szkła (rozmycie tła, refrakcja, połysk krawędzi) liczone shaderami HLSL.
- KDE Plasma → duża konfigurowalność (położenie paneli, widżety, motywy).
- Win11/Win10 → znajomy układ paska i menu Start.
- Styl ma być przełączalny/łączony przez motywy w JSON.

## Do ustalenia

- [x] Kolejność prac (ustalone 11.10.2026): **1. pasek zadań (+ tray) → 2. Exploder (menedżer plików) → 3. pulpit → 4. menu Start → 5. powiadomienia → 6. ustawienia**.

## Stan

- 11.10.2026 — utworzono folder i ten plik.
- 11.10.2026 — zainstalowano VS 2022 Build Tools (MSVC 14.44, Windows SDK 10.0.22621, CMake + Ninja) i .NET 8 SDK (8.0.425).
- 11.10.2026 — **pasek zadań v0.1** działa (uruchamiany obok explorer.exe, bez zmian w rejestrze):
  - AppBar na dole monitora głównego, ikony wyśrodkowane (styl Win11), akryl DWM w tle;
  - przyciski otwartych okien: klik = aktywuj / minimalizuj / przywróć, wskaźnik aktywnego okna, pomarańczowe miganie (okno prosi o uwagę);
  - PPM na oknie → „Zamknij okno”; PPM na pasku lub klik w Start → Menedżer zadań / Uruchom explorer.exe / Zamknij Exploder;
  - zegar z datą (odświeżanie raz na minutę), chowanie pod spód przy grach/filmach pełnoekranowych;
  - **awaryjne wyjście: Ctrl+Alt+Shift+Q** (zamyka Exploder i uruchamia explorer.exe, jeśli go nie ma).
  - **RAM:** Private 52,5 MB, Working Set 40,7 MB (explorer.exe: 127,8 MB). Większość to sterownik NVIDIA (GTX 1660 SUPER; `nvgpucomp64.dll`, `nvwgf2umx.dll`) ładowany przez D3D11 — koszt stały, nie rośnie z kolejnymi modułami. Do sprawdzenia później: render przez WARP (CPU) dla prostych elementów.
- 11.10.2026 — **pasek przerobiony na dock w stylu macOS** (na życzenie użytkownika; styl Win11 zostaje jako opcja `TaskbarStyle::Bar` w `Theme.h`):
  - pływający, wyśrodkowany, zaokrąglony, 6 px od dołu ekranu (po zmniejszeniu: wysokość 58, ikony 40, promień 15 DIP), szerokość rośnie/maleje z liczbą okien;
  - ikony 48 DIP pobierane w wysokiej rozdzielczości z pliku exe (fallback: ikona okna), kropki pod uruchomionymi, jaśniejsza pod aktywnym;
  - separator + zegar na prawym końcu docka; RAM bez zmian (Private 52,5 MB).
- 11.10.2026 — **zamiana ikon** (`src/taskbar/AppIcons.*`). Kolejność: własny PNG z `%LOCALAPPDATA%\Exploder\icons\<nazwa-exe>.png` (np. `brave.png`, `discord.png`) → ikona wbudowana rysowana wektorowo (konsola cmd: ciemne okienko z `>_` w stylu W11; PowerShell: niebieskie) → ikona z exe → ikona okna.
- 11.10.2026 — dock zmniejszony (wysokość 58, ikony 40). Użytkownik zaakceptował wygląd („leć dalej”).
- 11.10.2026 — **zasobnik (tray)** — `src/shell/TrayService.*`: ukryte okno `Shell_TrayWnd`, protokół `WM_COPYDATA` (NIM_ADD/MODIFY/DELETE/SETVERSION), rozesłanie `TaskbarCreated`, kliknięcia przekazywane do programów (wersje 0–4, NIN_SELECT, WM_CONTEXTMENU), sprzątanie ikon martwych programów. Ikony 16 DIP między separatorem a zegarem.
  - Działa **tylko gdy explorer.exe jest wyłączony** (wtedy Exploder jest powłoką). Wtedy AppBar też nie działa (obsługuje go explorer), więc obszar roboczy ustawiamy sami (`SPI_SETWORKAREA`) i przywracamy przy wyjściu.
  - Test z wyłączonym explorerem: 5 ikon programów pojawiło się w docku, obszar roboczy 1920×1010, RAM Private 59 MB.
  - Do sprawdzenia przez użytkownika: czy menu programów (np. Discord) otwierają się po kliknięciu ikony w zasobniku.
  - Brak: systemowe ikony głośności/sieci/baterii (rysuje je explorer — trzeba napisać własne), dymki z podpowiedzią (tooltip), ukrywanie nadmiaru ikon.
- 11.10.2026 — **tray schowany pod strzałką** (na życzenie): w docku przycisk „˄ Tray”; klik → panel `TrayFlyout` (akryl, zaokrąglony) z nagłówkiem „Tray” i listą ikon z opisami (pierwsza linia podpowiedzi programu, brak → „(bez opisu)”). Klik w wiersz = klik w ikonę zasobnika; panel zamyka się po kliknięciu, Esc lub kliknięciu obok.
- 11.10.2026 — **pulpit v1** (`src/desktop/`): tapeta z ustawień Windows („wypełnij”), okno na dole Z-order zarejestrowane przez `SetShellWindow` (dzięki temu explorer.exe uruchomiony do otwarcia folderu nie przejmuje powłoki). Ikony plików, przeciąganie, menu kontekstowe Windows, F2/Delete itd. zostały napisane, ale **na życzenie użytkownika wyłączone** (`kShowIcons = false` w `Desktop.cpp`) — pulpit ma być samą tapetą.
  - Rysowanie przez dwie warstwy DirectComposition: tapeta (rysowana raz) + warstwa wirtualna (pamięć tylko tam, gdzie coś jest).
  - Gdy proces Exploder się kończy (np. zabity), Windows sam uruchamia explorer.exe — zabezpieczenie na wypadek awarii.
- 11.10.2026 — **nowy układ wg obrazka użytkownika** (pulpit bez ikon, pasek u góry, panel aplikacji na środku):
  - **Górny pasek** (`src/topbar/TopBar.*`): pływający, akryl, 860×36 DIP. Lewo: sieć (Wi-Fi z siłą sygnału + nazwa / Ethernet / brak internetu — `SystemStatus.*`, odświeżane co 5 s). Środek: „teraz odtwarzane” z Windows (Spotify, YouTube w przeglądarce…) — okładka, tytuł, wykonawca, ⏮ ⏯ ⏭, pasek postępu z czasem (`MediaService.*`, C++/WinRT `GlobalSystemMediaTransportControls`). Prawo: głośność (klik = wycisz, kółko = ±2%).
  - **Panel aplikacji** na środku pulpitu (`src/desktop/AppLauncher.*`): szkło (rozmyta tapeta liczona raz), wyszukiwarka (pisanie na pulpicie od razu szuka, Enter uruchamia pierwszy wynik, Esc czyści), siatka wszystkich aplikacji z folderu Aplikacje (też UWP), przewijanie kółkiem.
  - **Start w docku** = pokaż pulpit (minimalizuje okna, drugi klik przywraca). Menu powłoki pod PPM na docku.
  - Bez explorera paski same chowają się pod gry/filmy na pełnym ekranie; obszar roboczy liczy `src/core/WorkArea.*` (góra + dół).
  - Poprawka: bez explorera Windows pokazywał zminimalizowane okna jako małe paski w rogu pulpitu (styl Win 3.1). Teraz w trybie powłoki ustawiamy `ARW_HIDE` (jak explorer, przywracane przy wyjściu) i przesuwamy już zminimalizowane okna na −32000 przez `SetWindowPos` (uwaga: `SetWindowPlacement` z `WPF_SETMINPOSITION` i `ArrangeIconicWindows` NIE działają — sprawdzone).
- 11.10.2026 — poprawki docka:
  - **ikony z exe** pobierane przez `PrivateExtractIconsW` w dokładnym rozmiarze (wcześniej `IShellItemImageFactory` dawał za mały, przycięty obraz — koło Discorda miało ścięte boki). Sprawdzone na zrzucie: okrągła i gładka.
  - **dymek nad ikoną** (`src/core/Tooltip.*`, styl macOS): nazwa programu (opis z exe, np. „Discord”, „Brave Browser”) + tytuł okna; nad Start „Start”, nad zegarem pełna data.
  - **Win+Shift+S** (`src/shell/ScreenSnip.*`): własne wycinanie (systemowe bez explorera nie działa) — zamrożony przyciemniony ekran, zaznaczenie myszą → schowek; Esc / PPM anuluje. Działa tylko gdy explorer jest wyłączony (inaczej skrót należy do niego).
- 11.10.2026 — **panel na środku = tylko przypięte** (na życzenie: bez listy wszystkich aplikacji). Domyślnie Kosz + Ten komputer + kafelek „Dodaj” (okno wyboru: exe/skrót/plik/folder). Wyszukiwarka nadal przeszukuje wszystkie aplikacje (lista ładowana dopiero przy pierwszym szukaniu); PPM na wyniku → „Przypnij do panelu”, PPM na przypiętym → „Usuń z panelu”. Zapis: `%LOCALAPPDATA%\Exploder\launcher.txt`. Panel dopasowuje wysokość do liczby kafelków, przy szukaniu rośnie w dół. RAM spadł do ~78 MB (bez listy wszystkich aplikacji na starcie).
- 11.10.2026 — wycinanie i pełny ekran:
  - **Win+Shift+S**: na górze pasek [Wycinek] [Cały ekran] [✕] (Cały ekran = od razu cały zrzut do schowka). Naprawione: górny pasek nie był przyciemniony — wypychał się nad ekran wycinania, bo przy każdej zmianie aktywnego okna robił `SetWindowPos(HWND_TOPMOST)`.
  - **Pełny ekran** (gra, film w przeglądarce): górny pasek i dock znikają (`SW_HIDE`), po wyjściu wracają. Sprawdzane co 0,5 s (film w przeglądarce nie zmienia aktywnego okna). Warunek: okno zakrywa cały monitor (bez sprawdzania stylu — przeglądarki zostawiają WS_CAPTION). Przetestowane oknem testowym na cały ekran: oba znikają i wracają.
- 11.10.2026 — duża paczka zmian:
  - **Spotify / „teraz odtwarzane”**: serwer tych danych (GSMTC) działa w explorer.exe — bez niego „klasa niezarejestrowana”. Tymczasowo tryb zastępczy (tytuł okna Spotify / karty YouTube lub Spotify w przeglądarce, ikona programu zamiast okładki). **Decyzja użytkownika: Spotify docelowo przez Spotify Web API, klucz w ustawieniach — w osobnym poleceniu.**
  - **Ikony w panelu** ostrzejsze (obraz 2× większy, zmniejszany przy rysowaniu).
  - **Górny pasek**: PPM na głośności → suwak (`StatusPopups.*`, nazwa urządzenia, wyciszenie, kółko); klik w sieć → menu: bieżące połączenie (IP, prędkość), lista Wi-Fi (jeśli jest karta) z łączeniem, pole hasła dla nowych sieci, „Połączenia sieciowe…” (ncpa.cpl). Wspólna baza okienek: `src/core/Popup.*`.
  - **Exploder — nowy menedżer plików** (`src/files/`): `FileView` (siatka ikon, historia, zaznaczanie, nowy folder, zmiana nazwy w miejscu, Delete, Ctrl+C/X/V, menu kontekstowe Windows, obserwacja folderu) + `FileWindow` (okno z **kropkami macOS** zamknij/minimalizuj/maksymalizuj, pasek boczny: ulubione, dyski, Kosz; wstecz/dalej/w górę, boczne przyciski myszy). Foldery z panelu na środku (Ten komputer, Kosz…) otwierają się w Explodzie zamiast explorer.exe. Kropki macOS w cudzych programach — **niezrobione** (wymagałoby nakładek na ich okna; do decyzji).
  - **Panel „Pliki” po lewej** (`desktop/FilesWidget.*`): mini-Exploder na pulpicie (domyślnie folder Pulpit), przeciąganie za pasek, zmiana rozmiaru za krawędzie, czerwona = zamknij, żółta/dwuklik w pasek = zwiń, zielona = duże okno. Otwierany ponownie kafelkiem **„Pliki”** w panelu na środku. Stan w `files-widget.txt`.
  - **Statystyki po prawej** (`desktop/StatsWidget.*`, `shell/SystemStats.*`): CPU, RAM, dysk (aktywność + wolne na C:), sieć ↓/↑, czas działania, „Dziś w aplikacjach” (top 5; bezczynność > 3 min nie liczy się; zapis `usage\RRRR-MM-DD.txt`). Odświeżanie co 2 s.
  - **Alt+Tab** (`shell/AltTab.*`): szklany panel z podglądami okien na żywo (miniatury DWM), hak klawiatury; krótkie Alt+Tab przełącza bez pokazywania panelu. Tylko w trybie powłoki.
  - RAM po zmianach: Private ~79 MB.
- 11.10.2026 — **awaria (0xC0000005 w `FileView::Draw`) naprawiona**: wczytywanie ikon/miniatur (wywołania powłoki) przetwarza po drodze inne komunikaty — np. odświeżenie folderu podmieniało listę plików w trakcie pętli rysowania. Teraz ikony wczytuje `PrepareIcons()` przed rysowaniem (FileView, panel aplikacji, dock, Alt+Tab, pasek boczny), a każde `Render()` ma blokadę ponownego wejścia. **Zasada: w trakcie rysowania nie wołać powłoki/COM.**
- 11.10.2026 — **statystyki dysku naprawione**: liczniki wydajności Windows u użytkownika są uszkodzone (`\PhysicalDisk` nie istnieje), więc PDH usunięte; aktywność, odczyt i zapis liczone z `IOCTL_DISK_PERFORMANCE` na `\\.\PhysicalDriveN` (bez administratora, sprawdzone na 3 dyskach). RAM ~75 MB.
- 12.10.2026 — **kropki macOS w cudzych oknach** (`src/shell/WindowDots.*`): nakładka nad przyciskami **aktywnego** okna (tło = próbka koloru paska tytułu, klik → `WM_SYSCOMMAND` zamknij/minimalizuj/maksymalizuj, symbole po najechaniu). Położenie: `DWMWA_CAPTION_BUTTON_BOUNDS` dla zwykłych okien; Brave/Chrome/Edge (3×45×29 DIP) i Discord (3×36×32 DIP, zmierzone przez `PrintWindow`; Brave potwierdzony na zrzucie użytkownika) — wymiary stałe; inne okna Chromium/Electron pomijane. Na czas przeciągania znika. Wyłącznik: PPM na docku → „Kropki macOS w oknach” (zapis w `settings.txt`). Sprawdzone na konsoli (cmd) — kropki dokładnie w miejscu przycisków.
  - Naprawione: tło nakładki na Discordzie było czerwone — kolor próbkowano w punkcie zasłoniętym jeszcze przez starą nakładkę (z Brave). Teraz kolor = najczęstszy z 7 pikseli na lewo od przycisków (co 3 px, od `left-6`; pomija kreskę Discorda i przezroczysty górny skraj okna, który dawał brązowy odcień), a gdy próbki leżą pod naszą nakładką — ukrycie + `DwmFlush()` przed `GetPixel`.
  - Naprawione: w Brave/Edge kropki znikały, gdy aktywny był dymek/popup przeglądarki (okno `WS_EX_TOOLWINDOW`) — teraz nakładka zostaje na oknie-właścicielu (`GA_ROOTOWNER`). Dodatkowo `EVENT_OBJECT_SHOW` odświeża nakładkę, gdy okno stało się aktywne przed pokazaniem. Sprawdzone prawdziwym kliknięciem na tymczasowym, zmaksymalizowanym Edge (skrypt `clk4.ps1` w scratchpadzie sesji).
  - Kolor tła próbkowany ponownie co 500 ms przez ~2 s po każdej zmianie (`kResampleTimer`) — Discord tuż po aktywacji ma jeszcze brązowy pasek „nieaktywnego” okna i przemalowuje go z opóźnieniem.
  - **Naprawione: Alt+Tab nie przełączał okien** (szczególnie Discord ↔ Brave) — `SetForegroundWindow` wołane wewnątrz haka klawiatury dawało zdarzenie aktywacji, ale na wierzchu zostawało stare okno (kropki przeskakiwały na wymiary drugiego okna). Teraz przełączenie po wyjściu z haka (`WM_APP + 4`) z `AttachThreadInput` do wątku aktywnego okna + `BringWindowToTop`. Sprawdzone testem: hak tymczasowo przyjmował wstrzyknięte klawisze ze znacznikiem (`dwExtraInfo`), po teście zmiana cofnięta.
  - Nowe: `src/core/Settings.*` — ustawienia `%LOCALAPPDATA%\Exploder\settings.txt` (klucz=wartość); tu trafi też klucz API Spotify.
  - **Naprawione: Alt+Tab w ogóle się nie tworzył** — w `WndProc` uchwyt `hwnd_` ustawiany dopiero po utworzeniu, a `HandleMessage` wołał `DefWindowProc(hwnd_ = null)` już dla `WM_NCCREATE` → Windows anulował tworzenie okna. **Zasada: w `WM_NCCREATE` od razu `self->hwnd_ = hwnd`.**
  - Diagnostyka: build z PDB i mapą (`exploder.pdb`, `exploder.map`), przy awarii zapis `%LOCALAPPDATA%\Exploder\crash.dmp`. Adres z dziennika zdarzeń (Aplikacja, ID 1000, „Przesunięcie błędu”) → funkcja z `exploder.map`.
  - RAM: Private ~91 MB (był 56 bez pulpitu) — do optymalizacji.
  - Zasada użytkownika: kolejny moduł dopiero po akceptacji poprzedniego.
  - Jeszcze brak: tray (zasobnik), przypinanie aplikacji, podgląd okien/tooltipy z tytułem, motywy z JSON, wiele monitorów.

- 12.10.2026 (wieczór) — duża runda zmian:
  - **Kropki na wszystkich oknach**, nie tylko aktywnym: osobna nakładka na każde widoczne okno, NIE „zawsze na wierzchu” — leży w kolejności okien tuż nad swoim oknem (`InsertAfterFor`: `GW_HWNDPREV`; za oknem „na wierzchu” → `HWND_TOP`), po zmianie aktywnego okna `Restack()`. Hak `EVENT_OBJECT_LOCATIONCHANGE` globalny (filtr: tylko śledzone okna). Kolor tła próbkowany tylko, gdy w punkcie próbki jest to samo okno (`WindowFromPoint`). Kropki wyrównane **do prawej** (w miejscu „zamknij”), w przeglądarkach większe (skala 1,3). Pomijane okna aplikacji Explodera (klasa `Exploder*`) — mają własne kropki (wcześniej kalkulator miał dwa komplety).
  - `TrafficLights` przeniesione do `src/core/TrafficLights.*` (parametr skali).
  - **Alt+Tab: podglądy też dla zminimalizowanych okien** — DWM trzyma ich ostatni obraz w pełnym rozmiarze; ikona tylko, gdy źródło ma rozmiar paska (≤ 240×60).
  - **Statystyki dysku**: pasek = zajętość C: w %, pod spodem aktywność z dokładnością 0,1% (bezczynny dysk ma ~0,2% — wcześniej wyglądało jak „brak %”) + odczyt/zapis.
  - **„+” w panelu aplikacji** otwiera własne okienko „Dodaj do panelu” (`src/desktop/AppPicker.*`: lista wszystkich aplikacji z ikonami, wyszukiwanie, kilka naraz) zamiast systemowego okna wyboru pliku; „Inny plik lub program…” → okno Explodera w trybie wyboru (`OpenFilePicker`, `FileView::pickFile` — dwuklik na pliku przypina).
  - `Popup` otwiera się nad przyciskiem, gdy pod nim brak miejsca (menu przy docku).
  - **Przycisk Windows w docku → menu** (`src/taskbar/StartMenu.*`): Terminal, Kalkulator, Sklep, Pliki, Ustawienia, Menedżer zadań, Pokaż pulpit (dawne działanie przycisku), Uruchom… (`RunFileDlg`, shell32 #61), zasilanie: Zablokuj, Uśpij (`SetSuspendState`, powrprof), Restart, Wyłącz (`ExitWindowsEx` + `SE_SHUTDOWN_NAME`), Wyloguj.
  - **Nowe aplikacje** (osobne procesy obok `exploder.exe`, wspólna baza `src/core/AppWindow.*`: okno bez ramki, kropki, przeciąganie, zmiana rozmiaru, cień DWM):
    - `exploder-terminal.exe` — **Terminal w stylu Linuksa** (`apps/terminal/`): cmd.exe w ConPTY, parser VT (kursor, kasowanie, wstaw/usuń, region przewijania, SGR 16/256/24-bit, ekran alternatywny, odpowiedź na DSR), historia 5000 wierszy, zaznaczanie myszą, Ctrl+C/Ctrl+Shift+C/Ctrl+V/PPM, Ctrl+kółko = czcionka (zapis `terminalFontSize`). Bakłażanowe tło Ubuntu, paleta Tango, prompt `user@host:ścieżka$`, aliasy doskey (`ls`, `ll`, `cat`, `grep`, `clear`, `pwd`, `rm`, `cp`, `mv`, `touch`, `which`, `ps`, `kill`…) w `%LOCALAPPDATA%\Exploder\terminal-aliasy.txt`. Tytuł „Terminal” zamiast ścieżki cmd.exe. Sprawdzone: `ver`, `pwd`, kolory promptu.
    - `exploder-calc.exe` — **Kalkulator, własny styl** (użytkownik odrzucił wygląd iOS i Windows 11 oraz przezroczystość): jednolite tło #121318, karta wyniku z 3 ostatnimi obliczeniami, krój Bahnschrift, zaokrąglone przyciski (działania fioletowe, „=” gradient fiolet→niebieski, C czerwone), małe przyciski √x x² 1/x CE. Klawiatura, schowek, powtarzanie „=”, % jak w Windows. Sprawdzone: 12+5=17, 1234×5+6=6176.
    - `exploder-store.exe` — **Sklep** (`apps/store/`): 45 programów w 7 kategoriach, instalacja jednym kliknięciem przez `winget install --silent` (kolejka, postęp z „NN%” w wyjściu, błędy z kodem, „już zainstalowane” = sukces). Wykrywanie zainstalowanych: `winget export` + `winget list` (identyfikator albo nazwa na początku wiersza — Brave/Discord bez dopasowania do źródła mają tylko `ARP\...`), drugi identyfikator Discorda ze sklepu MS. Ikonki: wbudowane w exe (patrz poprawki niżej), czarne tło. Identyfikatory zweryfikowane `winget show` (FileZilla usunięta — brak w winget).
    - Ikony aplikacji: `tools/make_icons.py` (Pillow) → `res/*.ico`, zasoby `apps/*/*.rc` (ikona + opis = nazwa w docku). RAM: ~52 MB na aplikację, tylko gdy otwarta.
  - Poprawki po uwagach:
    - **Klawisz Windows** otwiera/zamyka menu Start (hak `WH_KEYBOARD_LL` tylko w trybie powłoki; tylko samo Win — Win+Shift+S itp. nie otwierają; hak nie zjada klawiszy). `Popup` aktywuje się przez `AttachThreadInput`, gdy Windows blokuje zmianę pierwszego planu. Sprawdzone: Win → menu widoczne i aktywne, drugie Win → zamknięte, Win+Shift → bez menu.
    - **Terminal nie jest już różowy**: tło #121318 (jak Kalkulator), nieprzezroczyste; ikona w tych barwach.
    - **Kalkulator**: symbole działań (÷ × − + = ±) rysowane liniami (czcionka dawała dziwny „+”); odświeżony wygląd — gradient tła, karta wyniku z fioletową poświatą i akcentem fiolet→niebieski, przyciski z głębią (gradient + jaśniejsza krawędź), funkcje jako obrysowane pigułki.
    - **Ikonki Sklepu wbudowane w exe**: `tools/fetch_store_icons.py` raz pobiera najlepsze źródła (dashboard-icons, Wikimedia Commons, GitHub projektów, icon.horse), bierze największą klatkę, usuwa białe tło, odwraca ciemne jednokolorowe logo, przycina logo z napisem do znaku (Battle.net, Ubisoft), robi kwadrat 128 px → `res/store/*.png` + `apps/store/store_icons.rc` (zasoby RCDATA `I_<ID>`; bez cudzysłowów — rc zostawia je w nazwie). Sklep nie pobiera nic przy starcie. MSI Afterburner bez ikonki (kafelek z inicjałami).
    - **Kalkulator — kolejna zmiana wyglądu** (użytkownik: fiolet z poświatą „wygląda jak AI”, ± w lewym dolnym rogu dziwny): styl klasycznego urządzenia — grafitowy korpus #26231F, matowy wyświetlacz jak LCD (szałwia #A9B293, ciemne cyfry, ramka), kremowe cyfry, bursztynowe działania i „=” (#F2A541), płaskie klawisze z dolną krawędzią (wciśnięty „opada”), bez gradientów i poświat. Szerokie „0”; górny rząd małych klawiszy: ± √x x² 1/x; CE tylko klawiszem Delete. Ikona w tych barwach.
    - **Kalkulator — wygląd wybrany przez użytkownika: spójny z Veyrą** (styl retro uznał za „stary”; wybór z 3 podglądów): grafit #1C1C21, panel wyniku jak panele pulpitu (białe 5% + obwódka 12%), płaskie klawisze z białymi nakładkami, akcent #61B3FF (jak dock i menu) na działaniach i „=”, Segoe UI. Ikona w tych barwach.
  - **Aplikacja Ustawienia** (`apps/settings/SettingsApp.cpp`, `exploder-settings.exe`, styl Veyry; menu Start → Ustawienia): Personalizacja (podgląd i siatka tapet: Veyra, `C:\Windows\Web\Wallpaper`, folder Obrazy; miniatury w wątku WIC; klik → `SPI_SETDESKWALLPAPER`, pulpit przeładowuje), Pulpit i okna (kropki macOS), Aplikacje (rozmiar czcionki terminala, otwieranie aplikacji Veyry), System (Veyra zamiast Eksploratora po zalogowaniu — `HKCU\...\Winlogon\Shell`, włączenie wymaga drugiego kliknięcia; skróty do ustawień Windows `ms-settings:`), Informacje (RAM powłoki, skróty, GitHub). Zmiany na żywo: zapis `settings.txt` + rozgłoszenie `VeyraSettingsChanged` (`Settings::ChangedMessage`), powłoka i terminal robią `Settings::Reload()`. Sprawdzone: przełącznik kropek → nakładki 2 → 0 → 2.
  - **Tapety Veyry** (`tools/make_wallpaper.py` → `res/wallpapers/`, 2560×1440, deterministyczne), do wyboru w Ustawieniach:
    - `veyra-domyslna.jpg` — **domyślna**: górskie jezioro o zmierzchu (błękit → turkusowa poświata nad górami, obłoki z szumu 2D, góry w planach, las na brzegu, lustrzane odbicie z falowaniem), przyciemnione (`^1.25 × 0.78`) — użytkownik: `^1.2×0.8` „to samo”, `^1.6×0.62` „nie aż tak ciemna”,
    - `veyra-jezioro.jpg` — to samo jezioro w jaśniejszej wersji,
    - `veyra-gory.jpg` — nocny krajobraz: zorza, gwiazdy, góry o ostrych graniach (przesunięcie punktu środkowego), las świerków,
    - `veyra-zorza.jpg` — czarne tło z niebieską i zieloną smugą (pierwsza wersja).
    Kopiowane przy budowaniu do `build\wallpapers`; domyślna ustawiana przy pierwszym starcie powłoki (flaga `defaultWallpaperApplied` — potem nie nadpisuje wyboru użytkownika).
  - Testy bez przeszkadzania: zrzuty okien aplikacji przez `PrintWindow` (nie zrzut ekranu — okno bywa pod przeglądarką użytkownika).
  - Kod wysłany do repozytorium GitHub: https://github.com/Promileczek/Veyra (README należy do użytkownika — bez zmian).

## Budowanie i uruchamianie

- `build.bat` → wynik w `%LOCALAPPDATA%\Exploder\build\exploder.exe` (dysk lokalny — X: to udział sieciowy, niedostępny przy starcie powłoki).
- `build.bat` buduje też `exploder-terminal.exe`, `exploder-calc.exe`, `exploder-store.exe` (menu przycisku Windows uruchamia je z folderu `exploder.exe`).
- Struktura: `src/core` (grafika D3D11/D2D/DComp, akryl, `AppWindow`, `TrafficLights`, `Popup`, `Settings`), `src/shell` (okna, tray, Alt+Tab, kropki, statystyki), `src/taskbar` (dock, menu Start), `src/desktop` (pulpit, panele), `src/files` (okno plików), `src/topbar`, `apps/` (Terminal, Kalkulator, Sklep), `res/` (ikony), `tools/` (generator ikon).
- GitHub: https://github.com/Promileczek/Veyra (gałąź `main`).
