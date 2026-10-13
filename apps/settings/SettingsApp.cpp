// Ustawienia Veyry — styl jak reszta powłoki (grafit, akcent #61B3FF, płaskie karty).
// Sekcje: Personalizacja (tapeta), Pulpit i okna (kropki macOS), Aplikacje (czcionka terminala),
// System (Veyra zamiast Eksploratora po zalogowaniu, skróty do ustawień Windows), Informacje.
// Zmiany zapisuje do settings.txt i rozsyła "VeyraSettingsChanged" — powłoka i terminal stosują je od razu.
#include "../../src/core/AppWindow.h"
#include "../../src/core/Settings.h"
#include "Updater.h"
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>
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
constexpr UINT WM_CHECKED = WM_APP + 31;  // sprawdzanie wersji zakończone
constexpr float kSideW = 210.f, kPad = 28.f, kRowH = 64.f;
const D2D1_COLOR_F kBody = D2D1::ColorF(0x1C1C21), kSide = D2D1::ColorF(0x17171B), kAccent = D2D1::ColorF(0.38f, 0.70f, 1.f),
                   kOnAccent = D2D1::ColorF(0x0B1A26);
D2D1_COLOR_F White(float a) { return D2D1::ColorF(1.f, 1.f, 1.f, a); }

enum Section { Personalize, Desktop, Apps, System, Updates, About, SectionCount };
const wchar_t* const kSectionNames[] = {L"Personalizacja", L"Pulpit i okna", L"Aplikacje", L"System", L"Aktualizacje", L"Informacje"};
const wchar_t kSectionGlyphs[] = {L'\xE771', L'\xE7F4', L'\xE71D', L'\xE770', L'\xE895', L'\xE946'};

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

// Kalkulator Veyry zamiast systemowego: Image File Execution Options\calc.exe\Debugger → exploder-calc.exe.
// Każde uruchomienie calc.exe (Win+R, skróty, skrypty) otwiera Kalkulator Veyry; usunięcie wpisu przywraca systemowy.
// HKLM wymaga administratora — bez uprawnień zapis przez reg.exe z prośbą UAC.
constexpr wchar_t kIfeoCalc[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\calc.exe";

bool CalcIsVeyra() {
    wchar_t value[MAX_PATH * 2]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kIfeoCalc, L"Debugger", RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return false;
    std::wstring v = value;
    std::transform(v.begin(), v.end(), v.begin(), ::towlower);
    return v.find(L"exploder-calc.exe") != std::wstring::npos;
}
// true = zapisane od razu; false = uruchomiono reg.exe z prośbą UAC (wynik sprawdzamy później)
bool SetCalcVeyra(bool on) {
    const std::wstring debugger = L"\"" + ExeDir() + L"exploder-calc.exe\"";
    HKEY key = nullptr;
    LSTATUS st = on ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, kIfeoCalc, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
                    : RegOpenKeyExW(HKEY_LOCAL_MACHINE, kIfeoCalc, 0, KEY_SET_VALUE, &key);
    if (st == ERROR_SUCCESS) {
        st = on ? RegSetValueExW(key, L"Debugger", 0, REG_SZ, reinterpret_cast<const BYTE*>(debugger.c_str()),
                                 static_cast<DWORD>((debugger.size() + 1) * sizeof(wchar_t)))
                : RegDeleteValueW(key, L"Debugger");
        RegCloseKey(key);
        if (st == ERROR_SUCCESS || (!on && st == ERROR_FILE_NOT_FOUND)) return true;
    } else if (!on && st == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    // Brak uprawnień: reg.exe jako administrator
    const std::wstring regKey = L"\"HKLM\\" + std::wstring(kIfeoCalc) + L"\"";
    const std::wstring args = on ? L"add " + regKey + L" /v Debugger /t REG_SZ /d \"\\\"" + ExeDir() + L"exploder-calc.exe\\\"\" /f"
                                 : L"delete " + regKey + L" /v Debugger /f";
    ShellExecuteW(nullptr, L"runas", L"reg.exe", args.c_str(), nullptr, SW_HIDE);
    return false;
}

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
        appFormat_ = MakeFormat(L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        sideFormat_ = MakeFormat(L"Segoe UI", 13.f);
        titleFormat_ = MakeFormat(L"Segoe UI", 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        descFormat_ = MakeFormat(L"Segoe UI", 11.5f);
        buttonFormat_ = MakeFormat(L"Segoe UI", 12.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        glyphFormat_ = MakeFormat(L"Segoe MDL2 Assets", 15.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        sectionLabelFormat_ = MakeFormat(L"Segoe UI", 11.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        wrapFormat_ = MakeFormat(L"Segoe UI", 11.5f);  // opis w kilku liniach
        if (wrapFormat_) {
            wrapFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            wrapFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        wallpaper_ = CurrentWallpaper();
        shellVeyra_ = ShellIsVeyra();
        calcVeyra_ = CalcIsVeyra();
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
        for (int i = 0; i < SectionCount; ++i) {
            const D2D1_RECT_F r{10.f, 82.f + i * 38.f, kSideW - 10.f, 82.f + i * 38.f + 34.f};
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

        // Zawartość (przewijana)
        const D2D1_RECT_F content{kSideW, 0.f, W, H};
        dc->PushAxisAlignedClip(content, D2D1_ANTIALIAS_MODE_ALIASED);
        float y = 44.f - scroll_;
        Text(dc, kSectionNames[section_], headerFormat_.Get(), {kSideW + kPad, y, W - kPad, y + 36.f}, White(0.97f));
        y += 56.f;
        switch (section_) {
        case Personalize: y = PaintPersonalize(dc, y); break;
        case Desktop: y = PaintDesktop(dc, y); break;
        case Apps: y = PaintApps(dc, y); break;
        case System: y = PaintSystem(dc, y); break;
        case Updates: y = PaintUpdates(dc, y); break;
        case About: y = PaintAbout(dc, y); break;
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

            Label(dc, L"TAPETY VEYRY", L, y);
            y += 24.f;
            if (thumbs_.empty()) {
                Text(dc, thumbsDone_ ? L"Nie znaleziono obrazów" : L"Wczytywanie…", descFormat_.Get(), {L, y, R, y + 20.f}, White(0.5f));
                return y + 30.f;
            }
            const float gap = 12.f;
            const int cols = std::max(2, static_cast<int>((R - L + gap) / (170.f + gap)));
            const float tw = (R - L - (cols - 1) * gap) / cols, th = tw * 9.f / 16.f;
            for (size_t i = 0; i < thumbs_.size(); ++i) {
                const float x = L + (i % cols) * (tw + gap), ty = y + (i / cols) * (th + gap);
                const D2D1_RECT_F r{x, ty, x + tw, ty + th};
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
            y += ((thumbs_.size() + cols - 1) / cols) * (th + gap);
        }
        return y;
    }

    float PaintDesktop(ID2D1DeviceContext* dc, float y) {
        Label(dc, L"OKNA", kSideW + kPad, y);
        y += 24.f;
        const D2D1_RECT_F card = Card(dc, y, 1);
        Toggle(dc, card, 0, L"Kropki macOS w oknach programów", L"Czerwona, żółta i zielona kropka zamiast przycisków zamknij / minimalizuj / maksymalizuj",
               Settings::GetBool(L"windowDots", true), 0);
        y = card.bottom + 22.f;
        Label(dc, L"WSKAZÓWKA", kSideW + kPad, y);
        y += 24.f;
        Text(dc, L"Panel aplikacji na pulpicie: kliknij „+”, aby przypiąć program. PPM na kafelku → Usuń z panelu.", descFormat_.Get(),
             {kSideW + kPad, y, widthDip_ - kPad, y + 20.f}, White(0.55f));
        return y + 30.f;
    }

    float PaintApps(ID2D1DeviceContext* dc, float y) {
        Label(dc, L"TERMINAL", kSideW + kPad, y);
        y += 24.f;
        const D2D1_RECT_F card = Card(dc, y, 1);
        const D2D1_RECT_F row{card.left, card.top, card.right, card.top + kRowH};
        Text(dc, L"Rozmiar czcionki", titleFormat_.Get(), {row.left + 18.f, row.top + 12.f, row.right - 300.f, row.top + 32.f}, White(0.95f));
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
        y = card.bottom + 22.f;
        Label(dc, L"APLIKACJE VEYRY", kSideW + kPad, y);
        y += 24.f;
        const wchar_t* apps[][3] = {{L"Terminal", L"Konsola w stylu Linuksa", L"exploder-terminal.exe"},
                                    {L"Kalkulator", L"Z historią obliczeń", L"exploder-calc.exe"},
                                    {L"Sklep", L"Programy instalowane jednym kliknięciem", L"exploder-store.exe"}};
        const D2D1_RECT_F list = Card(dc, y, 3);
        for (int i = 0; i < 3; ++i) {
            const D2D1_RECT_F r{list.left, list.top + i * kRowH, list.right, list.top + (i + 1) * kRowH};
            if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
            Text(dc, apps[i][0], titleFormat_.Get(), {r.left + 18.f, r.top + 12.f, r.right - 140.f, r.top + 32.f}, White(0.95f));
            Text(dc, apps[i][1], descFormat_.Get(), {r.left + 18.f, r.top + 32.f, r.right - 140.f, r.top + 52.f}, White(0.5f));
            const D2D1_RECT_F b{r.right - 110.f, r.top + 16.f, r.right - 18.f, r.top + 48.f};
            Button(dc, b, L"Otwórz", HitId(HitKind::OpenApp, i));
            AddHit(b, HitKind::OpenApp, i);
        }
        return list.bottom + 20.f;
    }

    float PaintSystem(ID2D1DeviceContext* dc, float y) {
        Label(dc, L"POWŁOKA", kSideW + kPad, y);
        y += 24.f;
        const D2D1_RECT_F card = Card(dc, y, 2);
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
        const float L = kSideW + kPad, R = widthDip_ - kPad;
        const D2D1_RECT_F card = Card(dc, y, 3);
        auto row = [&](int i, const std::wstring& k, const std::wstring& v) {
            const D2D1_RECT_F r{card.left, card.top + i * kRowH, card.right, card.top + (i + 1) * kRowH};
            if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
            Text(dc, k, titleFormat_.Get(), {r.left + 18.f, r.top, r.left + 260.f, r.bottom}, White(0.95f));
            Text(dc, v, descFormat_.Get(), {r.left + 260.f, r.top, r.right - 18.f, r.bottom}, White(0.6f));
        };
        row(0, L"Veyra " + Updater::Current(), L"Powłoka Windows zamiast explorer.exe");
        row(1, L"Pamięć powłoki", memoryMb_ < 0 ? L"Powłoka nie działa" : std::to_wstring(memoryMb_) + L" MB RAM");
        row(2, L"Dane i ustawienia", L"%LOCALAPPDATA%\\Exploder");
        y = card.bottom + 22.f;
        Label(dc, L"SKRÓTY KLAWISZOWE", L, y);
        y += 24.f;
        const wchar_t* keys[][2] = {{L"Windows", L"Menu Start"},
                                    {L"Alt + Tab", L"Przełączanie okien z podglądem"},
                                    {L"Windows + Shift + S", L"Zrzut ekranu: wycinek albo cały ekran"},
                                    {L"Ctrl + Alt + Shift + Q", L"Awaryjny powrót do explorer.exe"}};
        const D2D1_RECT_F kc = Card(dc, y, 4);
        for (int i = 0; i < 4; ++i) {
            const D2D1_RECT_F r{kc.left, kc.top + i * kRowH, kc.right, kc.top + (i + 1) * kRowH};
            if (i) Fill(dc, {r.left + 18.f, r.top, r.right - 18.f, r.top + 1.f}, White(0.06f));
            Text(dc, keys[i][0], titleFormat_.Get(), {r.left + 18.f, r.top, r.left + 260.f, r.bottom}, White(0.95f));
            Text(dc, keys[i][1], descFormat_.Get(), {r.left + 260.f, r.top, r.right - 18.f, r.bottom}, White(0.6f));
        }
        y = kc.bottom + 18.f;
        const D2D1_RECT_F gh{L, y, L + 170.f, y + 34.f};
        Button(dc, gh, L"Projekt na GitHubie", HitId(HitKind::GitHub, 0));
        AddHit(gh, HitKind::GitHub, 0);
        (void)R;
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
        thumbThread_ = std::thread([this, hwnd] {
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
            for (const std::wstring& f : files) {
                if (stop_ || !wic) break;
                Thumb t;
                t.path = f;
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
    enum class HitKind { Section, Wallpaper, Toggle, FontMinus, FontPlus, OpenApp, WinLink, GitHub, CheckUpdate, DoUpdate };
    struct Hit {
        D2D1_RECT_F rect;
        int id;
    };
    static int HitId(HitKind k, int i) { return static_cast<int>(k) * 1000 + i; }
    void AddHit(const D2D1_RECT_F& r, HitKind k, int i) { hits_.push_back({r, HitId(k, i)}); }
    int HitAt(D2D1_POINT_2F p) const {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (Inside(it->rect, p) && (p.x < kSideW || p.y >= 0.f)) return it->id;
        return -1;
    }

    void OnMouseMove(D2D1_POINT_2F p) override {
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
            } else if (i == 2) {
                if (SetCalcVeyra(!calcVeyra_)) calcVeyra_ = CalcIsVeyra();
                else SetTimer(hwnd_, kCalcTimer, 1500, nullptr);  // po zgodzie UAC sprawdzamy ponownie
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
            }
            break;
        case HitKind::FontMinus:
        case HitKind::FontPlus: {
            const int size = std::clamp(_wtoi(Settings::Get(L"terminalFontSize", L"14").c_str()) + (kind == HitKind::FontPlus ? 1 : -1), 8, 32);
            Settings::Set(L"terminalFontSize", std::to_wstring(size));
            Broadcast();
            break;
        }
        case HitKind::OpenApp: {
            static const wchar_t* exes[] = {L"exploder-terminal.exe", L"exploder-calc.exe", L"exploder-store.exe"};
            ShellExecuteW(nullptr, L"open", (ExeDir() + exes[i]).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case HitKind::WinLink:
            ShellExecuteW(nullptr, L"open", kWinLinks[i].uri, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case HitKind::GitHub:
            ShellExecuteW(nullptr, L"open", L"https://github.com/Promileczek/Veyra", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        Redraw();
    }

    void OnTimer(UINT_PTR id) override {
        if (id == kCalcTimer) {
            KillTimer(hwnd_, kCalcTimer);
            calcVeyra_ = CalcIsVeyra();
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

    // Powłoka i terminal przeładowują settings.txt
    static void Broadcast() { PostMessageW(HWND_BROADCAST, Settings::ChangedMessage(), 0, 0); }

    ComPtr<IDWriteTextFormat> headerFormat_, appFormat_, sideFormat_, titleFormat_, descFormat_, buttonFormat_, glyphFormat_, sectionLabelFormat_,
        wrapFormat_;
    std::vector<Hit> hits_;
    int section_ = Personalize, hover_ = -1;
    float scroll_ = 0.f, contentH_ = 0.f;
    std::wstring wallpaper_, shellError_;
    bool shellVeyra_ = false, confirmShell_ = false, calcVeyra_ = false;
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
