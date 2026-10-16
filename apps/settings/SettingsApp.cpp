// Ustawienia Veyry — styl jak reszta powłoki (grafit, akcent #61B3FF, płaskie karty).
// Sekcje: Personalizacja (tapeta), Pulpit i okna (kropki macOS), Aplikacje (czcionka terminala),
// System (Veyra zamiast Eksploratora po zalogowaniu, skróty do ustawień Windows), Informacje.
// Zmiany zapisuje do settings.txt i rozsyła "VeyraSettingsChanged" — powłoka i terminal stosują je od razu.
#include "../../src/core/AppWindow.h"
#include "../../src/core/Edition.h"
#include "../../src/core/Settings.h"
#include "../../src/core/UiScale.h"
#include "Displays.h"
#include "Programs.h"
#include "SystemInfo.h"
#include "Updater.h"
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr UINT WM_THUMBS = WM_APP + 30;  // miniatury tapet gotowe
constexpr UINT_PTR kInfoTimer = 1;
constexpr UINT_PTR kUpdateTimer = 2;     // odczyt postępu aktualizacji
constexpr UINT_PTR kCalcTimer = 3;       // stan podmiany kalkulatora po zgodzie UAC
constexpr UINT_PTR kModeTimer = 4;       // odliczanie: zachować nową rozdzielczość?
constexpr UINT WM_CHECKED = WM_APP + 31;  // sprawdzanie wersji zakończone
constexpr float kSideW = 210.f, kPad = 28.f, kRowH = 64.f;
const D2D1_COLOR_F kBody = D2D1::ColorF(0x1C1C21), kSide = D2D1::ColorF(0x17171B), kAccent = D2D1::ColorF(0.38f, 0.70f, 1.f),
                   kOnAccent = D2D1::ColorF(0x0B1A26);
D2D1_COLOR_F White(float a) { return D2D1::ColorF(1.f, 1.f, 1.f, a); }

// Desktop (Pulpit i okna) scalone z Personalizacją — zostaje w enumie dla zgodności indeksów, ale nie ma go w nawigacji
enum Section { Personalize, Desktop, Display, Apps, System, Updates, About, Accounts, Mouse, SectionCount };
const wchar_t* const kSectionNames[] = {L"Personalizacja", L"Pulpit i okna", L"Ekran",      L"Aplikacje",
                                        L"System",         L"Aktualizacje",  L"Informacje", L"Konta", L"Mysz"};
const wchar_t kSectionGlyphs[] = {L'\xE771', L'\xE7F4', L'\xE7F8', L'\xE71D', L'\xE770', L'\xE895', L'\xE946', L'\xE77B', L'\xE962'};
// Veyra Minimal ma tylko Ekran i Informacje (reszta dotyczy aplikacji Veyry, których w Minimal nie ma).
// Kolejność Full: Aktualizacje, Informacje, System na górze, potem Personalizacja, Mysz, Ekran, Aplikacje, Konta.
std::vector<int> VisibleSections() {
    if (Edition::Minimal()) return {Display, About};
    return {Updates, About, System, Personalize, Mouse, Display, Apps, Accounts};
}

// Skala Veyry: wartości ustawienia "uiScale" (0 = auto)
constexpr int kScales[] = {0, 75, 90, 100, 125, 150, 175};  // jak w Windows (+ mniejsze)

std::wstring LowerCopy(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

// "Twoje tapety": do 3 — najpierw wybrane w Ustawieniach (najnowsze pierwsze), potem tapety Windows sprzed Veyry
// (zapamiętana przy pierwszym starcie i historia tła Windows). Bez tapet Veyry.
std::vector<std::wstring> UserWallpaperPaths() {
    std::vector<std::wstring> out;
    auto add = [&](const std::wstring& p) {
        if (p.empty() || out.size() >= 3 || GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        if (LowerCopy(p.substr(p.find_last_of(L'\\') + 1)).rfind(L"veyra-", 0) == 0) return;
        for (const auto& o : out)
            if (_wcsicmp(o.c_str(), p.c_str()) == 0) return;
        out.push_back(p);
    };
    const std::wstring custom = Settings::Get(L"customWallpapers");
    for (size_t a = 0, b; a < custom.size(); a = b + 1) {
        b = custom.find(L'|', a);
        if (b == std::wstring::npos) b = custom.size();
        add(custom.substr(a, b - a));
    }
    add(Settings::Get(L"previousWallpaper"));
    for (int i = 0; i < 5; ++i) {
        wchar_t value[MAX_PATH]{};
        DWORD size = sizeof(value);
        const std::wstring name = L"BackgroundHistoryPath" + std::to_wstring(i);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers", name.c_str(),
                         RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS)
            add(value);
    }
    return out;
}

struct WinLink {
    const wchar_t* label;
    const wchar_t* desc;
    const wchar_t* uri;
};
const WinLink kWinLinks[] = {
    {L"Sieć i Internet", L"Wi-Fi, Ethernet, VPN", L"ms-settings:network"},
    {L"Dźwięk", L"Urządzenia wyjścia i wejścia", L"ms-settings:sound"},
    {L"Ekran", L"Rozdzielczość, skalowanie, monitory", L"ms-settings:display"},
    {L"Bluetooth i urządzenia", L"Myszy, słuchawki, drukarki", L"ms-settings:bluetooth"},
    {L"Aplikacje", L"Instalowanie i odinstalowywanie", L"ms-settings:appsfeatures"},
    {L"Konta", L"Twoje konto, logowanie", L"ms-settings:yourinfo"},
    {L"Windows Update", L"Aktualizacje systemu", L"ms-settings:windowsupdate"},
    {L"Panel sterowania", L"Klasyczne ustawienia", L"control.exe"},
};

constexpr wchar_t kWinlogon[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";

std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L'\\') + 1);
}

// Veyra jako powłoka tylko dla tego użytkownika (HKCU\...\Winlogon\Shell); usunięcie wartości = znowu explorer.exe
bool ShellIsVeyra() {
    wchar_t value[MAX_PATH * 2]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kWinlogon, L"Shell", RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return false;
    std::wstring v = value;
    std::transform(v.begin(), v.end(), v.begin(), ::towlower);
    return v.find(L"exploder.exe") != std::wstring::npos;
}
bool SetShellVeyra(bool on) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kWinlogon, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    LSTATUS st;
    if (on) {
        const std::wstring cmd = L"\"" + ExeDir() + L"exploder.exe\"";
        st = RegSetValueExW(key, L"Shell", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, L"Shell");
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

// Program Veyry zamiast systemowego przez Image File Execution Options\<exe>\Debugger → <program Veyry>.
// Każde uruchomienie <exe> (Win+R, skróty, Ctrl+Shift+Esc) otwiera program Veyry; usunięcie wpisu przywraca systemowy.
// HKLM wymaga administratora — bez uprawnień zadanie Harmonogramu (bez UAC), a bez niego reg.exe z prośbą UAC.
std::wstring IfeoKey(const wchar_t* sysExe) {
    return std::wstring(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\") + sysExe;
}
bool IfeoIsVeyra(const wchar_t* sysExe, const wchar_t* veyraExe) {
    wchar_t value[MAX_PATH * 2]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, IfeoKey(sysExe).c_str(), L"Debugger", RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS)
        return false;
    std::wstring v = value;
    std::transform(v.begin(), v.end(), v.begin(), ::towlower);
    return v.find(veyraExe) != std::wstring::npos;
}
// true = zapisane od razu; false = uruchomiono zadanie/reg.exe (wynik sprawdzamy później)
bool SetIfeoVeyra(const wchar_t* sysExe, const wchar_t* veyraExe, const wchar_t* taskOn, const wchar_t* taskOff, bool on) {
    const std::wstring ifeo = IfeoKey(sysExe);
    const std::wstring debugger = L"\"" + ExeDir() + veyraExe + L"\"";
    HKEY key = nullptr;
    LSTATUS st = on ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, ifeo.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
                    : RegOpenKeyExW(HKEY_LOCAL_MACHINE, ifeo.c_str(), 0, KEY_SET_VALUE, &key);
    if (st == ERROR_SUCCESS) {
        st = on ? RegSetValueExW(key, L"Debugger", 0, REG_SZ, reinterpret_cast<const BYTE*>(debugger.c_str()),
                                 static_cast<DWORD>((debugger.size() + 1) * sizeof(wchar_t)))
                : RegDeleteValueW(key, L"Debugger");
        RegCloseKey(key);
        if (st == ERROR_SUCCESS || (!on && st == ERROR_FILE_NOT_FOUND)) return true;
    } else if (!on && st == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (Updater::TaskExists() && Updater::RequestTask(on ? taskOn : taskOff)) return false;
    const std::wstring regKey = L"\"HKLM\\" + ifeo + L"\"";
    const std::wstring args = on ? L"add " + regKey + L" /v Debugger /t REG_SZ /d \"\\\"" + ExeDir() + veyraExe + L"\\\"\" /f"
                                 : L"delete " + regKey + L" /v Debugger /f";
    ShellExecuteW(nullptr, L"runas", L"reg.exe", args.c_str(), nullptr, SW_HIDE);
    return false;
}
bool CalcIsVeyra() { return IfeoIsVeyra(L"calc.exe", L"exploder-calc.exe"); }
bool SetCalcVeyra(bool on) { return SetIfeoVeyra(L"calc.exe", L"exploder-calc.exe", L"kalkulator-wlacz", L"kalkulator-wylacz", on); }
bool TaskMgrIsVeyra() { return IfeoIsVeyra(L"taskmgr.exe", L"exploder-taskmgr.exe"); }
bool SetTaskMgrVeyra(bool on) { return SetIfeoVeyra(L"taskmgr.exe", L"exploder-taskmgr.exe", L"menedzer-wlacz", L"menedzer-wylacz", on); }

// Pamięć prywatna powłoki (exploder.exe) w MB; -1 = nie działa
int ShellMemoryMb() {
    int mb = -1;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return mb;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"exploder.exe") != 0) continue;
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            if (GetProcessMemoryInfo(p, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
                mb = static_cast<int>(pmc.PrivateUsage / (1024 * 1024));
            CloseHandle(p);
        }
        break;
    }
    CloseHandle(snap);
    return mb;
}

std::wstring CurrentWallpaper() {
    wchar_t path[MAX_PATH]{};
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0);
    return path;
}

class SettingsApp : public AppWindow {
public:
    bool Init(HINSTANCE inst, Graphics& g) {
        Options o;
        o.className = L"ExploderSettings";
        o.title = L"Ustawienia";
        o.width = 900.f;
        o.height = 600.f;
        o.minWidth = 700.f;
        o.minHeight = 440.f;
        titleH_ = 40.f;
        showTitle_ = false;
        if (!Create(inst, g, o)) return false;
        headerFormat_ = MakeFormat(L"Segoe UI", 24.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        bigCenterFormat_ = MakeFormat(L"Segoe UI", 24.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        appFormat_ = MakeFormat(L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        sideFormat_ = MakeFormat(L"Segoe UI", 13.f);
        titleFormat_ = MakeFormat(L"Segoe UI", 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        descFormat_ = MakeFormat(L"Segoe UI", 11.5f);
        buttonFormat_ = MakeFormat(L"Segoe UI", 12.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        glyphFormat_ = MakeFormat(L"Segoe MDL2 Assets", 15.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        glyphBigFormat_ = MakeFormat(L"Segoe MDL2 Assets", 34.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        sectionLabelFormat_ = MakeFormat(L"Segoe UI", 11.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        wrapFormat_ = MakeFormat(L"Segoe UI", 11.5f);  // opis w kilku liniach
        if (wrapFormat_) {
            wrapFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            wrapFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        wallpaper_ = CurrentWallpaper();
        shellVeyra_ = ShellIsVeyra();
        calcVeyra_ = CalcIsVeyra();
        taskmgrVeyra_ = TaskMgrIsVeyra();
        monitors_ = Displays::List();
        if (Edition::Minimal()) section_ = Display;
        StartThumbnails();
        if (wcsstr(GetCommandLineW(), L"--aktualizacje")) OpenUpdates();
        Redraw();
        return true;
    }

    ~SettingsApp() override {
        stop_ = true;
        if (thumbThread_.joinable()) thumbThread_.join();
        if (checkThread_.joinable()) checkThread_.join();
    }

protected:
    // ---------------- Rysowanie ----------------
    void Paint(ID2D1DeviceContext* dc) override {
        hits_.clear();
        const float W = widthDip_, H = heightDip_;
        dc->Clear(kBody);
        Fill(dc, {0.f, 0.f, kSideW, H}, kSide);
        Fill(dc, {kSideW - 1.f, 0.f, kSideW, H}, White(0.06f));
        Text(dc, L"Ustawienia", appFormat_.Get(), {20.f, 44.f, kSideW - 10.f, 70.f}, White(0.95f));
        const std::vector<int> visible = VisibleSections();
        for (size_t n = 0; n < visible.size(); ++n) {
            const int i = visible[n];
            const float top = 82.f + static_cast<float>(n) * 38.f;
            const D2D1_RECT_F r{10.f, top, kSideW - 10.f, top + 34.f};
            if (i == section_) {
                Fill(dc, r, White(0.10f), 8.f);
                Fill(dc, {r.left, r.top + 9.f, r.left + 3.f, r.bottom - 9.f}, kAccent, 1.5f);
            } else if (hover_ == HitId(HitKind::Section, i)) {
                Fill(dc, r, White(0.05f), 8.f);
            }
            Text(dc, std::wstring(1, kSectionGlyphs[i]), glyphFormat_.Get(), {r.left + 8.f, r.top, r.left + 36.f, r.bottom}, White(0.85f));
            Text(dc, kSectionNames[i], sideFormat_.Get(), {r.left + 44.f, r.top, r.right - 6.f, r.bottom}, White(0.92f));
            AddHit(r, HitKind::Section, i);
        }

        // Zawartość (przewijana) — pod paskiem tytułu, żeby karty nie wjeżdżały na kropki okna
        const D2D1_RECT_F content{kSideW, titleH_, W, H};
        dc->PushAxisAlignedClip(content, D2D1_ANTIALIAS_MODE_ALIASED);
        float y = 44.f - scroll_;
        Text(dc, kSectionNames[section_], headerFormat_.Get(), {kSideW + kPad, y, W - kPad, y + 36.f}, White(0.97f));
        y += 56.f;
        switch (section_) {
        case Personalize: y = PaintPersonalize(dc, y); break;
        case Display: y = PaintDisplay(dc, y); break;
        case Apps: y = PaintApps(dc, y); break;
        case System: y = PaintSystem(dc, y); break;
        case Updates: y = PaintUpdates(dc, y); break;
        case About: y = PaintAbout(dc, y); break;
        case Accounts: y = PaintAccounts(dc, y); break;
        case Mouse: y = PaintMouse(dc, y); break;
        default: break;
        }
        dc->PopAxisAlignedClip();
        contentH_ = y + scroll_ + kPad;
    }

    float PaintPersonalize(ID2D1DeviceContext* dc, float y) {
        const float L = kSideW + kPad, R = widthDip_ - kPad;
        Label(dc, L"TAPETA", L, y);
        y += 24.f;
        // Podgląd bieżącej tapety + nazwa
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const float pw = 280.f, ph = pw * 9.f / 16.f;
            const D2D1_RECT_F prev{L, y, L + pw, y + ph};
            Fill(dc, prev, White(0.06f), 12.f);
            for (auto& t : thumbs_)
                if (_wcsicmp(t.path.c_str(), wallpaper_.c_str()) == 0) DrawThumb(dc, t, prev, 12.f);
            const std::wstring name = wallpaper_.empty() ? L"Brak tapety" : wallpaper_.substr(wallpaper_.find_last_of(L'\\') + 1);
            Text(dc, L"Bieżąca tapeta", titleFormat_.Get(), {prev.right + 20.f, y + 4.f, R, y + 26.f}, White(0.95f));
            Text(dc, name, descFormat_.Get(), {prev.right + 20.f, y + 26.f, R, y + 46.f}, White(0.55f));
            Text(dc, L"Kliknij obraz poniżej, aby ustawić go jako tapetę Veyry.",
                 wrapFormat_.Get(), {prev.right + 20.f, y + 50.f, R, y + 100.f}, White(0.45f));
            y += ph + 22.f;

            const float gap = 12.f;
            const int cols = std::max(2, static_cast<int>((R - L + gap) / (170.f + gap)));
            const float tw = (R - L - (cols - 1) * gap) / cols, th = tw * 9.f / 16.f;
            // Twoje tapety (do 3) + kafelek "Wybierz obraz…"
            Label(dc, L"TWOJE TAPETY", L, y);
            y += 24.f;
            int n = 0;
            for (size_t i = 0; i < thumbs_.size(); ++i) {
                if (!thumbs_[i].user) continue;
                const float x = L + (n % cols) * (tw + gap), ty = y + (n / cols) * (th + gap);
                ThumbTile(dc, i, {x, ty, x + tw, ty + th});
                ++n;
            }
            {
                const float x = L + (n % cols) * (tw + gap), ty = y + (n / cols) * (th + gap);
                const D2D1_RECT_F r{x, ty, x + tw, ty + th};
                Fill(dc, r, White(hover_ == HitId(HitKind::PickWallpaper, 0) ? 0.10f : 0.06f), 10.f);
                Text(dc, L"+", bigCenterFormat_.Get(), {r.left, r.top + th / 2 - 34.f, r.right, r.top + th / 2 + 4.f}, White(0.8f));
                Text(dc, L"Wybierz obraz…", buttonFormat_.Get(), {r.left, r.top + th / 2 + 4.f, r.right, r.top + th / 2 + 26.f}, White(0.8f));
                AddHit(r, HitKind::PickWallpaper, 0);
                ++n;
            }
            y += ((n + cols - 1) / cols) * (th + gap) + 10.f;
            Label(dc, L"TAPETY VEYRY", L, y);
            y += 24.f;
            if (thumbs_.empty()) {
                Text(dc, thumbsDone_ ? L"Nie znaleziono obrazów" : L"Wczytywanie…", descFormat_.Get(), {L, y, R, y + 20.f}, White(0.5f));
                y += 30.f;
            } else {
                n = 0;
                for (size_t i = 0; i < thumbs_.size(); ++i) {
                    if (thumbs_[i].user) continue;
                    const float x = L + (n % cols) * (tw + gap), ty = y + (n / cols) * (th + gap);
                    ++n;
                    ThumbTile(dc, i, {x, ty, x + tw, ty + th});
                }
                y += ((n + cols - 1) / cols) * (th + gap);
            }
        }
        // Pulpit i okna (dawna osobna zakładka) + rozmiar czcionki Terminala — przeniesione tu
        y += 22.f;
        Label(dc, L"PULPIT I OKNA", L, y);
        y += 24.f;
        const D2D1_RECT_F dots = Card(dc, y, 2);
        Toggle(dc, dots, 0, L"Kropki macOS w oknach programów",
               L"Czerwona, żółta i zielona kropka zamiast przycisków zamknij / minimalizuj / maksymalizuj",
               Settings::GetBool(L"windowDots", true), 0);
        Fill(dc, {dots.left + 18.f, dots.top + kRowH, dots.right - 18.f, dots.top + kRowH + 1.f}, White(0.06f));
        Toggle(dc, dots, 1, L"Panel statystyk na pulpicie",
               L"Procesor, pamięć, dysk i sieć obok panelu aplikacji", Settings::GetBool(L"showStats", true), 5);
        y = dots.bottom + 22.f;
        Label(dc, L"ROZMIAR CZCIONKI (TERMINAL)", L, y);
        y += 24.f;
        y = FontSizeCard(dc, y);
        Text(dc, L"Panel aplikacji na pulpicie: kliknij „+”, aby przypiąć program. PPM na kafelku → Usuń z panelu.", descFormat_.Get(),
             {L, y, R, y + 20.f}, White(0.55f));
        return y + 30.f;
    }

    // Karta „Rozmiar czcionki” (Terminal) — używana w Personalizacji
    float FontSizeCard(ID2D1DeviceContext* dc, float y) {
        const D2D1_RECT_F card = Card(dc, y, 1);
        const D2D1_RECT_F row{card.left, card.top, card.right, card.top + kRowH};
        Text(dc, L"Rozmiar czcionki Terminala", titleFormat_.Get(), {row.left + 18.f, row.top + 12.f, row.right - 300.f, row.top + 32.f}, White(0.95f));
        Text(dc, L"Też w terminalu: Ctrl + kółko myszy, Ctrl + 0 przywraca 14", descFormat_.Get(),
             {row.left + 18.f, row.top + 32.f, row.right - 300.f, row.top + 52.f}, White(0.5f));
        const int size = std::clamp(_wtoi(Settings::Get(L"terminalFontSize", L"14").c_str()), 8, 32);
        const D2D1_RECT_F minus{row.right - 150.f, row.top + 16.f, row.right - 118.f, row.top + 48.f};
        const D2D1_RECT_F plus{row.right - 50.f, row.top + 16.f, row.right - 18.f, row.top + 48.f};
        Button(dc, minus, L"−", HitId(HitKind::FontMinus, 0));
        Button(dc, plus, L"+", HitId(HitKind::FontPlus, 0));
        Text(dc, std::to_wstring(size) + L" pt", buttonFormat_.Get(), {minus.right, row.top, plus.left, row.bottom}, White(0.95f));
        AddHit(minus, HitKind::FontMinus, 0);
        AddHit(plus, HitKind::FontPlus, 0);
        return card.bottom + 22.f;
    }

    float PaintAccounts(ID2D1DeviceContext* dc, float y) {
        const float L = kSideW + kPad, R = widthDip_ - kPad;
        const D2D1_RECT_F card{L, y, R, y + 150.f};
        Fill(dc, card, White(0.05f), 12.f);
        brush_->SetColor(White(0.08f));
        dc->DrawRoundedRectangle(D2D1::RoundedRect({card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f}, 12.f, 12.f), brush_.Get(), 1.f);
        Text(dc, L"\xE77B", glyphBigFormat_.Get(), {card.left, card.top + 24.f, card.right, card.top + 64.f}, kAccent);
        Text(dc, L"Konto Veyra — w budowie", titleFormat_.Get(), {card.left, card.top + 70.f, card.right, card.top + 94.f}, White(0.95f));
        Text(dc, L"Wkrótce: logowanie do konta Veyra i zapisywanie ustawień, tapet i układu w chmurze.",
             wrapFormat_.Get(), {card.left + 40.f, card.top + 96.f, card.right - 40.f, card.top + 140.f}, White(0.55f));
        return card.bottom + 20.f;
    }

    float PaintMouse(ID2D1DeviceContext* dc, float y) {
        const float L = kSideW + kPad, R = widthDip_ - kPad;
        Label(dc, L"KURSOR I WSKAŹNIK", L, y);
        y += 24.f;
        const D2D1_RECT_F card = Card(dc, y, 2);
        Toggle(dc, card, 0, L"Kursor w stylu Veyry",
               L"Ciemny grafit, biały obrys i akcent #61B3FF zamiast standardowego kursora Windows",
               Settings::GetBool(L"veyraCursor", true), 6);
        Fill(dc, {card.left + 18.f, card.top + kRowH, card.right - 18.f, card.top + kRowH + 1.f}, White(0.06f));
        Toggle(dc, card, 1, L"„Potrząśnij, aby znaleźć”",
               L"Gdy szybko potrząśniesz myszą w jednym miejscu, kursor na chwilę się powiększa (jak w macOS)",
               Settings::GetBool(L"shakeToFind", true), 7);
        y = card.bottom + 22.f;

        Label(dc, L"WIELKOŚĆ POWIĘKSZENIA KURSYRA", L, y);
        y += 24.f;
        const D2D1_RECT_F scaleCard = Card(dc, y, 1);
        const D2D1_RECT_F row{scaleCard.left, scaleCard.top, scaleCard.right, scaleCard.top + kRowH};
        Text(dc, L"Rozmiar powiększonego kursora", titleFormat_.Get(), {row.left + 18.f, row.top + 12.f, row.right - 350.f, row.top + 32.f}, White(0.95f));
        Text(dc, L"Wybierz stopień powiększenia podczas potrząsania myszą", descFormat_.Get(), {row.left + 18.f, row.top + 32.f, row.right - 350.f, row.top + 52.f}, White(0.5f));

        const int curScale = std::clamp(_wtoi(Settings::Get(L"shakeScale", L"96").c_str()), 48, 128);

        struct ScaleOption { int scale; const wchar_t* label; };
        const ScaleOption options[] = {
            {64, L"Standard (64 px)"},
            {96, L"Duży (96 px)"},
            {128, L"Maksymalny (128 px)"}
        };

        float bx = row.right - 334.f;
        const float bw = 104.f, bh = 32.f, by = row.top + 16.f;
        for (const auto& opt : options) {
            const D2D1_RECT_F btnRect{bx, by, bx + bw, by + bh};
            const bool active = (curScale == opt.scale) || (opt.scale == 96 && curScale != 64 && curScale != 128);
            const bool hot = hover_ == HitId(HitKind::MouseScale, opt.scale);
            Fill(dc, btnRect, active ? kAccent : White(hot ? 0.14f : 0.08f), 8.f);
            Text(dc, opt.label, buttonFormat_.Get(), btnRect, active ? kOnAccent : White(0.95f));
            AddHit(btnRect, HitKind::MouseScale, opt.scale);
            bx += bw + 8.f;
        }

        y = scaleCard.bottom + 20.f;
        Text(dc, L"Wskazówka: kursor powiększa się natychmiast po dynamicznym potrząśnięciu myszą na ekranie.", descFormat_.Get(),
             {L, y, R, y + 20.f}, White(0.55f));
        return y + 30.f;
    }

    // Miniatura tapety z obramowaniem bieżącej / najechanej (mutex_ trzymany przez wołającego)
    void ThumbTile(ID2D1DeviceContext* dc, size_t i, const D2D1_RECT_F& r) {
        Fill(dc, r, White(0.06f), 10.f);
        DrawThumb(dc, thumbs_[i], r, 10.f);
        const bool current = _wcsicmp(thumbs_[i].path.c_str(), wallpaper_.c_str()) == 0;
        const bool hot = hover_ == HitId(HitKind::Wallpaper, static_cast<int>(i));
        if (current || hot) {
            brush_->SetColor(current ? kAccent : White(0.5f));
            dc->DrawRoundedRectangle(D2D1::RoundedRect({r.left + 1.f, r.top + 1.f, r.right - 1.f, r.bottom - 1.f}, 10.f, 10.f), brush_.Get(),
                                     current ? 2.5f : 1.5f);
        }
        AddHit(r, HitKind::Wallpaper, static_cast<int>(i));
    }

    float PaintDisplay(ID2D1DeviceContext* dc, float y) {
        const float L = kSideW + kPad, R = widthDip_ - kPad;
        Label(dc, L"SKALA VEYRY", L, y);
        y += 24.f;
        const D2D1_RECT_F card = Card(dc, y, 2);
        const int autoPercent = static_cast<int>(std::lround(UiScale::AutoFactor() * 100.f));
        const int systemPercent = static_cast<int>(std::lround(GetDpiForSystem() * 100.0 / 96.0));
        Text(dc, L"Wielkość pulpitu, docku, górnego paska i menu", titleFormat_.Get(),
             {card.left + 18.f, card.top + 12.f, card.right - 18.f, card.top + 32.f}, White(0.95f));
        Text(dc, L"Auto dopasowuje Veyrę do ekranu (teraz " + std::to_wstring(autoPercent) + L"%). Skalowanie Windows: " +
                     std::to_wstring(systemPercent) + L"% — aplikacje skaluje Windows",
             descFormat_.Get(), {card.left + 18.f, card.top + 32.f, card.right - 18.f, card.top + 52.f}, White(0.5f));
        const std::wstring current = Settings::Get(L"uiScale", L"auto");
        float bx = card.left + 18.f;
        for (int i = 0; i < static_cast<int>(std::size(kScales)); ++i) {
            const D2D1_RECT_F b{bx, card.top + kRowH + 12.f, bx + 66.f, card.top + kRowH + 44.f};
            const bool on = kScales[i] ? _wtoi(current.c_str()) == kScales[i] : _wtoi(current.c_str()) == 0;
            if (on) {
                Fill(dc, b, kAccent, 8.f);
                Text(dc, i ? std::to_wstring(kScales[i]) + L"%" : L"Auto", buttonFormat_.Get(), b, kOnAccent);
            } else {
                Button(dc, b, (i ? std::to_wstring(kScales[i]) + L"%" : std::wstring(L"Auto")).c_str(), HitId(HitKind::Scale, i));
            }
            AddHit(b, HitKind::Scale, i);
            bx += 74.f;
        }
        y = card.bottom + 26.f;

        Label(dc, (L"MONITORY (" + std::to_wstring(monitors_.size()) + L")").c_str(), L, y);
        y += 24.f;
        // Układ: przeciągnij monitor i puść — przyklei się do krawędzi sąsiedniego
        const D2D1_RECT_F area{L, y, R, y + 230.f};
        Fill(dc, area, White(0.04f), 12.f);
        if (!monitors_.empty()) {
            if (dragIndex_ < 0) {  // podczas przeciągania skala widoku stoi w miejscu
                int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
                for (const auto& m : monitors_)
                    minX = std::min(minX, m.x), minY = std::min(minY, m.y), maxX = std::max(maxX, m.x + m.width), maxY = std::max(maxY, m.y + m.height);
                const float bw = static_cast<float>(maxX - minX), bh = static_cast<float>(maxY - minY);
                viewK_ = std::min((area.right - area.left - 60.f) / bw, (area.bottom - area.top - 40.f) / bh);
                viewX_ = (area.left + area.right) / 2 - (minX + bw / 2) * viewK_;
                viewY_ = (area.top + area.bottom) / 2 - (minY + bh / 2) * viewK_;
            }
            for (size_t i = 0; i < monitors_.size(); ++i) {
                const auto& m = monitors_[i];
                const D2D1_RECT_F r{viewX_ + m.x * viewK_ + 2.f, viewY_ + m.y * viewK_ + 2.f, viewX_ + (m.x + m.width) * viewK_ - 2.f,
                                    viewY_ + (m.y + m.height) * viewK_ - 2.f};
                const bool hot = static_cast<int>(i) == dragIndex_ || hover_ == HitId(HitKind::Monitor, static_cast<int>(i));
                Fill(dc, r, White(hot ? 0.16f : 0.10f), 8.f);
                brush_->SetColor(m.primary ? kAccent : White(0.25f));
                dc->DrawRoundedRectangle(D2D1::RoundedRect({r.left + 1.f, r.top + 1.f, r.right - 1.f, r.bottom - 1.f}, 8.f, 8.f), brush_.Get(),
                                         m.primary ? 2.f : 1.f);
                const float cy = (r.top + r.bottom) / 2;
                Text(dc, std::to_wstring(i + 1), bigCenterFormat_.Get(), {r.left, cy - 30.f, r.right, cy + 4.f}, White(0.95f));
                Text(dc, std::to_wstring(m.width) + L"×" + std::to_wstring(m.height), buttonFormat_.Get(), {r.left, cy + 6.f, r.right, cy + 24.f},
                     White(0.6f));
                AddHit(r, HitKind::Monitor, static_cast<int>(i));
            }
        }
        y = area.bottom + 12.f;
        Text(dc, monitors_.size() > 1 ? L"Przeciągnij monitor, aby ustawić go po lewej, prawej, nad albo pod innym, potem kliknij Zastosuj."
                                      : L"Podłączony jest jeden monitor.",
             descFormat_.Get(), {L, y, R, y + 20.f}, White(0.5f));
        y += 28.f;
        float bxx = L;
        if (layoutChanged_) {
            Button(dc, {bxx, y, bxx + 120.f, y + 34.f}, L"Zastosuj", HitId(HitKind::ApplyLayout, 0));
            AddHit({bxx, y, bxx + 120.f, y + 34.f}, HitKind::ApplyLayout, 0);
            bxx += 132.f;
            Button(dc, {bxx, y, bxx + 120.f, y + 34.f}, L"Cofnij", HitId(HitKind::ResetLayout, 0));
            AddHit({bxx, y, bxx + 120.f, y + 34.f}, HitKind::ResetLayout, 0);
            bxx += 132.f;
        }
        if (Edition::Minimal()) {  // z Eksploratorem w tle działają też Ustawienia Windows
            Button(dc, {bxx, y, bxx + 220.f, y + 34.f}, L"Ekran w Ustawieniach Windows", HitId(HitKind::WindowsDisplay, 0));
            AddHit({bxx, y, bxx + 220.f, y + 34.f}, HitKind::WindowsDisplay, 0);
            bxx += 232.f;
        }
        if (!displayStatus_.empty()) Text(dc, displayStatus_, descFormat_.Get(), {bxx + 8.f, y, R, y + 34.f}, White(0.6f));
        if (layoutChanged_ || Edition::Minimal() || !displayStatus_.empty()) y += 50.f;
        // Po zmianie rozdzielczości: potwierdzenie albo powrót za 15 s (jak w Windows)
        if (modeRevert_.device.size()) {
            const D2D1_RECT_F bar{L, y, R, y + 56.f};
            Fill(dc, bar, D2D1::ColorF(0.38f, 0.70f, 1.f, 0.14f), 12.f);
            Text(dc, L"Zachować nowe ustawienia ekranu? Powrót do poprzednich za " + std::to_wstring(modeSeconds_) + L" s", titleFormat_.Get(),
                 {bar.left + 18.f, bar.top, bar.right - 260.f, bar.bottom}, White(0.95f));
            const D2D1_RECT_F keep{bar.right - 250.f, bar.top + 11.f, bar.right - 136.f, bar.bottom - 11.f};
            const D2D1_RECT_F back{bar.right - 128.f, bar.top + 11.f, bar.right - 14.f, bar.bottom - 11.f};
            Fill(dc, keep, kAccent, 8.f);
            Text(dc, L"Zachowaj", buttonFormat_.Get(), keep, kOnAccent);
            Button(dc, back, L"Przywróć", HitId(HitKind::ModeRevert, 0));
            AddHit(keep, HitKind::ModeKeep, 0);
            AddHit(back, HitKind::ModeRevert, 0);
            y = bar.bottom + 14.f;
        }
        // Lista monitorów
        if (!monitors_.empty()) {
            const D2D1_RECT_F list = Card(dc, y, static_cast<int>(monitors_.size()));
            for (size_t i = 0; i < monitors_.size(); ++i) {
                const auto& m = monitors_[i];
                const D2D1_RECT_F r{list.left, list.top + i * kRowH, list.right, list.top + (i + 1) * kRowH};
                if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
                Text(dc, std::to_wstring(i + 1) + L".  " + m.name + (m.primary ? L"  (główny)" : L""), titleFormat_.Get(),
                     {r.left + 18.f, r.top + 12.f, r.right - 18.f, r.top + 32.f}, White(0.95f));
                std::wstring d = std::to_wstring(m.width) + L" × " + std::to_wstring(m.height);
                if (m.hz > 1) d += L"  ·  " + std::to_wstring(m.hz) + L" Hz";
                if (m.diagonalIn > 0) {
                    wchar_t in[16];
                    swprintf_s(in, L"%.1f″", m.diagonalIn);
                    d += L"  ·  " + std::wstring(in);
                }
                d += L"  ·  skalowanie Windows " + std::to_wstring(m.scale) + L"%";
                Text(dc, d, descFormat_.Get(), {r.left + 18.f, r.top + 32.f, r.right - 290.f, r.top + 52.f}, White(0.5f));
                // Rozdzielczość i odświeżanie (menu z trybami monitora)
                const D2D1_RECT_F res{r.right - 278.f, r.top + 16.f, r.right - 118.f, r.top + 48.f};
                const D2D1_RECT_F hz{r.right - 110.f, r.top + 16.f, r.right - 18.f, r.top + 48.f};
                Button(dc, res, (std::to_wstring(m.width) + L" × " + std::to_wstring(m.height) + L"  ▾").c_str(), HitId(HitKind::ResMenu, static_cast<int>(i)));
                Button(dc, hz, (std::to_wstring(m.hz) + L" Hz  ▾").c_str(), HitId(HitKind::HzMenu, static_cast<int>(i)));
                AddHit(res, HitKind::ResMenu, static_cast<int>(i));
                AddHit(hz, HitKind::HzMenu, static_cast<int>(i));
            }
            y = list.bottom + 20.f;
        }
        return y;
    }

    float PaintApps(ID2D1DeviceContext* dc, float y) {
        Label(dc, L"APLIKACJE VEYRY", kSideW + kPad, y);
        y += 24.f;
        const wchar_t* apps[][3] = {{L"Terminal", L"Konsola w stylu Linuksa", L"exploder-terminal.exe"},
                                    {L"Kalkulator", L"Z historią obliczeń", L"exploder-calc.exe"},
                                    {L"Sklep", L"Programy instalowane jednym kliknięciem", L"exploder-store.exe"},
                                    {L"Menedżer zadań", L"Procesy, wydajność, autostart", L"exploder-taskmgr.exe"}};
        const int appCount = static_cast<int>(std::size(apps));
        const D2D1_RECT_F list = Card(dc, y, appCount);
        for (int i = 0; i < appCount; ++i) {
            const D2D1_RECT_F r{list.left, list.top + i * kRowH, list.right, list.top + (i + 1) * kRowH};
            if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
            Text(dc, apps[i][0], titleFormat_.Get(), {r.left + 18.f, r.top + 12.f, r.right - 140.f, r.top + 32.f}, White(0.95f));
            Text(dc, apps[i][1], descFormat_.Get(), {r.left + 18.f, r.top + 32.f, r.right - 140.f, r.top + 52.f}, White(0.5f));
            const D2D1_RECT_F b{r.right - 110.f, r.top + 16.f, r.right - 18.f, r.top + 48.f};
            Button(dc, b, L"Otwórz", HitId(HitKind::OpenApp, i));
            AddHit(b, HitKind::OpenApp, i);
        }
        y = list.bottom + 26.f;
        return PaintPrograms(dc, y);
    }

    // Zainstalowane programy z odinstalowaniem; pisanie na tej stronie filtruje listę
    float PaintPrograms(ID2D1DeviceContext* dc, float y) {
        const float L = kSideW + kPad, R = widthDip_ - kPad;
        Label(dc, (L"ZAINSTALOWANE PROGRAMY (" + std::to_wstring(programs_.size()) + L")").c_str(), L, y);
        y += 24.f;
        const D2D1_RECT_F search{L, y, R, y + 38.f};
        Fill(dc, search, White(0.06f), 10.f);
        Text(dc, L"\xE721", glyphFormat_.Get(), {search.left + 6.f, search.top, search.left + 36.f, search.bottom}, White(0.5f));
        Text(dc, programQuery_.empty() ? L"Pisz, aby wyszukać program…" : programQuery_ + L"▏", descFormat_.Get(),
             {search.left + 40.f, search.top, search.right - 12.f, search.bottom}, White(programQuery_.empty() ? 0.4f : 0.95f));
        y = search.bottom + 12.f;
        programView_.clear();
        const std::wstring q = LowerCopy(programQuery_);
        for (size_t i = 0; i < programs_.size(); ++i)
            if (q.empty() || LowerCopy(programs_[i].name + L" " + programs_[i].publisher).find(q) != std::wstring::npos) programView_.push_back(i);
        if (programView_.empty()) {
            Text(dc, programs_.empty() ? L"Wczytywanie…" : L"Brak pasujących programów", descFormat_.Get(), {L, y, R, y + 20.f}, White(0.5f));
            return y + 30.f;
        }
        const D2D1_RECT_F card = Card(dc, y, static_cast<int>(programView_.size()));
        for (size_t n = 0; n < programView_.size(); ++n) {
            const auto& p = programs_[programView_[n]];
            const D2D1_RECT_F r{card.left, card.top + n * kRowH, card.right, card.top + (n + 1) * kRowH};
            if (r.bottom < 0.f || r.top > heightDip_) continue;  // poza ekranem — bez rysowania
            if (n) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
            Text(dc, p.name, titleFormat_.Get(), {r.left + 18.f, r.top + 12.f, r.right - 140.f, r.top + 32.f}, White(0.95f));
            std::wstring d = p.publisher;
            if (!p.version.empty()) d += (d.empty() ? L"" : L"  ·  ") + p.version;
            if (p.sizeKb) {
                wchar_t sz[32];
                swprintf_s(sz, p.sizeKb >= 1024 * 1024 ? L"%.1f GB" : L"%.0f MB", p.sizeKb >= 1024 * 1024 ? p.sizeKb / 1048576.0 : p.sizeKb / 1024.0);
                d += (d.empty() ? L"" : L"  ·  ") + std::wstring(sz);
            }
            Text(dc, d, descFormat_.Get(), {r.left + 18.f, r.top + 32.f, r.right - 140.f, r.top + 52.f}, White(0.5f));
            const D2D1_RECT_F b{r.right - 120.f, r.top + 16.f, r.right - 18.f, r.top + 48.f};
            Button(dc, b, L"Odinstaluj", HitId(HitKind::Uninstall, static_cast<int>(n)));
            AddHit(b, HitKind::Uninstall, static_cast<int>(n));
        }
        return card.bottom + 20.f;
    }

    float PaintSystem(ID2D1DeviceContext* dc, float y) {
        Label(dc, L"POWŁOKA", kSideW + kPad, y);
        y += 24.f;
        const D2D1_RECT_F card = Card(dc, y, 3);
        Toggle(dc, card, 2, L"Menedżer zadań Veyry zamiast systemowego",
               L"Ctrl+Shift+Esc, Ctrl+Alt+Del → Menedżer zadań i taskmgr.exe otwierają Menedżer Veyry. Wymaga uprawnień administratora",
               taskmgrVeyra_, 4);
        Fill(dc, {card.left + 18.f, card.top + 2 * kRowH, card.right - 18.f, card.top + 2 * kRowH + 1.f}, White(0.06f));
        Toggle(dc, card, 1, L"Kalkulator Veyry zamiast Kalkulatora Windows",
               L"calc.exe (Win+R, skróty, klawisz Kalkulator) otwiera Kalkulator Veyry. Wymaga uprawnień administratora", calcVeyra_, 2);
        Fill(dc, {card.left + 18.f, card.top + kRowH, card.right - 18.f, card.top + kRowH + 1.f}, White(0.06f));
        const wchar_t* desc = confirmShell_ ? L"Kliknij jeszcze raz, aby potwierdzić. Awaryjnie: Ctrl+Alt+Shift+Q albo Menedżer zadań → explorer.exe"
                                            : L"Tylko dla Twojego konta. Wyłączenie przywraca Eksplorator przy następnym logowaniu";
        Toggle(dc, card, 0, L"Uruchamiaj Veyrę zamiast Eksploratora po zalogowaniu", desc, shellVeyra_, 1);
        if (!shellError_.empty())
            Text(dc, shellError_, descFormat_.Get(), {card.left + 18.f, card.bottom + 4.f, card.right, card.bottom + 22.f}, D2D1::ColorF(0xFF8A80));
        y = card.bottom + 26.f;
        Label(dc, L"USTAWIENIA WINDOWS", kSideW + kPad, y);
        y += 24.f;
        const int n = static_cast<int>(std::size(kWinLinks));
        const D2D1_RECT_F list = Card(dc, y, n);
        for (int i = 0; i < n; ++i) {
            const D2D1_RECT_F r{list.left, list.top + i * kRowH, list.right, list.top + (i + 1) * kRowH};
            if (hover_ == HitId(HitKind::WinLink, i)) Fill(dc, r, White(0.04f), i == 0 || i == n - 1 ? 12.f : 0.f);
            if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
            Text(dc, kWinLinks[i].label, titleFormat_.Get(), {r.left + 18.f, r.top + 12.f, r.right - 60.f, r.top + 32.f}, White(0.95f));
            Text(dc, kWinLinks[i].desc, descFormat_.Get(), {r.left + 18.f, r.top + 32.f, r.right - 60.f, r.top + 52.f}, White(0.5f));
            Text(dc, L"\xE8A7", glyphFormat_.Get(), {r.right - 50.f, r.top, r.right - 18.f, r.bottom}, White(0.5f));  // otwórz zewnętrznie
            AddHit(r, HitKind::WinLink, i);
        }
        return list.bottom + 20.f;
    }

    float PaintUpdates(ID2D1DeviceContext* dc, float y) {
        const float L = kSideW + kPad;
        {  // automatyczne sprawdzanie (powłoka: minutę po starcie, potem co 6 h) → powiadomienie
            const D2D1_RECT_F autoCard = Card(dc, y, 1);
            Toggle(dc, autoCard, 0, L"Sprawdzaj aktualizacje automatycznie",
                   L"Co 6 godzin; o nowej wersji powie powiadomienie w prawym górnym rogu", Settings::GetBool(L"autoUpdateCheck", true), 3);
            y = autoCard.bottom + 22.f;
        }
        Label(dc, L"WERSJA", L, y);
        y += 24.f;
        const D2D1_RECT_F card = Card(dc, y, 3);
        auto row = [&](int i, const std::wstring& k, const std::wstring& v, D2D1_COLOR_F c) {
            const D2D1_RECT_F r{card.left, card.top + i * kRowH, card.right, card.top + (i + 1) * kRowH};
            if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
            Text(dc, k, titleFormat_.Get(), {r.left + 18.f, r.top, r.left + 240.f, r.bottom}, White(0.95f));
            Text(dc, v, titleFormat_.Get(), {r.left + 240.f, r.top, r.right - 18.f, r.bottom}, c);
        };
        const bool newer = checked_ && Updater::Compare(latest_, Updater::Current()) > 0;
        row(0, L"Zainstalowana wersja", Updater::Current(), White(0.8f));
        row(1, L"Najnowsza wersja", checking_ ? L"Sprawdzanie…" : checked_ ? latest_ : L"—", newer ? kAccent : White(0.8f));
        std::wstring state;
        D2D1_COLOR_F stateColor = White(0.8f);
        if (updating_) state = L"Aktualizowanie…", stateColor = kAccent;
        else if (checking_) state = L"Sprawdzanie…";
        else if (!checkError_.empty()) state = L"Nie udało się sprawdzić", stateColor = D2D1::ColorF(0xFF8A80);
        else if (!checked_) state = L"Nie sprawdzono";
        else if (newer) state = L"Dostępna aktualizacja", stateColor = kAccent;
        else state = L"\x2713  Masz najnowszą wersję", stateColor = D2D1::ColorF(0x5BD27A);
        row(2, L"Stan", state, stateColor);
        y = card.bottom + 16.f;

        // Przyciski
        const D2D1_RECT_F check{L, y, L + 150.f, y + 34.f};
        if (!updating_ && !checking_) {
            Button(dc, check, L"Sprawdź teraz", HitId(HitKind::CheckUpdate, 0));
            AddHit(check, HitKind::CheckUpdate, 0);
        }
        if (newer && !updating_) {
            const D2D1_RECT_F upd{check.right + 12.f, y, check.right + 232.f, y + 34.f};
            if (tools_ || !binaryZip_.empty()) {  // gotowe pliki z wydania nie potrzebują Build Tools
                Fill(dc, upd, hover_ == HitId(HitKind::DoUpdate, 0) ? D2D1::ColorF(0.48f, 0.77f, 1.f) : kAccent, 8.f);
                Text(dc, L"Zaktualizuj do " + latest_, buttonFormat_.Get(), upd, kOnAccent);
                AddHit(upd, HitKind::DoUpdate, 0);
            } else {
                Text(dc, L"Do aktualizacji potrzebne są Visual Studio Build Tools", descFormat_.Get(), {upd.left, y, widthDip_ - kPad, y + 34.f},
                     D2D1::ColorF(0xFF8A80));
            }
        }
        y += 46.f;
        // Komunikat: błąd sprawdzania albo postęp/wynik aktualizacji
        std::wstring note;
        D2D1_COLOR_F noteColor = White(0.6f);
        if (!checkError_.empty()) note = checkError_, noteColor = D2D1::ColorF(0xFF8A80);
        if (!updateStatus_.empty()) {
            if (updateStatus_.rfind(L"OK ", 0) == 0) note = L"Zaktualizowano do wersji " + updateStatus_.substr(3), noteColor = D2D1::ColorF(0x5BD27A);
            else if (updateStatus_.rfind(L"BŁĄD", 0) == 0) note = L"Aktualizacja nie powiodła się: " + updateStatus_.substr(5), noteColor = D2D1::ColorF(0xFF8A80);
            else note = updateStatus_, noteColor = kAccent;
        }
        if (!note.empty()) {
            Text(dc, note, wrapFormat_.Get(), {L, y, widthDip_ - kPad, y + 40.f}, noteColor);
            y += 44.f;
        }
        Text(dc, binaryZip_.empty() || !checked_
                     ? L"Aktualizacja pobiera nową wersję z GitHuba (gotowe pliki z wydania albo kod, który zostanie zbudowany na tym komputerze). "
                       L"Veyra zamknie się na chwilę, podmieni pliki i uruchomi ponownie. Gdy coś się nie uda, nic nie zostanie zmienione."
                     : L"Aktualizacja pobiera gotowe pliki z wydania na GitHubie. Veyra zamknie się na chwilę, podmieni pliki i uruchomi ponownie.",
             wrapFormat_.Get(), {L, y, widthDip_ - kPad, y + 40.f}, White(0.45f));
        y += 52.f;

        if (!changes_.empty()) {
            Label(dc, L"CO NOWEGO (OSTATNIE ZMIANY)", L, y);
            y += 24.f;
            const int n = static_cast<int>(changes_.size());
            const D2D1_RECT_F list = Card(dc, y, n);
            for (int i = 0; i < n; ++i) {
                const D2D1_RECT_F r{list.left, list.top + i * kRowH, list.right, list.top + (i + 1) * kRowH};
                if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
                Text(dc, changes_[static_cast<size_t>(i)], descFormat_.Get(), {r.left + 18.f, r.top, r.right - 18.f, r.bottom}, White(0.8f));
            }
            y = list.bottom + 20.f;
        }
        return y;
    }

    // Wejście na zakładkę: stan skryptu (np. po aktualizacji) + sprawdzenie wersji raz na sesję
    void OpenUpdates() {
        section_ = Updates;
        tools_ = Updater::BuildToolsAvailable();
        updateStatus_ = Updater::Status();
        const bool running = !updateStatus_.empty() && updateStatus_.rfind(L"OK ", 0) != 0 && updateStatus_.rfind(L"BŁĄD", 0) != 0;
        if (running) {  // skrypt jeszcze pracuje (Ustawienia otwarte ponownie w trakcie)
            updating_ = true;
            SetTimer(hwnd_, kUpdateTimer, 700, nullptr);
        }
        if (!checked_ && !checking_) StartCheck();
    }

    void StartCheck() {
        if (checking_) return;
        checking_ = true;
        checkError_.clear();
        if (checkThread_.joinable()) checkThread_.join();
        const HWND hwnd = hwnd_;
        checkThread_ = std::thread([this, hwnd] {
            Updater::CheckResult r = Updater::Check();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                checkResult_ = std::move(r);
            }
            if (!stop_) PostMessageW(hwnd, WM_CHECKED, 0, 0);
        });
    }

    float PaintAbout(ID2D1DeviceContext* dc, float y) {
        const float L = kSideW + kPad;
        auto table = [&](const wchar_t* label, const std::vector<SystemInfo::Row>& rows) {
            Label(dc, label, L, y);
            y += 24.f;
            const D2D1_RECT_F card = Card(dc, y, static_cast<int>(rows.size()));
            for (size_t i = 0; i < rows.size(); ++i) {
                const D2D1_RECT_F r{card.left, card.top + i * kRowH, card.right, card.top + (i + 1) * kRowH};
                if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
                Text(dc, rows[i].key, titleFormat_.Get(), {r.left + 18.f, r.top, r.left + 230.f, r.bottom}, White(0.95f));
                Text(dc, rows[i].value, descFormat_.Get(), {r.left + 230.f, r.top, r.right - 18.f, r.bottom}, White(0.6f));
            }
            y = card.bottom + 22.f;
        };
        // Veyra
        table(L"VEYRA", {{L"Wersja", L"Veyra " + Updater::Current() + (Edition::Minimal() ? L" Minimal" : L" Full")},
                         {L"Wydanie", Edition::Minimal() ? L"Minimal — wygląd Veyry, Eksplorator w tle, programy Windows"
                                                        : L"Full — powłoka zamiast Eksploratora, aplikacje Veyry"},
                         {L"Folder programu", ExeDir()},
                         {L"Dane i ustawienia", L"%LOCALAPPDATA%\\Exploder"},
                         {L"Pamięć powłoki", memoryMb_ < 0 ? L"Powłoka nie działa" : std::to_wstring(memoryMb_) + L" MB RAM"}});
        // Komputer
        if (hardware_.empty()) hardware_ = SystemInfo::Collect();
        std::vector<SystemInfo::Row> hw = hardware_;
        std::wstring mons;
        for (const auto& m : monitors_)
            mons += (mons.empty() ? L"" : L",  ") + m.name + L" " + std::to_wstring(m.width) + L"×" + std::to_wstring(m.height);
        hw.push_back({L"Monitory (" + std::to_wstring(monitors_.size()) + L")", mons});
        table(L"KOMPUTER", hw);
        // Skróty
        std::vector<SystemInfo::Row> keys = {{L"Windows", L"Menu Start Veyry"},
                                             {L"Alt + Tab", L"Przełączanie okien z podglądem"},
                                             {L"Ctrl + Alt + Shift + Q", L"Awaryjny powrót do explorer.exe"}};
        if (!Edition::Minimal()) {
            keys.push_back({L"Windows + R", L"Okienko „Uruchom” (cmd → Terminal, calc → Kalkulator)"});
            keys.push_back({L"Windows + V", L"Historia schowka"});
            keys.push_back({L"Windows + Shift + S", L"Zrzut ekranu: wycinek albo cały ekran"});
            keys.push_back({L"F11", L"Pełny ekran w aplikacjach Veyry"});
            keys.push_back({L"Ctrl + kółko myszy", L"Wielkość czcionki w Terminalu (Ctrl + 0 przywraca)"});
            keys.push_back({L"Klawisz Kalkulator", L"Kalkulator Veyry"});
        } else {
            keys.push_back({L"Windows + R / V / Shift + S", L"Jak w Windows (Eksplorator działa w tle)"});
        }
        table(L"SKRÓTY KLAWISZOWE", keys);
        const D2D1_RECT_F gh{L, y - 4.f, L + 170.f, y + 30.f};
        Button(dc, gh, L"Projekt na GitHubie", HitId(HitKind::GitHub, 0));
        AddHit(gh, HitKind::GitHub, 0);
        return gh.bottom + 20.f;
    }

    // ---------------- Elementy ----------------
    void Label(ID2D1DeviceContext* dc, const wchar_t* s, float x, float y) {
        Text(dc, s, sectionLabelFormat_.Get(), {x + 2.f, y, widthDip_ - kPad, y + 18.f}, White(0.45f));
    }
    D2D1_RECT_F Card(ID2D1DeviceContext* dc, float y, int rows) {
        const D2D1_RECT_F r{kSideW + kPad, y, widthDip_ - kPad, y + rows * kRowH};
        Fill(dc, r, White(0.05f), 12.f);
        brush_->SetColor(White(0.08f));
        dc->DrawRoundedRectangle(D2D1::RoundedRect({r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f}, 12.f, 12.f), brush_.Get(), 1.f);
        return r;
    }
    void Toggle(ID2D1DeviceContext* dc, const D2D1_RECT_F& card, int row, const wchar_t* title, const wchar_t* desc, bool on, int id) {
        const D2D1_RECT_F r{card.left, card.top + row * kRowH, card.right, card.top + (row + 1) * kRowH};
        Text(dc, title, titleFormat_.Get(), {r.left + 18.f, r.top + 12.f, r.right - 90.f, r.top + 32.f}, White(0.95f));
        Text(dc, desc, descFormat_.Get(), {r.left + 18.f, r.top + 32.f, r.right - 90.f, r.top + 52.f}, White(0.5f));
        const D2D1_RECT_F sw{r.right - 64.f, r.top + 22.f, r.right - 20.f, r.top + 44.f};
        const bool hot = hover_ == HitId(HitKind::Toggle, id);
        Fill(dc, sw, on ? kAccent : White(hot ? 0.22f : 0.15f), 11.f);
        const float kx = on ? sw.right - 11.f : sw.left + 11.f;
        brush_->SetColor(on ? kOnAccent : White(0.9f));
        dc->FillEllipse(D2D1::Ellipse({kx, (sw.top + sw.bottom) / 2}, 7.f, 7.f), brush_.Get());
        AddHit({r.left, r.top, r.right, r.bottom}, HitKind::Toggle, id);
    }
    void Button(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const wchar_t* label, int hitId) {
        Fill(dc, r, White(hover_ == hitId ? 0.14f : 0.08f), 8.f);
        Text(dc, label, buttonFormat_.Get(), r, White(0.95f));
    }

    // ---------------- Miniatury tapet (wątek w tle) ----------------
    struct Thumb {
        std::wstring path;
        bool user = false;  // "Twoje tapety"
        ComPtr<IWICBitmapSource> pending;
        ComPtr<ID2D1Bitmap> bitmap;
    };
    void DrawThumb(ID2D1DeviceContext* dc, Thumb& t, const D2D1_RECT_F& r, float radius) {
        if (t.pending) {
            dc->CreateBitmapFromWicBitmap(t.pending.Get(), nullptr, &t.bitmap);
            t.pending.Reset();
        }
        if (!t.bitmap) return;
        // Zaokrąglone rogi: geometria jako maska warstwy
        ComPtr<ID2D1Factory> factory;
        dc->GetFactory(&factory);
        ComPtr<ID2D1RoundedRectangleGeometry> geo;
        factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, radius, radius), &geo);
        dc->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get()), nullptr);
        dc->DrawBitmap(t.bitmap.Get(), &r, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        dc->PopLayer();
    }

    void StartThumbnails() {
        const HWND hwnd = hwnd_;
        const std::vector<std::wstring> userFiles = UserWallpaperPaths();
        thumbThread_ = std::thread([this, hwnd, userFiles] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            std::vector<std::wstring> files;
            auto scan = [&](const std::wstring& dir, int depth, auto& self) -> void {
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE) return;
                do {
                    const std::wstring name = fd.cFileName;
                    if (name == L"." || name == L"..") continue;
                    const std::wstring full = dir + L"\\" + name;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        if (depth > 0) self(full, depth - 1, self);
                        continue;
                    }
                    std::wstring ext = name.substr(name.find_last_of(L'.') + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    if ((ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"bmp") && files.size() < 40) files.push_back(full);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            };
            scan(ExeDir() + L"wallpapers", 0, scan);  // tylko tapety Veyry (na życzenie użytkownika)
            std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {  // domyślna pierwsza
                const bool da = a.find(L"veyra-domyslna") != std::wstring::npos, db = b.find(L"veyra-domyslna") != std::wstring::npos;
                return da != db ? da : _wcsicmp(a.c_str(), b.c_str()) < 0;
            });

            ComPtr<IWICImagingFactory> wic;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
            const size_t userCount = userFiles.size();
            files.insert(files.begin(), userFiles.begin(), userFiles.end());
            for (size_t fi = 0; fi < files.size(); ++fi) {
                const std::wstring& f = files[fi];
                if (stop_ || !wic) break;
                Thumb t;
                t.path = f;
                t.user = fi < userCount;
                t.pending = MakeThumb(wic.Get(), f);
                if (!t.pending) continue;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    thumbs_.push_back(std::move(t));
                }
                PostMessageW(hwnd, WM_THUMBS, 0, 0);
            }
            thumbsDone_ = true;
            PostMessageW(hwnd, WM_THUMBS, 0, 0);
            CoUninitialize();
        });
    }

    // Obraz → miniatura 320×180 (wypełnienie z przycięciem), kopia w pamięci
    static ComPtr<IWICBitmapSource> MakeThumb(IWICImagingFactory* wic, const std::wstring& path) {
        ComPtr<IWICBitmapDecoder> dec;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
            FAILED(dec->GetFrame(0, &frame)))
            return nullptr;
        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (!w || !h) return nullptr;
        const double k = std::max(320.0 / w, 180.0 / h);
        const UINT sw = std::max<UINT>(320, static_cast<UINT>(std::ceil(w * k))), sh = std::max<UINT>(180, static_cast<UINT>(std::ceil(h * k)));
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICBitmapClipper> clip;
        ComPtr<IWICFormatConverter> conv;
        const WICRect crop{static_cast<INT>((sw - 320) / 2), static_cast<INT>((sh - 180) / 2), 320, 180};
        if (FAILED(wic->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame.Get(), sw, sh, WICBitmapInterpolationModeFant)) ||
            FAILED(wic->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(scaler.Get(), &crop)) || FAILED(wic->CreateFormatConverter(&conv)) ||
            FAILED(conv->Initialize(clip.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
            return nullptr;
        ComPtr<IWICBitmap> bmp;
        if (FAILED(wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &bmp))) return nullptr;
        return bmp;
    }

    // ---------------- Interakcja ----------------
    enum class HitKind { Section, Wallpaper, Toggle, FontMinus, FontPlus, OpenApp, WinLink, GitHub, CheckUpdate, DoUpdate, PickWallpaper, Scale,
                         Monitor, ApplyLayout, ResetLayout, WindowsDisplay, ResMenu, HzMenu, ModeKeep, ModeRevert, Uninstall, MouseScale };
    struct Hit {
        D2D1_RECT_F rect;
        int id;
    };
    static int HitId(HitKind k, int i) { return static_cast<int>(k) * 1000 + i; }
    void AddHit(const D2D1_RECT_F& r, HitKind k, int i) { hits_.push_back({r, HitId(k, i)}); }
    int HitAt(D2D1_POINT_2F p) const {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (Inside(it->rect, p) && (p.x < kSideW || p.y >= titleH_)) return it->id;  // treść schowana pod paskiem tytułu
        return -1;
    }

    void OnMouseMove(D2D1_POINT_2F p) override {
        if (dragIndex_ >= 0 && viewK_ > 0.f) {  // przeciąganie monitora
            auto& m = monitors_[static_cast<size_t>(dragIndex_)];
            m.x = dragOrigX_ + static_cast<int>(std::lround((p.x - dragStart_.x) / viewK_));
            m.y = dragOrigY_ + static_cast<int>(std::lround((p.y - dragStart_.y) / viewK_));
            Redraw();
            return;
        }
        const int h = HitAt(p);
        if (h != hover_) {
            hover_ = h;
            SetCursor(LoadCursorW(nullptr, h >= 0 && h / 1000 != static_cast<int>(HitKind::Section) ? IDC_HAND : IDC_ARROW));
            Redraw();
        }
    }
    void OnMouseLeave() override {
        hover_ = -1;
        Redraw();
    }
    void OnWheel(int delta, D2D1_POINT_2F p) override {
        if (p.x < kSideW) return;
        scroll_ = std::clamp(scroll_ - delta / 120.f * 60.f, 0.f, std::max(0.f, contentH_ - heightDip_));
        Redraw();
    }
    void OnMouseUp(D2D1_POINT_2F) override {
        if (dragIndex_ < 0) return;
        Displays::Snap(monitors_, static_cast<size_t>(dragIndex_));
        dragIndex_ = -1;
        ReleaseCapture();
        layoutChanged_ = true;
        displayStatus_.clear();
        Redraw();
    }

    void OnMouseDown(D2D1_POINT_2F p, bool right) override {
        if (right) return;
        const int h = HitAt(p);
        if (h < 0) return;
        const auto kind = static_cast<HitKind>(h / 1000);
        const int i = h % 1000;
        if (kind != HitKind::Toggle || i != 1) confirmShell_ = false;
        switch (kind) {
        case HitKind::Section:
            section_ = i;
            scroll_ = 0.f;
            if (i == About) {
                memoryMb_ = ShellMemoryMb();
                SetTimer(hwnd_, kInfoTimer, 2000, nullptr);
            } else {
                KillTimer(hwnd_, kInfoTimer);
            }
            if (i == Updates) OpenUpdates();
            if (i == Apps) programs_ = Programs::List();
            break;
        case HitKind::CheckUpdate:
            StartCheck();
            break;
        case HitKind::DoUpdate:
            if (Updater::Start(ExeDir(), latest_, binaryZip_)) {
                updating_ = true;
                updateStatus_ = L"Uruchamianie aktualizacji…";
                SetTimer(hwnd_, kUpdateTimer, 700, nullptr);
            } else {
                updateStatus_ = L"BŁĄD nie udało się uruchomić skryptu aktualizacji";
            }
            break;
        case HitKind::Wallpaper: {
            std::wstring path;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (static_cast<size_t>(i) < thumbs_.size()) path = thumbs_[static_cast<size_t>(i)].path;
            }
            // Pulpit Veyry przeładowuje tapetę po WM_SETTINGCHANGE (SPI_SETDESKWALLPAPER)
            if (!path.empty() && SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, path.data(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)) wallpaper_ = path;
            break;
        }
        case HitKind::Toggle:
            if (i == 0) {
                Settings::SetBool(L"windowDots", !Settings::GetBool(L"windowDots", true));
                Broadcast();
            } else if (i == 3) {
                Settings::SetBool(L"autoUpdateCheck", !Settings::GetBool(L"autoUpdateCheck", true));
                Broadcast();
            } else if (i == 5) {
                Settings::SetBool(L"showStats", !Settings::GetBool(L"showStats", true));
                Broadcast();
            } else if (i == 2) {
                if (SetCalcVeyra(!calcVeyra_)) calcVeyra_ = CalcIsVeyra();
                else SetTimer(hwnd_, kCalcTimer, 1500, nullptr);  // po zgodzie UAC sprawdzamy ponownie
            } else if (i == 4) {
                if (SetTaskMgrVeyra(!taskmgrVeyra_)) taskmgrVeyra_ = TaskMgrIsVeyra();
                else SetTimer(hwnd_, kCalcTimer, 1500, nullptr);
            } else if (i == 1) {
                if (!shellVeyra_ && !confirmShell_) {  // włączenie wymaga drugiego kliknięcia
                    confirmShell_ = true;
                    break;
                }
                confirmShell_ = false;
                if (SetShellVeyra(!shellVeyra_)) {
                    shellVeyra_ = ShellIsVeyra();
                    shellError_.clear();
                } else {
                    shellError_ = L"Nie udało się zapisać ustawienia w rejestrze.";
                }
            } else if (i == 6) {
                Settings::SetBool(L"veyraCursor", !Settings::GetBool(L"veyraCursor", true));
                Broadcast();
            } else if (i == 7) {
                Settings::SetBool(L"shakeToFind", !Settings::GetBool(L"shakeToFind", true));
                Broadcast();
            }
            break;
        case HitKind::FontMinus:
        case HitKind::FontPlus: {
            const int size = std::clamp(_wtoi(Settings::Get(L"terminalFontSize", L"14").c_str()) + (kind == HitKind::FontPlus ? 1 : -1), 8, 32);
            Settings::Set(L"terminalFontSize", std::to_wstring(size));
            Broadcast();
            break;
        }
        case HitKind::MouseScale:
            Settings::Set(L"shakeScale", std::to_wstring(i));
            Broadcast();
            break;
        case HitKind::OpenApp: {
            static const wchar_t* exes[] = {L"exploder-terminal.exe", L"exploder-calc.exe", L"exploder-store.exe", L"exploder-taskmgr.exe"};
            if (i >= 0 && i < static_cast<int>(std::size(exes)))
                ShellExecuteW(nullptr, L"open", (ExeDir() + exes[i]).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case HitKind::WinLink:
            ShellExecuteW(nullptr, L"open", kWinLinks[i].uri, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case HitKind::PickWallpaper:
            PickWallpaper();
            break;
        case HitKind::Scale:
            Settings::Set(L"uiScale", kScales[i] ? std::to_wstring(kScales[i]) : std::wstring(L"auto"));
            Broadcast();
            break;
        case HitKind::Monitor:
            if (monitors_.size() > 1) {
                dragIndex_ = i;
                dragStart_ = p;
                dragOrigX_ = monitors_[static_cast<size_t>(i)].x;
                dragOrigY_ = monitors_[static_cast<size_t>(i)].y;
                SetCapture(hwnd_);
            }
            break;
        case HitKind::ApplyLayout:
            displayStatus_ = Displays::ApplyLayout(monitors_) ? L"Zastosowano nowy układ." : L"Windows nie przyjął tego układu.";
            monitors_ = Displays::List();
            layoutChanged_ = false;
            break;
        case HitKind::ResetLayout:
            monitors_ = Displays::List();
            layoutChanged_ = false;
            displayStatus_.clear();
            break;
        case HitKind::Uninstall:
            if (static_cast<size_t>(i) < programView_.size()) {
                Programs::Uninstall(programs_[programView_[static_cast<size_t>(i)]]);  // odmowa UAC — nic się nie dzieje
            }
            break;
        case HitKind::ResMenu:
        case HitKind::HzMenu:
            if (static_cast<size_t>(i) < monitors_.size()) ChooseMode(static_cast<size_t>(i), kind == HitKind::HzMenu);
            break;
        case HitKind::ModeKeep:
            modeRevert_ = {};
            KillTimer(hwnd_, kModeTimer);
            break;
        case HitKind::ModeRevert:
            RevertMode();
            break;
        case HitKind::WindowsDisplay:
            ShellExecuteW(nullptr, L"open", L"ms-settings:display", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case HitKind::GitHub:
            ShellExecuteW(nullptr, L"open", L"https://github.com/Promileczek/Veyra", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        Redraw();
    }

    // Szukanie programów: litery dopisują, Backspace kasuje, Esc czyści
    void OnChar(wchar_t ch) override {
        if (section_ != Apps) return;
        if (ch == L'\b') {
            if (!programQuery_.empty()) programQuery_.pop_back();
        } else if (ch == 27) {
            programQuery_.clear();
        } else if (ch >= 32) {
            programQuery_ += ch;
            scroll_ = 0.f;
        }
        Redraw();
    }
    // Po powrocie z deinstalatora lista może być inna
    void OnActivate(bool active) override {
        if (active && section_ == Apps) {
            programs_ = Programs::List();
            Redraw();
        }
    }

    void OnTimer(UINT_PTR id) override {
        if (id == kModeTimer) {
            if (--modeSeconds_ <= 0) RevertMode();
            Redraw();
            return;
        }
        if (id == kCalcTimer) {
            KillTimer(hwnd_, kCalcTimer);
            calcVeyra_ = CalcIsVeyra();
            taskmgrVeyra_ = TaskMgrIsVeyra();
            Redraw();
            return;
        }
        if (id == kUpdateTimer) {  // postęp skryptu aktualizacji
            updateStatus_ = Updater::Status();
            if (updateStatus_.rfind(L"OK ", 0) == 0 || updateStatus_.rfind(L"BŁĄD", 0) == 0) {
                updating_ = false;
                KillTimer(hwnd_, kUpdateTimer);
            }
            Redraw();
            return;
        }
        if (id != kInfoTimer) return;
        memoryMb_ = ShellMemoryMb();
        Redraw();
    }

    bool OnMessage(UINT msg, WPARAM, LPARAM, LRESULT& result) override {
        if (msg == WM_CHECKED) {
            std::lock_guard<std::mutex> lock(mutex_);
            checking_ = false;
            checked_ = checkResult_.ok;
            latest_ = checkResult_.latest;
            binaryZip_ = checkResult_.binaryZip;
            checkError_ = checkResult_.ok ? L"" : checkResult_.error;
            if (!checkResult_.changes.empty()) changes_ = checkResult_.changes;
            Redraw();
            result = 0;
            return true;
        }
        if (msg == WM_DISPLAYCHANGE) {  // monitor podłączony / odłączony / nowa rozdzielczość
            if (dragIndex_ < 0 && !layoutChanged_) monitors_ = Displays::List();
            Redraw();
            return false;
        }
        if (msg == WM_THUMBS) {
            Redraw();
            result = 0;
            return true;
        }
        if (msg == Settings::ChangedMessage()) {  // zmiana z innego miejsca (np. menu docka)
            Settings::Reload();
            Redraw();
            result = 0;
            return true;
        }
        return false;
    }

    // Menu z rozdzielczościami albo częstotliwościami monitora; wybór zmienia tryb i włącza odliczanie potwierdzenia
    void ChooseMode(size_t index, bool hzOnly) {
        const Displays::Monitor m = monitors_[index];
        const std::vector<Displays::Mode> modes = Displays::Modes(m.device);
        std::vector<Displays::Mode> items;
        for (const auto& md : modes) {
            if (hzOnly ? (md.width == m.width && md.height == m.height)
                       : std::none_of(items.begin(), items.end(), [&](const Displays::Mode& o) { return o.width == md.width && o.height == md.height; }))
                items.push_back(md);
        }
        if (items.empty()) return;
        HMENU menu = CreatePopupMenu();
        for (size_t k = 0; k < items.size(); ++k) {
            const bool current = hzOnly ? items[k].hz == m.hz : (items[k].width == m.width && items[k].height == m.height);
            const std::wstring label = hzOnly ? std::to_wstring(items[k].hz) + L" Hz"
                                              : std::to_wstring(items[k].width) + L" × " + std::to_wstring(items[k].height);
            AppendMenuW(menu, MF_STRING | (current ? MF_CHECKED : 0), k + 1, label.c_str());
        }
        POINT pt{};
        GetCursorPos(&pt);
        const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        if (!cmd) return;
        Displays::Mode pick = items[cmd - 1];
        if (!hzOnly) {  // nowa rozdzielczość: dotychczasowe odświeżanie, jeśli jest — inaczej najwyższe
            for (const auto& md : modes)
                if (md.width == pick.width && md.height == pick.height && md.hz == m.hz) pick.hz = m.hz;
        }
        if (pick.width == m.width && pick.height == m.height && pick.hz == m.hz) return;
        if (modeRevert_.device.empty()) modeRevert_ = {m.device, m.width, m.height, m.hz};
        if (Displays::SetMode(m.device, pick.width, pick.height, pick.hz)) {
            modeSeconds_ = 15;
            SetTimer(hwnd_, kModeTimer, 1000, nullptr);
            displayStatus_.clear();
        } else {
            modeRevert_ = {};
            displayStatus_ = L"Windows nie przyjął tego trybu.";
        }
        monitors_ = Displays::List();
    }
    void RevertMode() {
        KillTimer(hwnd_, kModeTimer);
        if (!modeRevert_.device.empty()) Displays::SetMode(modeRevert_.device, modeRevert_.width, modeRevert_.height, modeRevert_.hz);
        modeRevert_ = {};
        monitors_ = Displays::List();
        Redraw();
    }

    // Własna tapeta: systemowe okno wyboru, trafia na początek "Twoich tapet" (zostają 3 najnowsze)
    void PickWallpaper() {
        ComPtr<IFileOpenDialog> dialog;
        ComPtr<IShellItem> item;
        PWSTR chosen = nullptr;
        const COMDLG_FILTERSPEC types[] = {{L"Obrazy", L"*.jpg;*.jpeg;*.png;*.bmp"}};
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
        dialog->SetFileTypes(1, types);
        dialog->SetTitle(L"Wybierz tapetę");
        if (FAILED(dialog->Show(hwnd_)) || FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen))) return;
        std::wstring path = chosen;
        CoTaskMemFree(chosen);
        std::wstring list = path;
        for (const std::wstring& p : UserWallpaperPaths())
            if (_wcsicmp(p.c_str(), path.c_str()) != 0) list += L"|" + p;
        Settings::Set(L"customWallpapers", list);
        if (SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, path.data(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)) wallpaper_ = path;
        // Miniatury "Twoich tapet" od nowa (lista wyżej już ma właściwą kolejność i limit)
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        std::vector<Thumb> fresh;
        for (const std::wstring& p : UserWallpaperPaths()) {
            Thumb t;
            t.path = p;
            t.user = true;
            t.pending = wic ? MakeThumb(wic.Get(), p) : nullptr;
            if (t.pending) fresh.push_back(std::move(t));
        }
        std::lock_guard<std::mutex> lock(mutex_);
        std::erase_if(thumbs_, [](const Thumb& t) { return t.user; });
        thumbs_.insert(thumbs_.begin(), std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));
    }

    // Powłoka i terminal przeładowują settings.txt
    static void Broadcast() { PostMessageW(HWND_BROADCAST, Settings::ChangedMessage(), 0, 0); }

    ComPtr<IDWriteTextFormat> headerFormat_, bigCenterFormat_, appFormat_, sideFormat_, titleFormat_, descFormat_, buttonFormat_, glyphFormat_,
        glyphBigFormat_, sectionLabelFormat_, wrapFormat_;
    std::vector<Hit> hits_;
    int section_ = Personalize, hover_ = -1;
    float scroll_ = 0.f, contentH_ = 0.f;
    std::wstring wallpaper_, shellError_;
    // Ekran
    std::vector<Displays::Monitor> monitors_;
    int dragIndex_ = -1, dragOrigX_ = 0, dragOrigY_ = 0;
    D2D1_POINT_2F dragStart_{};
    float viewK_ = 0.f, viewX_ = 0.f, viewY_ = 0.f;  // piksele monitorów → punkty widoku układu
    bool layoutChanged_ = false;
    std::wstring displayStatus_;
    // Programy
    std::vector<Programs::Program> programs_;
    std::vector<size_t> programView_;
    std::vector<SystemInfo::Row> hardware_;  // zbierane raz, przy pierwszym otwarciu Informacji
    std::wstring programQuery_;
    struct ModeRevert {
        std::wstring device;
        int width = 0, height = 0, hz = 0;
    } modeRevert_;
    int modeSeconds_ = 0;
    bool shellVeyra_ = false, confirmShell_ = false, calcVeyra_ = false, taskmgrVeyra_ = false;
    int memoryMb_ = -1;
    // Aktualizacje
    std::thread checkThread_;
    Updater::CheckResult checkResult_;
    bool checking_ = false, checked_ = false, updating_ = false, tools_ = false;
    std::wstring latest_, checkError_, updateStatus_, binaryZip_;
    std::vector<std::wstring> changes_;

    std::mutex mutex_;
    std::vector<Thumb> thumbs_;
    std::thread thumbThread_;
    std::atomic<bool> stop_{false}, thumbsDone_{false};
};
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Graphics graphics;
    if (!graphics.Init()) return 1;
    auto app = std::make_unique<SettingsApp>();
    if (!app->Init(inst, graphics)) return 1;
    const int code = AppWindow::RunMessageLoop();
    app.reset();
    return code;
}
