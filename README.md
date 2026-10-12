<div align="center">

# 🧱 Veyra

**Lekka, niestandardowa powłoka Windows – alternatywa dla `explorer.exe`**

[![Release](https://img.shields.io/github/v/release/Promileczek/Veyra?style=for-the-badge&color=blue)](../../releases)
[![License](https://img.shields.io/github/license/Promileczek/Veyra?style=for-the-badge&color=green)](LICENSE)
[![Stars](https://img.shields.io/github/stars/Promileczek/Veyra?style=for-the-badge&color=yellow)](../../stargazers)
![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?style=for-the-badge&logo=windows&logoColor=white)

[Budowanie](#-budowanie-i-uruchamianie) · [Funkcje](#-funkcje) · [Skróty](#%EF%B8%8F-skróty-klawiszowe) · [Zgłoś błąd](../../issues)

<img src="https://placehold.co/900x450?text=Screenshot+Veyra" alt="Veyra screenshot" width="85%">

</div>

---

## 🎯 O projekcie

**Veyra** zastępuje `explorer.exe` jako powłoka systemu: pulpit z tapetą, dock, górny pasek, panel aplikacji, zasobnik, Alt+Tab, menedżer plików i własne aplikacje. Wygląd łączy Windows 11, KDE Plasma i macOS (kropki zamiast przycisków okien, szkło), a całość jest pisana w C++ (Direct3D 11 + Direct2D + DirectComposition) z myślą o **małym zużyciu RAM** (~90 MB cała powłoka).

> [!NOTE]
> Powłoka włącza się w pełni (pulpit, zasobnik, kropki w oknach, Alt+Tab), gdy `explorer.exe` nie działa. Uruchomiona obok Eksploratora działa jako sam dock i górny pasek.

## ✨ Funkcje

| Funkcja | Status | Opis |
| --- | :---: | --- |
| 🖥️ Pulpit | ✅ | Tapeta z ustawień Windows, szklane panele, bez ikon (z wyboru) |
| 📊 Dock | ✅ | Styl macOS, ikony aplikacji, podpowiedzi, chowa się przy pełnym ekranie |
| 🪟 Menu Windows | ✅ | Aplikacje, Ustawienia, Menedżer zadań, Uruchom…, zasilanie |
| 🚀 Panel aplikacji | ✅ | Przypięte programy, wyszukiwarka wszystkich aplikacji, okienko „Dodaj” |
| 📈 Statystyki | ✅ | Procesor, RAM, dysk, sieć, czas działania, czas w aplikacjach |
| 🔔 Zasobnik systemowy | ✅ | Ikony tray schowane pod przyciskiem „Tray” |
| 🎵 Górny pasek | ✅ | Sieć i Wi-Fi, muzyka (Spotify/YouTube), głośność z suwakiem |
| 🔀 Alt+Tab | ✅ | Własny przełącznik z podglądem okien |
| 🔴 Kropki macOS | ✅ | Zamknij / minimalizuj / maksymalizuj w oknach innych programów |
| ✂️ Zrzuty ekranu | ✅ | Win+Shift+S: wycinek albo cały ekran do schowka |
| 📁 Menedżer plików | ✅ | Okno w stylu Findera, nowe foldery, kopiuj/wklej, menu kontekstowe |
| 💻 Terminal | ✅ | W stylu Linuksa (ConPTY + cmd, aliasy `ls`, `cat`, `grep`…) |
| 🧮 Kalkulator | ✅ | Własny wygląd, historia obliczeń, klawiatura |
| 🛍️ Sklep | ✅ | Popularne programy instalowane jednym kliknięciem (winget) |
| 🎧 Spotify przez API | 📋 | Klucz API w ustawieniach |
| 🔔 Powiadomienia | 📋 | Własne dymki powiadomień |
| ⚙️ Ustawienia | 📋 | Okno ustawień, motywy z JSON |
| 🖥️🖥️ Wiele monitorów | 📋 | Dock i pasek na każdym ekranie |

✅ gotowe · 🚧 w trakcie · 📋 planowane

## 🚀 Budowanie i uruchamianie

Wymagania: Windows 10 1809+ (ConPTY), Visual Studio 2022 Build Tools (MSVC, Windows SDK, CMake, Ninja).

```bat
build.bat
```

Wynik trafia do `%LOCALAPPDATA%\Exploder\build\`: `exploder.exe` (powłoka) oraz `exploder-terminal.exe`, `exploder-calc.exe`, `exploder-store.exe`.

Uruchomienie jako powłoka: zamknij `explorer.exe` (Menedżer zadań → Eksplorator Windows → Zakończ zadanie) i uruchom `exploder.exe`.

> [!TIP]
> Chcesz wrócić do Eksploratora? <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Q</kbd> albo PPM na docku → „Wróć do explorer.exe”. Gdyby powłoka się zawiesiła: Menedżer zadań → Uruchom nowe zadanie → `explorer.exe`.

Ustawienia i dane: `%LOCALAPPDATA%\Exploder\` (`settings.txt`, `launcher.txt`, statystyki użycia, ikony sklepu, `crash.dmp` przy awarii).

## ⌨️ Skróty klawiszowe

| Skrót | Akcja |
| --- | --- |
| <kbd>Alt</kbd> + <kbd>Tab</kbd> | Przełącz okno (z podglądem) |
| <kbd>Win</kbd> + <kbd>Shift</kbd> + <kbd>S</kbd> | Zrzut ekranu: wycinek albo cały ekran |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Q</kbd> | Awaryjne wyjście i powrót do `explorer.exe` |
| Pisanie na pulpicie | Wyszukiwanie aplikacji w panelu |

## 🗺️ Roadmapa

- [x] Pulpit i tapeta
- [x] Dock i menu Windows
- [x] Panel aplikacji
- [x] Zasobnik systemowy
- [x] Alt+Tab, kropki macOS, zrzuty ekranu
- [x] Menedżer plików
- [x] Terminal, Kalkulator, Sklep
- [ ] Spotify przez API
- [ ] Powiadomienia
- [ ] Okno ustawień i motywy (JSON)
- [ ] Wiele monitorów

## 🧩 Struktura

| Folder | Zawartość |
| --- | --- |
| `src/core` | Grafika (D3D11/D2D/DComp), szkło, okno aplikacji, popupy, ustawienia |
| `src/shell` | Śledzenie okien, zasobnik, Alt+Tab, kropki, statystyki, zrzuty |
| `src/taskbar` | Dock, ikony aplikacji, menu Windows, panel Tray |
| `src/desktop` | Pulpit, panel aplikacji, panel plików, statystyki |
| `src/files` | Menedżer plików |
| `src/topbar` | Górny pasek: sieć, muzyka, głośność |
| `apps/` | Terminal, Kalkulator, Sklep |
| `res/`, `tools/` | Ikony aplikacji i ich generator |

## 📄 Licencja

Udostępniane na licencji **MIT**. Szczegóły w pliku [LICENSE](LICENSE).

---

<div align="center">

Jeśli projekt Ci się podoba, zostaw ⭐ na GitHubie!

</div>
