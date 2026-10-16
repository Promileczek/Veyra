# Veyra 1.2 — plan

Stan wyjściowy: **1.1.5.2** (13.10.2026).

## Błędy

- [x] **Po aktualizacji z Ustawień wraca stary Eksplorator Windows** (zrobione 13.10.2026 w kodzie, zbudowane, niezainstalowane — do sprawdzenia na żywo przy następnej aktualizacji; Full jako powłoka przy starcie i co 2 s przez minutę zamyka Eksplorator, który wszedł na miejsce powłoki: `ExplorerUi::IsConfiguredShell` / `CloseExplorerShell`) (pasek zadań Windows obok Veyry) — zauważone przy aktualizacji 1.1.5 → 1.1.5.2 z prośbą UAC. Po aktualizacji **nie może** wrócić ani pasek / pulpit Eksploratora, ani sam proces explorer.exe (w Full) — tylko Veyra.
- [ ] **Okno UAC przy aktualizacji** (systemowe „Czy chcesz zezwolić… Windows PowerShell”) — ma być w stylu Veyry.
  Uwaga: samego okna UAC nie da się podmienić. Rysuje je systemowy consent.exe na osobnym, zabezpieczonym pulpicie, do którego programy nie mają dostępu; Windows nie ma na to żadnego interfejsu, a podmiana pliku systemowego psułaby się po aktualizacjach i osłabiała zabezpieczenia. Do wyboru:
  - aktualizacja bez UAC: instalator tworzy zadanie Harmonogramu z najwyższymi uprawnieniami, które tylko podmienia pliki Veyry, albo instalacja w folderze użytkownika zamiast Program Files;
  - albo przed UAC własne okienko Veyry z wyjaśnieniem („Veyra potrzebuje zgody, żeby podmienić pliki”).
- [x] **UAC tylko raz — przy instalacji** (zrobione 13.10.2026 w kodzie, zbudowane, niezainstalowane — do sprawdzenia na żywo po instalacji nowego VeyraSetup: prośba „sprawdz” przez `schtasks /run` jako zwykły użytkownik; szczegóły w projekt.md). Instalator (który i tak pyta o uprawnienia administratora) tworzy zadanie Harmonogramu zadań z najwyższymi uprawnieniami (np. „Veyra Update”). Później aktualizacje i inne akcje wymagające administratora (podmiana plików w Program Files, wpisy w rejestrze HKLM — IFEO calc.exe / taskmgr.exe, wpis deinstalacji) Veyra wykonuje przez to zadanie — **bez okna UAC**. Zadanie przyjmuje tylko z góry określone akcje (nie dowolne polecenia), deinstalacja je usuwa.
- [x] **Terminal: nie dało się przewijać w górę i w dół w Claude Code** (zgłoszone i naprawione 13.10.2026, sprawdzone na osobnej instancji Terminala z buildu; niezainstalowane).
- [x] **Spotify nie ma kropek macOS** (i inne aplikacje Electron, np. VS Code) — dziś kropki są tylko w zwykłych oknach, Brave/Chrome/Edge i Discordzie. *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [x] **Okienko „Uruchom” (Win+R)** nie da się przesunąć, zminimalizować ani zamknąć jak zwykłego okna. *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [x] **Czasem aplikacje się bugują i nie widać kropek macOS** (kropki znikają z okna, które powinno je mieć). *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [x] Puste czarne okienka z samymi kropkami macOS w rogach ekranu (nakładki WindowDots dla niewidocznych okien). *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [x] Statystyki „Dziś w aplikacjach” pokazują „Application Frame Host” zamiast nazwy aplikacji ze Sklepu (użyć AUMID jak w docku). *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [ ] Kropki macOS: kolor tła nie zawsze trafia (nieaktywne okna, Discord po przełączeniu).
- [x] Alt+Tab: ciemny podgląd zminimalizowanych przeglądarek. *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [ ] Szkło docka pokazuje rozmytą tapetę zamiast okien za dockiem. *(nie zrobione: prawdziwe rozmycie okien wymaga akrylu DWM wypełniającego cały prostokąt okna — ten sam problem co otoczka górnego paska; do próby z użytkownikiem)*
- [ ] Minimal: pasek zadań Windows miga po zalogowaniu, zanim Veyra go schowa. *(nie zrobione: Windows opóźnia programy z HKCU Run o kilka sekund; możliwe rozwiązanie — start Veyry Minimal z zadania Harmonogramu „przy logowaniu” zamiast Run; czeka na decyzję użytkownika)*
- [x] Minimal: po zabiciu Veyry (Menedżer zadań) pasek Windows zostaje schowany. *(zrobione 13.10.2026 w buildzie, niezainstalowane; niesprawdzone na żywo — wymaga instalacji Minimal)*
- [x] Sklep: postęp instalacji nie zawsze w procentach. *(zrobione 13.10.2026 w buildzie, niezainstalowane; niesprawdzone na żywo)*
- [x] **Obwódki ikon**: niektóre aplikacje w panelu aplikacji mają białą/ciemną obwódkę wokół ikony (np. Zdjęcia iCloud), Discord ma dziwną otoczkę. *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [x] **Obwódka górnego paska** — dziwna jasna otoczka wokół paska; to samo przy podświetleniu ikony w docku po najechaniu. *(górny pasek zrobiony 13.10.2026 w buildzie: bez akrylu, tło 0.95 jak dock; podświetlenie ikony w docku niepotwierdzone; ten sam wzorzec akryl+region mają Tooltip, Popup, TrayFlyout, Notification, AltTab — do sprawdzenia)*
- [x] **Tray**: nie wszystkie ikony mają nazwy („(bez opisu)” — np. Discord, BlueStacks); brać nazwę programu (opis exe / okno), gdy podpowiedź jest pusta. *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [x] **Ustawienia**: przy przewijaniu treść (karty) nachodzi na kropki okna w prawym górnym rogu — zostawić pas na górze albo przycinać treść pod paskiem tytułu. *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [x] **Przypięte aplikacje znikały z panelu aplikacji po restarcie** (np. Brave ze Start/AppsFolder: klucz w launcher.txt był samą nazwą, nieodtwarzalną). *(zrobione 15.10.2026 w buildzie, `AppLauncher.cpp`: klucz `shell:AppsFolder\<AUMID>` sprawdzany pod kątem odtwarzalności, stare wpisy odzyskiwane; sprawdzone na żywo; niezainstalowane)*
- [x] **Kalkulator** da się powiększyć na pełny ekran — ma mieć jeden, stały rozmiar (bez maksymalizacji i F11). *(zrobione 13.10.2026 w buildzie, niezainstalowane)*
- [x] **Panel „Pulpit”**: siatka folderów ma większy odstęp z lewej niż z prawej — wyrównać (ten sam margines po obu stronach). *(zrobione 13.10.2026 w buildzie, niezainstalowane)*

## Do zrobienia

### Animacje
- [x] Powiadomienia: animacja pojawiania się wszędzie, gdzie jej brakuje (powiadomienie o aktualizacji już wjeżdża z prawej). *(zrobione 14.10.2026 w buildzie, niezainstalowane: `Surface::Appear` w Popup i TrayFlyout; samo Notification bez zmian — już wjeżdżało z prawej)*
- [x] Otwieranie menu Start. *(zrobione 14.10.2026 w buildzie, sprawdzone na żywo)*
- [x] Otwieranie okienka głośności i okienka sieci (internet) w górnym pasku. *(zrobione 14.10.2026 w buildzie; także Win+V, „Uruchom”, tray)*
- [x] Inna, własna animacja minimalizowania i przywracania aplikacji. *(zrobione 15.10.2026 w buildzie, sprawdzone na żywo (zwykłe okno i Edge), niezainstalowane; przywracanie animowane tylko z docka — Alt+Tab i przywracanie przez sam program bez animacji)*

### Bateria (laptop)
- [x] Gdy Veyra wykryje laptop (bateria) — w górnym pasku obok głośności stan baterii (procent, ładowanie). *(zrobione 14.10.2026 w buildzie, sprawdzone na żywo przez `batteryTest` w settings.txt; niezainstalowane)*
- [x] Powiadomienia: 15%, 10% i 5% baterii oraz „naładowana w pełni”. *(zrobione 14.10.2026 w buildzie; sprawdzone 9% przez `batteryTest`)*

### Menedżer zadań Veyry
- [x] Podmiana przez „Image File Execution Options” dla `taskmgr.exe` (Debugger), jak Process Explorer i jak Veyra podmienia `calc.exe`. *(zrobione 14.10.2026 w kodzie, zbudowane; podmiana IFEO niesprawdzona na żywo — wymaga instalacji/uprawnień)*
- [x] Własny Menedżer zadań w stylu Veyry z zakładkami: **Procesy**, **Wydajność**, **Uruchamianie**. *(zrobione 14.10.2026 w buildzie, `apps/taskmgr`, tylko Full; wszystkie 3 zakładki sprawdzone na żywo; niezainstalowane)*
- [x] Przełącznik podmiany w Ustawieniach → System (jak przy Kalkulatorze), wyłączenie przywraca systemowy. *(zrobione 14.10.2026 w kodzie; zadanie Harmonogramu `menedzer-wlacz/-wylacz`; niesprawdzone na żywo)*

### Ekran Ctrl+Alt+Del w stylu Veyry
- [ ] Cel: te same opcje co w Windows (Zablokuj, Przełącz użytkownika, Wyloguj, Zmień hasło, Menedżer zadań, zasilanie) w stylu Veyry.
- [ ] Uwaga: ekranu Ctrl+Alt+Del Windows nie pozwala podmienić ani przechwycić — obsługuje go Winlogon na zabezpieczonym pulpicie (jak UAC). Da się tylko ukrywać jego pozycje zasadami systemu. Realnie:
  - „Menedżer zadań” z ekranu Ctrl+Alt+Del i tak otworzy Menedżer zadań Veyry (po podmianie taskmgr.exe);
  - własny ekran opcji Veyry z tymi samymi akcjami pod innym skrótem (np. Ctrl+Shift+Del albo z menu Start → zasilanie) — skrót do ustalenia.

### Ustawienia
- [x] Kolejność zakładek: **Aktualizacje** na samej górze, pod nimi **Informacje**, pod nimi **System**. *(zrobione 15.10.2026 w buildzie, sprawdzone na żywo; niezainstalowane)*
- [x] **Pulpit i okna** przenieść do **Personalizacji**. *(zrobione 15.10.2026 w buildzie; zakładki Desktop już nie ma)*
- [x] **Rozmiar czcionki** (Terminal) przenieść do **Personalizacji**. *(zrobione 15.10.2026 w buildzie)*
- [ ] **Aplikacje**: podkategorie w miejscu listy zakładek, ze strzałką „wstecz”:
  - **Aplikacje Veyry**,
  - **Zainstalowane programy**.
- [x] **Informacje → Komputer**: mniej szczegółów — bez liczby rdzeni procesora i bez informacji o dyskach. *(zrobione 15.10.2026 w buildzie)*
- [x] Nowa zakładka **Konta** z informacją „W budowie” *(zrobione 15.10.2026 w buildzie, sprawdzone na żywo)*: tu będzie zakładanie kont i logowanie do konta Veyra (zapisywanie ustawień, tapet itp. w chmurze).
- [x] Ustawienia → Personalizacja (lub Pulpit): **wyłączenie panelu statystyk** — wtedy panel „Pliki” (foldery pulpitu) idzie pod panel aplikacji. *(zrobione 15.10.2026 w buildzie, sprawdzone na żywo; niezainstalowane. Różnica względem planu: panel „Pliki” NIE przesuwa się pod panel aplikacji — zostaje na swoim miejscu (panele są ruchome); do ewentualnej zmiany)*
- [ ] Ustawienia → **Sklep**: wybór, co pokazuje Sklep (aplikacje Veyry, programy z internetu / winget, kategorie itp.).
- [ ] Ustawienia → **Dźwięk**: wybór urządzenia, z którego leci dźwięk (głośniki, słuchawki…). Po podłączeniu nowego urządzenia audio — powiadomienie Veyry (tam, gdzie Windows pokazałby swoje).
- [x] Ustawienia → **Mysz**: nowy kursor w stylu Veyry; „potrząśnij, aby znaleźć” — gdy mysz szybko porusza się w jednym miejscu, kursor na chwilę się powiększa (jak w macOS); włączanie i wielkość w Ustawieniach. *(zrobione 16.10.2026 w buildzie; Direct2D z cieniem, obrysem i akcentem #61B3FF, autodetekcja potrząsania, zakładka Mysz w Ustawieniach, bezpieczne przywracanie kursora systemowego)*

### Dock, menu Start, ikony
- [x] **Ikona Veyry** (logo) i **własna ikona folderu** w stylu Veyry. *(zrobione 15.10.2026 w buildzie, sprawdzone na żywo; logo = pełne „V” w akcencie, folder = płaski niebieski rysowany w D2D (`FolderIcon.h`) w panelu „Pliki” i na pulpicie; wygenerowane `res/veyra.*` i `res/folder.*` na razie nieużywane. Niezrobione: kafelek „Pliki” w panelu aplikacji ma nadal systemową żółtą ikonę)*
- [x] Przycisk Windows w docku (cztery kwadraty) → **ikona Veyry**. *(zrobione 15.10.2026 w buildzie, `DrawVeyraLogo` w Taskbar.cpp)*
- [x] Menu Start, nagłówek: usunąć podpis „Exploder”, a zamiast niebieskiej ikony pokazać **zdjęcie / ikonę konta użytkownika z Windows**. *(zrobione 14.10.2026 w buildzie, sprawdzone na żywo; bez zdjęcia zostaje logo)*
- [x] **Grupowanie okien w docku**: kilka okien tego samego programu (np. 2× Brave) = jedna ikona z oznaczeniem liczby okien (kilka kropek pod ikoną / licznik); kliknięcie przełącza między nimi albo pokazuje listę z podglądami. *(zrobione 15.10.2026 w buildzie, sprawdzone na żywo (3 Terminale = 1 ikona, 3 kropki); niezainstalowane. Klik cykluje między oknami; PPM: zamknij aktywne / wszystkie. Podglądy okien (lista z miniaturami) NIE zrobione — możliwe rozszerzenie)*

### Centrum powiadomień i szybkie ustawienia
- [ ] Na pasku (dock albo górny pasek) **przycisk powiadomień** i **przycisk szybkich funkcji**.
- [ ] **Powiadomienia**: historia powiadomień (programy, bateria, aktualizacje, urządzenia audio), czyszczenie pojedynczo i „Wyczyść wszystko”.
- [ ] **Szybkie funkcje**: włącz/wyłącz internet (Wi-Fi / sieć), Bluetooth (gdy jest), tryb samolotowy, jasność (laptop), głośność itp.

### Okna
- [ ] **Przyciąganie okien do krawędzi / rogów** (Aero Snap): własna animacja i ładniejszy wygląd podglądu (dziś: duży półprzezroczysty prostokąt Windows).

### Muzyka w górnym pasku (Full) — ustalenia
- [x] Prośba: korzystać z **Global System Media Transport Controls (GSMTC)** zawsze. Zrobione 15.10.2026 w buildzie, sprawdzone na żywo, niezainstalowane — Veyra Full sama hostuje usługę NPSM (szczegóły w projekt.md).
  Ustalenie z 13.10.2026 (sonda na żywo): klient GSMTC (`Windows.Media.Control`, biblioteka `Windows.Media.MediaControl.dll`) łączy się z klasą COM
  **NowPlayingSessionManager** `{BCBB9860-C012-4AD7-A938-6E337AE6ABA5}` z grupy **ImmersiveShellBrokers** (AppID `{2fd08a73-d1f1-43eb-b888-24c2496f95fd}`,
  kod w `NPSM.dll` / `twinui.appcore.dll`). Tę usługę uruchamia **explorer.exe działający jako powłoka**; bez niego GSMTC zwraca „Klasa niezarejestrowana”
  (0x80040154), a sam Eksplorator jako okno folderu nie wystarcza. W Minimal GSMTC działa (Eksplorator w tle). W Full możliwe drogi: Spotify Web API,
  odczyt kart przeglądarki (UI Automation), rozszerzenie przeglądarki albo Eksplorator w tle także w Full — **decyzja użytkownika**.
