# Veyra 1.2 — plan

Stan wyjściowy: **1.1.5.2** (13.10.2026).

## Błędy

- [ ] **Po aktualizacji z Ustawień wraca stary Eksplorator Windows** (pasek zadań Windows obok Veyry) — zauważone przy aktualizacji 1.1.5 → 1.1.5.2 z prośbą UAC. Po aktualizacji **nie może** wrócić ani pasek / pulpit Eksploratora, ani sam proces explorer.exe (w Full) — tylko Veyra.
- [ ] **Okno UAC przy aktualizacji** (systemowe „Czy chcesz zezwolić… Windows PowerShell”) — ma być w stylu Veyry.
  Uwaga: samego okna UAC nie da się podmienić. Rysuje je systemowy consent.exe na osobnym, zabezpieczonym pulpicie, do którego programy nie mają dostępu; Windows nie ma na to żadnego interfejsu, a podmiana pliku systemowego psułaby się po aktualizacjach i osłabiała zabezpieczenia. Do wyboru:
  - aktualizacja bez UAC: instalator tworzy zadanie Harmonogramu z najwyższymi uprawnieniami, które tylko podmienia pliki Veyry, albo instalacja w folderze użytkownika zamiast Program Files;
  - albo przed UAC własne okienko Veyry z wyjaśnieniem („Veyra potrzebuje zgody, żeby podmienić pliki”).
- [ ] **UAC tylko raz — przy instalacji.** Instalator (który i tak pyta o uprawnienia administratora) tworzy zadanie Harmonogramu zadań z najwyższymi uprawnieniami (np. „Veyra Update”). Później aktualizacje i inne akcje wymagające administratora (podmiana plików w Program Files, wpisy w rejestrze HKLM — IFEO calc.exe / taskmgr.exe, wpis deinstalacji) Veyra wykonuje przez to zadanie — **bez okna UAC**. Zadanie przyjmuje tylko z góry określone akcje (nie dowolne polecenia), deinstalacja je usuwa.
- [ ] **Spotify nie ma kropek macOS** (i inne aplikacje Electron, np. VS Code) — dziś kropki są tylko w zwykłych oknach, Brave/Chrome/Edge i Discordzie.
- [ ] **Okienko „Uruchom” (Win+R)** nie da się przesunąć, zminimalizować ani zamknąć jak zwykłego okna.
- [ ] **Czasem aplikacje się bugują i nie widać kropek macOS** (kropki znikają z okna, które powinno je mieć).
- [ ] Puste czarne okienka z samymi kropkami macOS w rogach ekranu (nakładki WindowDots dla niewidocznych okien).
- [ ] Statystyki „Dziś w aplikacjach” pokazują „Application Frame Host” zamiast nazwy aplikacji ze Sklepu (użyć AUMID jak w docku).
- [ ] Kropki macOS: kolor tła nie zawsze trafia (nieaktywne okna, Discord po przełączeniu).
- [ ] Alt+Tab: ciemny podgląd zminimalizowanych przeglądarek.
- [ ] Szkło docka pokazuje rozmytą tapetę zamiast okien za dockiem.
- [ ] Minimal: pasek zadań Windows miga po zalogowaniu, zanim Veyra go schowa.
- [ ] Minimal: po zabiciu Veyry (Menedżer zadań) pasek Windows zostaje schowany.
- [ ] Sklep: postęp instalacji nie zawsze w procentach.
- [ ] **Obwódki ikon**: niektóre aplikacje w panelu aplikacji mają białą/ciemną obwódkę wokół ikony (np. Zdjęcia iCloud), Discord ma dziwną otoczkę.
- [ ] **Obwódka górnego paska** — dziwna jasna otoczka wokół paska; to samo przy podświetleniu ikony w docku po najechaniu.
- [ ] **Tray**: nie wszystkie ikony mają nazwy („(bez opisu)” — np. Discord, BlueStacks); brać nazwę programu (opis exe / okno), gdy podpowiedź jest pusta.
- [ ] **Ustawienia**: przy przewijaniu treść (karty) nachodzi na kropki okna w prawym górnym rogu — zostawić pas na górze albo przycinać treść pod paskiem tytułu.
- [ ] **Kalkulator** da się powiększyć na pełny ekran — ma mieć jeden, stały rozmiar (bez maksymalizacji i F11).
- [ ] **Panel „Pulpit”**: siatka folderów ma większy odstęp z lewej niż z prawej — wyrównać (ten sam margines po obu stronach).

## Do zrobienia

### Animacje
- [ ] Powiadomienia: animacja pojawiania się wszędzie, gdzie jej brakuje (powiadomienie o aktualizacji już wjeżdża z prawej — ten sam styl dla innych).
- [ ] Otwieranie menu Start.
- [ ] Otwieranie okienka głośności i okienka sieci (internet) w górnym pasku.
- [ ] Inna, własna animacja minimalizowania i przywracania aplikacji.

### Bateria (laptop)
- [ ] Gdy Veyra wykryje laptop (bateria) — w górnym pasku obok głośności stan baterii (procent, ładowanie).
- [ ] Powiadomienia: 15%, 10% i 5% baterii oraz „naładowana w pełni”.

### Menedżer zadań Veyry
- [ ] Sprawdzone: podmiana jest możliwa — przez „Image File Execution Options” dla `taskmgr.exe` (wartość Debugger), tak samo jak robi to Process Explorer („Replace Task Manager”) i jak Veyra podmienia `calc.exe`. Wtedy Ctrl+Shift+Esc, Ctrl+Alt+Del → Menedżer zadań i `taskmgr` otwierają aplikację Veyry.
- [ ] Własny Menedżer zadań w stylu Veyry z zakładkami: **Procesy**, **Wydajność**, **Uruchamianie** (programy autostartu).
- [ ] Przełącznik podmiany w Ustawieniach (jak przy Kalkulatorze), wyłączenie przywraca systemowy.

### Ekran Ctrl+Alt+Del w stylu Veyry
- [ ] Cel: te same opcje co w Windows (Zablokuj, Przełącz użytkownika, Wyloguj, Zmień hasło, Menedżer zadań, zasilanie) w stylu Veyry.
- [ ] Uwaga: ekranu Ctrl+Alt+Del Windows nie pozwala podmienić ani przechwycić — obsługuje go Winlogon na zabezpieczonym pulpicie (jak UAC). Da się tylko ukrywać jego pozycje zasadami systemu. Realnie:
  - „Menedżer zadań” z ekranu Ctrl+Alt+Del i tak otworzy Menedżer zadań Veyry (po podmianie taskmgr.exe);
  - własny ekran opcji Veyry z tymi samymi akcjami pod innym skrótem (np. Ctrl+Shift+Del albo z menu Start → zasilanie) — skrót do ustalenia.

### Ustawienia
- [ ] Kolejność zakładek: **Aktualizacje** na samej górze, pod nimi **Informacje**, pod nimi **System**.
- [ ] **Pulpit i okna** przenieść do **Personalizacji**.
- [ ] **Rozmiar czcionki** (Terminal) przenieść do **Personalizacji**.
- [ ] **Aplikacje**: podkategorie w miejscu listy zakładek, ze strzałką „wstecz”:
  - **Aplikacje Veyry**,
  - **Zainstalowane programy**.
- [ ] **Informacje → Komputer**: mniej szczegółów — bez liczby rdzeni procesora i bez informacji o dyskach.
- [ ] Nowa zakładka **Konta** z informacją „W budowie”: tu będzie zakładanie kont i logowanie do konta Veyra (zapisywanie ustawień, tapet itp. w chmurze).
- [ ] Ustawienia → Personalizacja (lub Pulpit): **wyłączenie panelu statystyk** — wtedy panel „Pliki” (foldery pulpitu) idzie pod panel aplikacji.
- [ ] Ustawienia → **Sklep**: wybór, co pokazuje Sklep (aplikacje Veyry, programy z internetu / winget, kategorie itp.).
- [ ] Ustawienia → **Dźwięk**: wybór urządzenia, z którego leci dźwięk (głośniki, słuchawki…). Po podłączeniu nowego urządzenia audio — powiadomienie Veyry (tam, gdzie Windows pokazałby swoje).
- [ ] Ustawienia → **Mysz**: nowy kursor w stylu Veyry; „potrząśnij, aby znaleźć” — gdy mysz szybko porusza się w jednym miejscu, kursor na chwilę się powiększa (jak w macOS); włączanie i wielkość w Ustawieniach.

### Dock, menu Start, ikony
- [ ] **Ikona Veyry** (logo) i **własna ikona folderu** w stylu Veyry.
- [ ] Przycisk Windows w docku (cztery kwadraty) → **ikona Veyry**.
- [ ] Menu Start, nagłówek: usunąć podpis „Exploder”, a zamiast niebieskiej ikony pokazać **zdjęcie / ikonę konta użytkownika z Windows**.
- [ ] **Grupowanie okien w docku**: kilka okien tego samego programu (np. 2× Brave) = jedna ikona z oznaczeniem liczby okien (kilka kropek pod ikoną / licznik); kliknięcie przełącza między nimi albo pokazuje listę z podglądami.

### Centrum powiadomień i szybkie ustawienia
- [ ] Na pasku (dock albo górny pasek) **przycisk powiadomień** i **przycisk szybkich funkcji**.
- [ ] **Powiadomienia**: historia powiadomień (programy, bateria, aktualizacje, urządzenia audio), czyszczenie pojedynczo i „Wyczyść wszystko”.
- [ ] **Szybkie funkcje**: włącz/wyłącz internet (Wi-Fi / sieć), Bluetooth (gdy jest), tryb samolotowy, jasność (laptop), głośność itp.

### Okna
- [ ] **Przyciąganie okien do krawędzi / rogów** (Aero Snap): własna animacja i ładniejszy wygląd podglądu (dziś: duży półprzezroczysty prostokąt Windows).

### Muzyka w górnym pasku (Full) — ustalenia
- [ ] Prośba: korzystać z **Global System Media Transport Controls (GSMTC)** zawsze.
  Ustalenie z 13.10.2026 (sonda na żywo): klient GSMTC (`Windows.Media.Control`, biblioteka `Windows.Media.MediaControl.dll`) łączy się z klasą COM
  **NowPlayingSessionManager** `{BCBB9860-C012-4AD7-A938-6E337AE6ABA5}` z grupy **ImmersiveShellBrokers** (AppID `{2fd08a73-d1f1-43eb-b888-24c2496f95fd}`,
  kod w `NPSM.dll` / `twinui.appcore.dll`). Tę usługę uruchamia **explorer.exe działający jako powłoka**; bez niego GSMTC zwraca „Klasa niezarejestrowana”
  (0x80040154), a sam Eksplorator jako okno folderu nie wystarcza. W Minimal GSMTC działa (Eksplorator w tle). W Full możliwe drogi: Spotify Web API,
  odczyt kart przeglądarki (UI Automation), rozszerzenie przeglądarki albo Eksplorator w tle także w Full — **decyzja użytkownika**.
