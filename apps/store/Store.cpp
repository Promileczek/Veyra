// Sklep Explodera — katalog popularnych programów instalowanych jednym kliknięciem przez winget (Menedżer pakietów Windows).
// Zainstalowane: "winget export" (lista identyfikatorów) w tle przy starcie. Instalacje po kolei, z postępem z wyjścia winget.
#include "../../src/core/AppWindow.h"
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr UINT WM_INSTALLED_LIST = WM_APP + 20;  // lista zainstalowanych gotowa
constexpr UINT WM_INSTALL_PROGRESS = WM_APP + 21;  // wParam: indeks, lParam: procent
constexpr UINT WM_INSTALL_DONE = WM_APP + 22;      // wParam: indeks, lParam: kod wyjścia winget
constexpr UINT_PTR kSpinTimer = 1;

constexpr float kSideW = 190.f, kSearchH = 38.f, kCardW = 250.f, kCardH = 124.f, kGap = 14.f, kPad = 20.f;

struct App {
    const wchar_t* name;
    const wchar_t* id;  // identyfikator winget
    int category;
    const wchar_t* desc;
    uint32_t color;              // kafelek z inicjałami
    const wchar_t* alt = nullptr;  // drugi identyfikator (np. ten sam program ze sklepu Microsoft)
};

const wchar_t* const kCategories[] = {L"Wszystkie", L"Przeglądarki", L"Komunikatory", L"Gry", L"Multimedia", L"Narzędzia", L"Programowanie", L"Biuro i grafika"};
const wchar_t* const kCategoryGlyphs[] = {L"\xE71D", L"\xE774", L"\xE8BD", L"\xE7FC", L"\xE8D6", L"\xE90F", L"\xE943", L"\xE790"};

const App kApps[] = {
    {L"Brave", L"Brave.Brave", 1, L"Szybka przeglądarka z blokadą reklam", 0xFB542B},
    {L"Google Chrome", L"Google.Chrome", 1, L"Przeglądarka Google", 0x4285F4},
    {L"Firefox", L"Mozilla.Firefox", 1, L"Otwarta przeglądarka Mozilli", 0xFF7139},
    {L"Opera GX", L"Opera.OperaGX", 1, L"Przeglądarka dla graczy", 0xFA1E4E},
    {L"Discord", L"Discord.Discord", 2, L"Czat głosowy i tekstowy", 0x5865F2, L"XPDC2RH70K22MN"},
    {L"Telegram", L"Telegram.TelegramDesktop", 2, L"Komunikator w chmurze", 0x26A5E4},
    {L"Signal", L"OpenWhisperSystems.Signal", 2, L"Szyfrowany komunikator", 0x3A76F0},
    {L"Zoom", L"Zoom.Zoom", 2, L"Spotkania wideo", 0x2D8CFF},
    {L"Thunderbird", L"Mozilla.Thunderbird", 2, L"Poczta e-mail", 0x0A84FF},
    {L"Steam", L"Valve.Steam", 3, L"Sklep i biblioteka gier", 0x1B2838},
    {L"Epic Games", L"EpicGames.EpicGamesLauncher", 3, L"Launcher Epic Games", 0x313131},
    {L"Battle.net", L"Blizzard.BattleNet", 3, L"Gry Blizzarda", 0x148EFF},
    {L"EA app", L"ElectronicArts.EADesktop", 3, L"Gry Electronic Arts", 0xFF4747},
    {L"Ubisoft Connect", L"Ubisoft.Connect", 3, L"Gry Ubisoftu", 0x0070FF},
    {L"Spotify", L"Spotify.Spotify", 4, L"Muzyka i podcasty", 0x1DB954},
    {L"VLC", L"VideoLAN.VLC", 4, L"Odtwarzacz wszystkiego", 0xFF8800},
    {L"OBS Studio", L"OBSProject.OBSStudio", 4, L"Nagrywanie i streaming", 0x302E31},
    {L"Audacity", L"Audacity.Audacity", 4, L"Edycja dźwięku", 0x0000CC},
    {L"ShareX", L"ShareX.ShareX", 4, L"Zrzuty i nagrania ekranu", 0x2E9BF0},
    {L"7-Zip", L"7zip.7zip", 5, L"Archiwa ZIP, 7z, RAR", 0x2C2C2C},
    {L"WinRAR", L"RARLab.WinRAR", 5, L"Archiwa RAR i ZIP", 0x7A3E9D},
    {L"Everything", L"voidtools.Everything", 5, L"Błyskawiczne szukanie plików", 0xF39C12},
    {L"PowerToys", L"Microsoft.PowerToys", 5, L"Dodatki do Windows", 0x6B69D6},
    {L"qBittorrent", L"qBittorrent.qBittorrent", 5, L"Klient torrentów", 0x2F67BA},
    {L"AnyDesk", L"AnyDesk.AnyDesk", 5, L"Zdalny pulpit", 0xEF443B},
    {L"TeamViewer", L"TeamViewer.TeamViewer", 5, L"Zdalna pomoc", 0x0E8EE9},
    {L"HWiNFO", L"REALiX.HWiNFO", 5, L"Informacje o sprzęcie", 0x2D6DB5},
    {L"CPU-Z", L"CPUID.CPU-Z", 5, L"Procesor, płyta, RAM", 0x3B4E8C},
    {L"MSI Afterburner", L"Guru3D.Afterburner", 5, L"Podkręcanie i monitoring GPU", 0xC4161C},
    {L"Rufus", L"Rufus.Rufus", 5, L"Bootowalny pendrive", 0x4A4A4A},
    {L"Bitwarden", L"Bitwarden.Bitwarden", 5, L"Menedżer haseł", 0x175DDC},
    {L"Visual Studio Code", L"Microsoft.VisualStudioCode", 6, L"Edytor kodu", 0x007ACC},
    {L"Notepad++", L"Notepad++.Notepad++", 6, L"Lekki edytor tekstu", 0x90E59A},
    {L"Git", L"Git.Git", 6, L"System kontroli wersji", 0xF05032},
    {L"Python 3.12", L"Python.Python.3.12", 6, L"Język programowania", 0x3776AB},
    {L"Node.js LTS", L"OpenJS.NodeJS.LTS", 6, L"JavaScript poza przeglądarką", 0x5FA04E},
    {L"Windows Terminal", L"Microsoft.WindowsTerminal", 6, L"Terminal Microsoftu", 0x2D2D2D},
    {L"PuTTY", L"PuTTY.PuTTY", 6, L"SSH i telnet", 0x1F4E79},
    {L"WinSCP", L"WinSCP.WinSCP", 6, L"Pliki przez SFTP/FTP", 0x2E7D32},
    {L"LibreOffice", L"TheDocumentFoundation.LibreOffice", 7, L"Pakiet biurowy", 0x18A303},
    {L"GIMP", L"GIMP.GIMP", 7, L"Edycja zdjęć", 0x5C5543},
    {L"Krita", L"KDE.Krita", 7, L"Malowanie cyfrowe", 0x3BABFF},
    {L"Inkscape", L"Inkscape.Inkscape", 7, L"Grafika wektorowa", 0x000000},
    {L"Paint.NET", L"dotPDN.PaintDotNet", 7, L"Prosty edytor grafiki", 0x2B579A},
    {L"Blender", L"BlenderFoundation.Blender", 7, L"Grafika 3D", 0xEA7600},
};

enum class State { Unknown, NotInstalled, Queued, Installing, Installed, Failed };

D2D1_COLOR_F Rgb(uint32_t c, float a = 1.f) { return {((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, a}; }

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

// Nazwa zasobu z ikonką (res/store/*.png wbudowane w exe, patrz tools/fetch_store_icons.py):
// identyfikator winget z samymi literami i cyframi, reszta → _
std::wstring ResourceName(const wchar_t* id) {
    std::wstring n = std::wstring(L"I_") + id;  // przedrostek: nazwa zasobu nie może zaczynać się cyfrą (7zip)
    for (wchar_t& c : n)
        if (!iswalnum(c)) c = L'_';
    return n;
}

std::wstring Initials(const wchar_t* name) {
    std::wstring s;
    bool start = true;
    for (const wchar_t* p = name; *p && s.size() < 2; ++p) {
        if (start && iswalnum(*p)) s += static_cast<wchar_t>(std::towupper(*p));
        start = *p == L' ' || *p == L'-' || *p == L'.';
    }
    return s;
}

// Uruchamia winget bez okna; onOutput dostaje kolejne kawałki wyjścia. Zwraca kod wyjścia (albo -1).
int RunWinget(const std::wstring& args, const std::function<void(const std::string&)>& onOutput) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0)) return -1;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = write;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"winget.exe " + args;
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write);
    if (!ok) {
        CloseHandle(read);
        return -1;
    }
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(read, buf, sizeof(buf), &got, nullptr) && got)
        if (onOutput) onOutput(std::string(buf, got));
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(read);
    return static_cast<int>(code);
}

class Store : public AppWindow {
public:
    bool Init(HINSTANCE inst, Graphics& g) {
        Options o;
        o.className = L"ExploderStore";
        o.title = L"Sklep";
        o.width = 1060.f;
        o.height = 680.f;
        o.minWidth = 640.f;
        o.minHeight = 420.f;
        titleH_ = 52.f;
        showTitle_ = false;
        states_.assign(std::size(kApps), State::Unknown);
        progress_.assign(std::size(kApps), 0);
        codes_.assign(std::size(kApps), 0);
        if (!Create(inst, g, o)) return false;
        titleFormat_ = MakeFormat(L"Segoe UI", 20.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        nameFormat_ = MakeFormat(L"Segoe UI", 14.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        textFormat_ = MakeFormat(L"Segoe UI", 12.f);
        smallFormat_ = MakeFormat(L"Segoe UI", 11.f);
        buttonFormat_ = MakeFormat(L"Segoe UI", 12.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        initialsFormat_ = MakeFormat(L"Segoe UI", 18.f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        glyphFormat_ = MakeFormat(L"Segoe MDL2 Assets", 15.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        sideFormat_ = MakeFormat(L"Segoe UI", 13.f);
        Filter();
        // Lista zainstalowanych w tle (winget export trwa kilka sekund)
        const HWND hwnd = hwnd_;
        listThread_ = std::thread([this, hwnd] {
            wchar_t tmp[MAX_PATH]{};
            GetTempPathW(MAX_PATH, tmp);
            const std::wstring file = std::wstring(tmp) + L"exploder-winget-export.json";
            DeleteFileW(file.c_str());
            const int code = RunWinget(L"export -o \"" + file + L"\" --accept-source-agreements --disable-interactivity", nullptr);
            std::string json;
            // "winget list" widzi też programy, których export pomija (np. Brave zainstalowany dla użytkownika)
            RunWinget(L"list --accept-source-agreements --disable-interactivity", [&json](const std::string& chunk) { json += chunk; });
            json += '\n';
            if (FILE* f = nullptr; !_wfopen_s(&f, file.c_str(), L"rb") && f) {
                char buf[8192];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, n);
                fclose(f);
                DeleteFileW(file.c_str());
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                exportJson_ = std::move(json);
                wingetMissing_ = code == -1;
            }
            if (!closing_) PostMessageW(hwnd, WM_INSTALLED_LIST, 0, 0);
        });
        // Ikonki z zasobów exe (bez internetu); brak ikonki → kafelek z inicjałami
        pendingIcons_.resize(std::size(kApps));
        icons_.resize(std::size(kApps));
        for (size_t i = 0; i < std::size(kApps); ++i) pendingIcons_[i] = LoadIconResource(ResourceName(kApps[i].id));
        SetTimer(hwnd_, kSpinTimer, 50, nullptr);
        Redraw();
        return true;
    }

    ~Store() override {
        closing_ = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.clear();
        }
        if (listThread_.joinable()) listThread_.detach();  // winget export może trwać — nie blokujemy zamknięcia
        if (worker_.joinable()) worker_.detach();
    }

protected:
    void Paint(ID2D1DeviceContext* dc) override {
        dc->Clear(Rgb(0x1C1C1F));
        const float W = widthDip_, H = heightDip_;
        // Pasek boczny: kategorie
        Fill(dc, {0.f, 0.f, kSideW, H}, Rgb(0x26262A));
        Text(dc, L"Sklep", titleFormat_.Get(), {20.f, 40.f, kSideW, 76.f}, Rgb(0xFFFFFF));
        for (int i = 0; i < static_cast<int>(std::size(kCategories)); ++i) {
            const D2D1_RECT_F r = CategoryRect(i);
            if (i == category_) Fill(dc, r, Rgb(0xFFFFFF, 0.12f), 7.f);
            else if (hover_ == -100 - i) Fill(dc, r, Rgb(0xFFFFFF, 0.06f), 7.f);
            Text(dc, kCategoryGlyphs[i], glyphFormat_.Get(), {r.left + 4.f, r.top, r.left + 34.f, r.bottom}, Rgb(0xFFFFFF, 0.85f));
            Text(dc, kCategories[i], sideFormat_.Get(), {r.left + 40.f, r.top, r.right - 6.f, r.bottom}, Rgb(0xFFFFFF, 0.92f));
        }
        Text(dc, L"Instalacja przez winget", smallFormat_.Get(), {20.f, H - 34.f, kSideW - 10.f, H - 14.f}, Rgb(0xFFFFFF, 0.4f));

        // Wyszukiwarka
        const D2D1_RECT_F search = SearchRect();
        Fill(dc, search, Rgb(0xFFFFFF, searchFocus_ ? 0.12f : 0.07f), 9.f);
        Text(dc, L"\xE721", glyphFormat_.Get(), {search.left + 4.f, search.top, search.left + 34.f, search.bottom}, Rgb(0xFFFFFF, 0.6f));
        Text(dc, query_.empty() ? L"Szukaj programów…" : query_ + (searchFocus_ ? L"|" : L""), textFormat_.Get(),
             {search.left + 36.f, search.top, search.right - 10.f, search.bottom}, Rgb(0xFFFFFF, query_.empty() ? 0.45f : 0.95f));

        if (wingetMissingShown_) {
            Text(dc, L"Brak menedżera pakietów winget. Zainstaluj „Instalator aplikacji” (App Installer), żeby pobierać programy.",
                 textFormat_.Get(), {kSideW + kPad, search.bottom + 14.f, W - kPad, search.bottom + 40.f}, Rgb(0xFF8A80));
        } else if (!listLoaded_) {
            Text(dc, L"Sprawdzam zainstalowane programy…", smallFormat_.Get(), {kSideW + kPad, search.bottom + 6.f, W - kPad, search.bottom + 26.f},
                 Rgb(0xFFFFFF, 0.45f));
        }

        // Karty
        dc->PushAxisAlignedClip({kSideW, GridTop() - 4.f, W, H}, D2D1_ANTIALIAS_MODE_ALIASED);
        for (size_t v = 0; v < view_.size(); ++v) {
            const D2D1_RECT_F r = CardRect(static_cast<int>(v));
            if (r.bottom < GridTop() - 4.f || r.top > H) continue;
            DrawCard(dc, static_cast<int>(v), r);
        }
        dc->PopAxisAlignedClip();
        if (view_.empty()) Text(dc, L"Nic nie znaleziono", textFormat_.Get(), {kSideW + kPad, GridTop(), W - kPad, GridTop() + 30.f}, Rgb(0xFFFFFF, 0.5f));
    }

    void DrawCard(ID2D1DeviceContext* dc, int v, const D2D1_RECT_F& r) {
        const size_t i = view_[static_cast<size_t>(v)];
        const App& a = kApps[i];
        Fill(dc, r, Rgb(0xFFFFFF, hover_ == v ? 0.09f : 0.055f), 12.f);
        const D2D1_RECT_F tile{r.left + 14.f, r.top + 14.f, r.left + 62.f, r.top + 62.f};
        if (pendingIcons_[i]) {  // tylko Direct2D w trakcie rysowania
            dc->CreateBitmapFromWicBitmap(pendingIcons_[i].Get(), nullptr, &icons_[i]);
            pendingIcons_[i].Reset();
        }
        if (icons_[i]) {  // czarne tło pod logo (ikonki przygotowane przez tools/fetch_store_icons.py)
            Fill(dc, tile, Rgb(0x000000), 12.f);
            brush_->SetColor(Rgb(0xFFFFFF, 0.08f));
            dc->DrawRoundedRectangle(D2D1::RoundedRect({tile.left + 0.5f, tile.top + 0.5f, tile.right - 0.5f, tile.bottom - 0.5f}, 12.f, 12.f),
                                     brush_.Get(), 1.f);
            const D2D1_RECT_F img{tile.left + 6.f, tile.top + 6.f, tile.right - 6.f, tile.bottom - 6.f};
            dc->DrawBitmap(icons_[i].Get(), &img, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        } else {
            Fill(dc, tile, Rgb(a.color), 12.f);
            Text(dc, Initials(a.name), initialsFormat_.Get(), tile, Rgb(0xFFFFFF));
        }
        Text(dc, a.name, nameFormat_.Get(), {tile.right + 12.f, r.top + 14.f, r.right - 12.f, r.top + 36.f}, Rgb(0xFFFFFF));
        Text(dc, a.desc, smallFormat_.Get(), {tile.right + 12.f, r.top + 36.f, r.right - 12.f, r.top + 58.f}, Rgb(0xFFFFFF, 0.55f));

        const D2D1_RECT_F b = ButtonRect(r);
        const State s = states_[i];
        switch (s) {
        case State::Installed:
            Text(dc, L"\xE73E", glyphFormat_.Get(), {r.left + 10.f, b.top, r.left + 34.f, b.bottom}, Rgb(0x5BD27A));
            Text(dc, L"Zainstalowano", textFormat_.Get(), {r.left + 36.f, b.top, r.right - 14.f, b.bottom}, Rgb(0x5BD27A));
            break;
        case State::Queued:
            Fill(dc, b, Rgb(0xFFFFFF, 0.08f), 8.f);
            Text(dc, L"W kolejce…", buttonFormat_.Get(), b, Rgb(0xFFFFFF, 0.7f));
            break;
        case State::Installing: {
            const D2D1_RECT_F bar{r.left + 14.f, b.top + 12.f, r.right - 14.f, b.top + 17.f};
            Fill(dc, bar, Rgb(0xFFFFFF, 0.12f), 2.5f);
            if (progress_[i] > 0) {
                Fill(dc, {bar.left, bar.top, bar.left + (bar.right - bar.left) * progress_[i] / 100.f, bar.bottom}, Rgb(0x3D8BFF), 2.5f);
            } else {  // brak procentów (np. instalator) — przesuwający się odcinek
                const float w = (bar.right - bar.left) * 0.25f, x = bar.left + ((bar.right - bar.left) - w) * (0.5f + 0.5f * std::sin(spin_));
                Fill(dc, {x, bar.top, x + w, bar.bottom}, Rgb(0x3D8BFF), 2.5f);
            }
            const std::wstring label = progress_[i] > 0 ? L"Pobieranie i instalacja… " + std::to_wstring(progress_[i]) + L"%" : L"Instalowanie…";
            Text(dc, label, smallFormat_.Get(), {r.left + 14.f, b.top + 20.f, r.right - 14.f, b.bottom + 6.f}, Rgb(0xFFFFFF, 0.7f));
            break;
        }
        default: {
            const bool failed = s == State::Failed;
            Fill(dc, b, hoverButton_ == v ? Rgb(failed ? 0xE0574F : 0x4C97FF) : Rgb(failed ? 0xC94A43 : 0x3D8BFF), 8.f);
            Text(dc, failed ? L"Ponów" : L"Pobierz", buttonFormat_.Get(), b, Rgb(0xFFFFFF));
            if (failed) {
                wchar_t code[32];
                swprintf_s(code, L"Błąd 0x%08X", static_cast<unsigned>(codes_[i]));
                Text(dc, code, smallFormat_.Get(), {r.left + 14.f, b.top, b.left - 8.f, b.bottom}, Rgb(0xFF8A80));
            }
            break;
        }
        }
    }

    void OnMouseMove(D2D1_POINT_2F p) override {
        int hover = -1, button = -1;
        for (int i = 0; i < static_cast<int>(std::size(kCategories)); ++i)
            if (Inside(CategoryRect(i), p)) hover = -100 - i;
        if (p.y >= GridTop() - 4.f)
            for (size_t v = 0; v < view_.size(); ++v) {
                const D2D1_RECT_F r = CardRect(static_cast<int>(v));
                if (!Inside(r, p)) continue;
                hover = static_cast<int>(v);
                if (Inside(ButtonRect(r), p)) button = static_cast<int>(v);
            }
        if (hover != hover_ || button != hoverButton_) {
            hover_ = hover;
            hoverButton_ = button;
            SetCursor(LoadCursorW(nullptr, button >= 0 ? IDC_HAND : IDC_ARROW));
            Redraw();
        }
    }
    void OnMouseLeave() override {
        hover_ = hoverButton_ = -1;
        Redraw();
    }
    void OnMouseDown(D2D1_POINT_2F p, bool right) override {
        if (right) return;
        searchFocus_ = Inside(SearchRect(), p);
        for (int i = 0; i < static_cast<int>(std::size(kCategories)); ++i)
            if (Inside(CategoryRect(i), p)) {
                category_ = i;
                scroll_ = 0;
                Filter();
            }
        if (p.y >= GridTop() - 4.f)
            for (size_t v = 0; v < view_.size(); ++v) {
                const D2D1_RECT_F r = CardRect(static_cast<int>(v));
                if (Inside(ButtonRect(r), p)) Install(view_[v]);
            }
        Redraw();
    }
    void OnWheel(int delta, D2D1_POINT_2F) override {
        scroll_ = std::clamp(scroll_ - delta / 120.f * 60.f, 0.f, MaxScroll());
        Redraw();
    }
    void OnResize() override { scroll_ = std::clamp(scroll_, 0.f, MaxScroll()); }
    void OnChar(wchar_t ch) override {  // pisanie zawsze trafia do wyszukiwarki
        if (ch == L'\b') {
            if (!query_.empty()) query_.pop_back();
        } else if (ch == 0x1B) {
            query_.clear();
        } else if (ch >= L' ') {
            query_ += ch;
        } else {
            return;
        }
        searchFocus_ = true;
        scroll_ = 0;
        Filter();
        Redraw();
    }
    void OnTimer(UINT_PTR id) override {
        if (id != kSpinTimer) return;
        if (std::find(states_.begin(), states_.end(), State::Installing) == states_.end()) return;
        spin_ += 0.12f;
        Redraw();
    }

    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override {
        result = 0;
        switch (msg) {
        case WM_INSTALLED_LIST: {
            std::string json;
            bool missing = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                json.swap(exportJson_);
                missing = wingetMissing_;
            }
            listLoaded_ = true;
            wingetMissingShown_ = missing;
            const std::string lower = [&] {
                std::string s = json;
                std::transform(s.begin(), s.end(), s.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
                return s;
            }();
            for (size_t i = 0; i < std::size(kApps); ++i) {
                if (states_[i] != State::Unknown) continue;
                const bool found = HasId(lower, kApps[i].id) || (kApps[i].alt && HasId(lower, kApps[i].alt)) || HasName(lower, kApps[i].name);
                states_[i] = found ? State::Installed : State::NotInstalled;
            }
            Redraw();
            return true;
        }
        case WM_INSTALL_PROGRESS:
            if (wp < states_.size()) {
                states_[wp] = State::Installing;
                progress_[wp] = static_cast<int>(lp);
                Redraw();
            }
            return true;
        case WM_INSTALL_DONE:
            if (wp < states_.size()) {
                const int code = static_cast<int>(lp);
                // 0x8A15002B: brak nowszej wersji (już zainstalowane), 0x8A150061: pakiet już zainstalowany
                const bool ok = code == 0 || code == static_cast<int>(0x8A15002B) || code == static_cast<int>(0x8A150061);
                states_[wp] = ok ? State::Installed : State::Failed;
                codes_[wp] = code;
                Redraw();
            }
            return true;
        }
        return false;
    }

private:
    // Identyfikator jako osobne słowo (w JSON w cudzysłowie, w tabeli "winget list" między spacjami)
    static bool HasId(const std::string& blob, const wchar_t* id) {
        const std::string needle = Utf8(Lower(id));
        auto sep = [](char c) { return c == '"' || c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        for (size_t p = blob.find(needle); p != std::string::npos; p = blob.find(needle, p + 1)) {
            const size_t end = p + needle.size();
            if ((p == 0 || sep(blob[p - 1])) && (end >= blob.size() || sep(blob[end]))) return true;
        }
        return false;
    }

    // Program bez dopasowania do źródła winget (identyfikator ARP\...) — po nazwie na początku wiersza tabeli,
    // z co najmniej dwiema spacjami za nią ("Discord" nie łapie "Discord Canary")
    static bool HasName(const std::string& blob, const wchar_t* name) {
        const std::string needle = "\n" + Utf8(Lower(name)) + "  ";
        return blob.find(needle) != std::string::npos;
    }

    // PNG z zasobów exe → bitmapa WIC (32bpp PBGRA, gotowa dla Direct2D)
    ComPtr<IWICBitmapSource> LoadIconResource(const std::wstring& name) const {
        const HRSRC res = FindResourceW(nullptr, name.c_str(), RT_RCDATA);
        const HGLOBAL data = res ? LoadResource(nullptr, res) : nullptr;
        const void* bytes = data ? LockResource(data) : nullptr;
        if (!bytes) return nullptr;
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(static_cast<const BYTE*>(bytes), SizeofResource(nullptr, res)));
        IWICImagingFactory* wic = g_->WIC();
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> conv;
        if (!stream || FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
            FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
            return nullptr;
        ComPtr<IWICBitmap> bitmap;  // kopia w pamięci
        if (FAILED(wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &bitmap))) return nullptr;
        return bitmap;
    }

    static std::string Utf8(const std::wstring& s) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
        return out;
    }

    // Układ
    D2D1_RECT_F CategoryRect(int i) const { return {10.f, 92.f + i * 36.f, kSideW - 10.f, 92.f + i * 36.f + 32.f}; }
    D2D1_RECT_F SearchRect() const { return {kSideW + kPad, 8.f, std::min(widthDip_ - kPad, kSideW + kPad + 420.f), 8.f + kSearchH}; }
    float GridTop() const { return titleH_ + 26.f; }
    int Columns() const { return std::max(1, static_cast<int>((widthDip_ - kSideW - 2 * kPad + kGap) / (kCardW + kGap))); }
    float CardWidth() const {
        const int cols = Columns();
        return (widthDip_ - kSideW - 2 * kPad - (cols - 1) * kGap) / cols;
    }
    D2D1_RECT_F CardRect(int v) const {
        const int cols = Columns();
        const float w = CardWidth();
        const float x = kSideW + kPad + (v % cols) * (w + kGap), y = GridTop() + (v / cols) * (kCardH + kGap) - scroll_;
        return {x, y, x + w, y + kCardH};
    }
    static D2D1_RECT_F ButtonRect(const D2D1_RECT_F& card) { return {card.right - 104.f, card.bottom - 42.f, card.right - 14.f, card.bottom - 12.f}; }
    float MaxScroll() const {
        const int rows = (static_cast<int>(view_.size()) + Columns() - 1) / Columns();
        return std::max(0.f, rows * (kCardH + kGap) + GridTop() + kPad - heightDip_);
    }
    bool IsCaption(D2D1_POINT_2F p) const override { return p.y < titleH_ && !Inside(SearchRect(), p); }

    void Filter() {
        view_.clear();
        const std::wstring q = Lower(query_);
        for (size_t i = 0; i < std::size(kApps); ++i) {
            if (category_ != 0 && kApps[i].category != category_) continue;
            if (!q.empty() && Lower(kApps[i].name).find(q) == std::wstring::npos && Lower(kApps[i].desc).find(q) == std::wstring::npos) continue;
            view_.push_back(i);
        }
    }

    void Install(size_t i) {
        if (states_[i] == State::Installing || states_[i] == State::Queued || states_[i] == State::Installed) return;
        states_[i] = State::Queued;
        progress_[i] = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(i);
        }
        if (workerRunning_) return;
        workerRunning_ = true;
        if (worker_.joinable()) worker_.join();
        const HWND hwnd = hwnd_;
        worker_ = std::thread([this, hwnd] {
            for (;;) {
                size_t i = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (queue_.empty() || closing_) break;
                    i = queue_.front();
                    queue_.pop_front();
                }
                PostMessageW(hwnd, WM_INSTALL_PROGRESS, i, 0);
                int last = -1;
                const int code = RunWinget(std::wstring(L"install --id ") + kApps[i].id +
                                               L" -e --silent --accept-package-agreements --accept-source-agreements --disable-interactivity",
                                           [&](const std::string& chunk) {
                                               // Ostatnie "NN%" w wyjściu = postęp pobierania
                                               for (size_t p = chunk.find('%'); p != std::string::npos; p = chunk.find('%', p + 1)) {
                                                   size_t s = p;
                                                   while (s > 0 && isdigit(static_cast<unsigned char>(chunk[s - 1]))) --s;
                                                   if (s == p) continue;
                                                   const int pct = std::clamp(atoi(chunk.substr(s, p - s).c_str()), 0, 100);
                                                   if (pct != last) {
                                                       last = pct;
                                                       PostMessageW(hwnd, WM_INSTALL_PROGRESS, i, pct);
                                                   }
                                               }
                                           });
                PostMessageW(hwnd, WM_INSTALL_DONE, i, code);
            }
            workerRunning_ = false;
        });
    }

    ComPtr<IDWriteTextFormat> titleFormat_, nameFormat_, textFormat_, smallFormat_, buttonFormat_, initialsFormat_, glyphFormat_, sideFormat_;
    std::vector<State> states_;
    std::vector<int> progress_, codes_;
    std::vector<size_t> view_;
    std::wstring query_;
    int category_ = 0, hover_ = -1, hoverButton_ = -1;
    float scroll_ = 0.f, spin_ = 0.f;
    bool searchFocus_ = false, listLoaded_ = false, wingetMissingShown_ = false;

    std::thread listThread_, worker_;
    std::vector<ComPtr<IWICBitmapSource>> pendingIcons_;
    std::vector<ComPtr<ID2D1Bitmap>> icons_;
    std::mutex mutex_;
    std::deque<size_t> queue_;
    std::string exportJson_;
    bool wingetMissing_ = false;
    std::atomic<bool> workerRunning_{false}, closing_{false};
};
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Graphics graphics;
    if (!graphics.Init()) return 1;
    auto store = std::make_unique<Store>();
    if (!store->Init(inst, graphics)) return 1;
    const int code = AppWindow::RunMessageLoop();
    store.reset();
    return code;
}
