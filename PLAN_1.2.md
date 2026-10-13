# Veyra 1.2 — plan

Stan wyjściowy: **1.1.5.2** (13.10.2026).

## Błędy

- [ ] **Po aktualizacji z Ustawień wraca stary Eksplorator Windows** (pasek zadań Windows obok Veyry) — zauważone przy aktualizacji 1.1.5 → 1.1.5.2 z prośbą UAC. Po aktualizacji **nie może** wrócić ani pasek / pulpit Eksploratora, ani sam proces explorer.exe (w Full) — tylko Veyra.
- [ ] **Okno UAC przy aktualizacji** (systemowe „Czy chcesz zezwolić… Windows PowerShell”) — ma być w stylu Veyry.
  Uwaga: samego okna UAC nie da się podmienić. Rysuje je systemowy consent.exe na osobnym, zabezpieczonym pulpicie, do którego programy nie mają dostępu; Windows nie ma na to żadnego interfejsu, a podmiana pliku systemowego psułaby się po aktualizacjach i osłabiała zabezpieczenia. Do wyboru:
  - aktualizacja bez UAC: instalator tworzy zadanie Harmonogramu z najwyższymi uprawnieniami, które tylko podmienia pliki Veyry, albo instalacja w folderze użytkownika zamiast Program Files;
  - albo przed UAC własne okienko Veyry z wyjaśnieniem („Veyra potrzebuje zgody, żeby podmienić pliki”).
- [ ] **Spotify nie ma kropek macOS** (i inne aplikacje Electron, np. VS Code) — dziś kropki są tylko w zwykłych oknach, Brave/Chrome/Edge i Discordzie.
- [ ] **Okienko „Uruchom” (Win+R)** nie da się przesunąć, zminimalizować ani zamknąć jak zwykłego okna.
- [ ] Puste czarne okienka z samymi kropkami macOS w rogach ekranu (nakładki WindowDots dla niewidocznych okien).
- [ ] Statystyki „Dziś w aplikacjach” pokazują „Application Frame Host” zamiast nazwy aplikacji ze Sklepu (użyć AUMID jak w docku).
- [ ] Kropki macOS: kolor tła nie zawsze trafia (nieaktywne okna, Discord po przełączeniu).
- [ ] Alt+Tab: ciemny podgląd zminimalizowanych przeglądarek.
- [ ] Szkło docka pokazuje rozmytą tapetę zamiast okien za dockiem.
- [ ] Minimal: pasek zadań Windows miga po zalogowaniu, zanim Veyra go schowa.
- [ ] Minimal: po zabiciu Veyry (Menedżer zadań) pasek Windows zostaje schowany.
- [ ] Sklep: postęp instalacji nie zawsze w procentach.

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
