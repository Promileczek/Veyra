<div align="center">

# 🧱 Veyra

**Lekka, niestandardowa powłoka Windows – alternatywa dla `explorer.exe`**

[![Release](https://img.shields.io/github/v/release/Promileczek/Veyra?style=for-the-badge&color=blue)](../../releases)
[![License](https://img.shields.io/github/license/Promileczek/Veyra?style=for-the-badge&color=green)](LICENSE)
[![Stars](https://img.shields.io/github/stars/Promileczek/Veyra?style=for-the-badge&color=yellow)](../../stargazers)
![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?style=for-the-badge&logo=windows&logoColor=white)

[Instalacja](#-instalacja) · [Budowanie](#-budowanie) · [Funkcje](#-funkcje) · [Skróty](#%EF%B8%8F-skróty-klawiszowe) · [Zgłoś błąd](../../issues)

<img src="https://placehold.co/900x450?text=Screenshot+Veyra" alt="Veyra screenshot" width="85%">

</div>

---

## 🎯 O projekcie

**Veyra** zastępuje `explorer.exe` jako powłoka systemu. Pisana w C++ (Direct2D + DirectComposition), lekka — działa w jednym procesie C++.

Dostępne w dwóch wersjach (wybierasz w instalatorze):

| Wersja | Opis |
| --- | --- |
| **Minimal** | Działa razem z `explorer.exe` w tle (ukrywa jego pasek i pulpit). Zostają systemowe aplikacje Windows (Kalkulator, Ustawienia, Sklep), działają powiadomienia i aplikacje UWP. Bez kropek macOS i aplikacji Veyry. |
| **Full** | Veyra zastępuje `explorer.exe` jako powłoka. Własne aplikacje (Terminal, Kalkulator, Sklep, Ustawienia, Pliki) i kropki macOS w oknach. Aplikacje UWP nie startują, a część funkcji Windows (Centrum akcji, Win+Tab) nie działa. |

> [!WARNING]
> Projekt jest we wczesnej fazie i ma jeszcze sporo błędów. Zawsze pamiętaj o powrocie do Eksploratora: <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Q</kbd>.

## ✨ Funkcje

| Funkcja | Status |
| --- | :---: |
| 🖥️ Pulpit, dock i górny pasek | ✅ |
| 🚀 Panel aplikacji z wyszukiwarką | ✅ |
| 🧩 Zasobnik systemowy | ✅ |
| 🔀 Alt+Tab z podglądem okien | ✅ |
| 📁 Menedżer plików | ✅ |
| 💻 Terminal, 🧮 Kalkulator, 🛍️ Sklep | ✅ |
| ⚙️ Ustawienia i aktualizacje | ✅ |
| 📦 Instalator (Minimal / Full) | ✅ |
| 🔔 Powiadomienia | 📋 |
| 🎨 Motywy | 📋 |
| 🖥️ Wiele monitorów | 📋 |

✅ gotowe · 🚧 w trakcie · 📋 planowane

## 📦 Instalacja

1. Pobierz `VeyraSetup.exe` z [wydań](../../releases).
2. Uruchom, wybierz wersję **Minimal** albo **Full** i zainstaluj (do `C:\Program Files\Veyra`, wymaga UAC).
3. Odinstalowanie: Aplikacje Windows → Veyra (albo `VeyraSetup.exe /odinstaluj`). Przywraca Eksplorator i Kalkulator Windows.

> [!TIP]
> Powrót do Eksploratora: <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Q</kbd> albo PPM na docku → „Wróć do explorer.exe”. Gdyby powłoka się zawiesiła: Menedżer zadań → Uruchom nowe zadanie → `explorer.exe`.

Aktualizacje: Ustawienia → **Aktualizacje** (wersja w pliku `VERSION`, porównywana z najnowszym wydaniem na GitHubie).

## 🔨 Budowanie

Wymagania: Windows 10 1809+ (ConPTY), Visual Studio 2022 Build Tools (MSVC, Windows SDK, CMake, Ninja).

```bat
build.bat
```

Wynik trafia do `%LOCALAPPDATA%\Exploder\build\`: `exploder.exe` (powłoka), `exploder-terminal.exe`, `exploder-calc.exe`, `exploder-store.exe`, `exploder-settings.exe` oraz `veyra-setup.exe` (instalator).

Ustawienia i dane: `%LOCALAPPDATA%\Exploder\` (`settings.txt`, `launcher.txt`, statystyki użycia, `crash.dmp` przy awarii).

## ⌨️ Skróty klawiszowe

| Skrót | Akcja |
| --- | --- |
| <kbd>Alt</kbd> + <kbd>Tab</kbd> | Przełącz okno (z podglądem) |
| <kbd>Win</kbd> + <kbd>Shift</kbd> + <kbd>S</kbd> | Zrzut ekranu: wycinek albo cały ekran |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Q</kbd> | Awaryjne wyjście i powrót do `explorer.exe` |
| Pisanie na pulpicie | Wyszukiwanie aplikacji w panelu |

## 🗺️ Roadmapa

- [x] Pulpit, dock, panel aplikacji
- [x] Zasobnik i Alt+Tab
- [x] Menedżer plików
- [x] Terminal, Kalkulator, Sklep
- [x] Ustawienia i własna tapeta
- [x] Instalator (Minimal / Full) i aktualizacje
- [ ] Powiadomienia
- [ ] Motywy
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
| `apps/` | Terminal, Kalkulator, Sklep, Ustawienia, instalator |
| `res/`, `tools/` | Ikony, tapeta i ich generatory |

## 📄 Licencja

Udostępniane na licencji **MIT**. Szczegóły w pliku [LICENSE](LICENSE).

---

<div align="center">

Jeśli projekt Ci się podoba, zostaw ⭐ na GitHubie!

</div>
