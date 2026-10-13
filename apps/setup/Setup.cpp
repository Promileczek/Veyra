// Instalator Veyry (VeyraSetup.exe) — styl jak reszta (grafit, akcent #61B3FF, płaskie karty).
// Wersje: Minimal (sama powłoka: pulpit, dock, górny pasek, menu Start, Alt+Tab) i Full (wszystko: kropki, Pliki,
// Terminal, Kalkulator zamiast systemowego, Sklep, Ustawienia z aktualizacjami). Folder: Program Files\Veyra.
// "/odinstaluj" — deinstalacja (wpis w Aplikacjach Windows). Minimal: przywraca Eksplorator. Full: dodatkowo usuwa
// przejęcie calc.exe / ms-calculator: i przywraca systemowy Kalkulator.
//
// Okno działa ze zwykłymi uprawnieniami; pliki w Program Files i wpisy HKLM (deinstalacja, IFEO calc.exe) robi ten sam
// exe uruchomiony przez UAC z "/wykonaj …". Dzięki temu wpisy HKCU (powłoka, ms-calculator:, Kalkulator ze Sklepu)
// trafiają do konta użytkownika, a Veyra po instalacji startuje bez uprawnień administratora.
#include "../../src/core/AppWindow.h"
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr UINT WM_STEP = WM_APP + 40;  // wp = postęp w promilach, lp = std::wstring* (nowy opis kroku)
constexpr UINT WM_DONE = WM_APP + 41;  // lp = std::wstring* (pusty = sukces, inaczej treść błędu)
constexpr float kPad = 28.f;
const D2D1_COLOR_F kBody = D2D1::ColorF(0x1C1C21), kAccent = D2D1::ColorF(0.38f, 0.70f, 1.f), kOnAccent = D2D1::ColorF(0x0B1A26);
D2D1_COLOR_F White(float a) { return D2D1::ColorF(1.f, 1.f, 1.f, a); }

constexpr wchar_t kUninstallKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Veyra";
constexpr wchar_t kIfeoCalc[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\calc.exe";
constexpr wchar_t kWinlogon[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kCalcProtocol[] = L"Software\\Classes\\ms-calculator\\shell";
constexpr wchar_t kMarker[] = L"veyra-minimal";  // plik obok exploder.exe = wersja Minimal (src/core/Edition.h)

// Pliki w exe instalatora (setup_payload.rc, RCDATA); fullOnly = tylko w wersji Full
struct Payload {
    const wchar_t* res;
    const wchar_t* file;
    bool fullOnly;
};
constexpr Payload kPayload[] = {
    {L"I_EXPLODER", L"exploder.exe", false},
    {L"I_TERMINAL", L"exploder-terminal.exe", true},
    {L"I_CALC", L"exploder-calc.exe", true},
    {L"I_STORE", L"exploder-store.exe", true},
    {L"I_SETTINGS", L"exploder-settings.exe", true},
    {L"I_WP_DOMYSLNA", L"wallpapers\\veyra-domyslna.jpg", false},
    {L"I_WP_JEZIORO", L"wallpapers\\veyra-jezioro.jpg", false},
    {L"I_WP_GORY", L"wallpapers\\veyra-gory.jpg", false},
    {L"I_WP_ZORZA", L"wallpapers\\veyra-zorza.jpg", false},
};

// Kody wyjścia części z uprawnieniami administratora
enum Result { Ok = 0, ErrFiles = 10, ErrRegistry = 11, ErrRemove = 12, ErrArgs = 13 };

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

std::wstring SelfPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::wstring InstallDir() {
    PWSTR pf = nullptr;
    std::wstring dir = L"C:\\Program Files";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &pf))) dir = pf;
    CoTaskMemFree(pf);
    return dir + L"\\Veyra";
}

std::wstring RegString(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t value[1024]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return {};
    return value;
}
bool RegSetString(HKEY root, const wchar_t* key, const wchar_t* name, const std::wstring& v) {
    return RegSetKeyValueW(root, key, name, REG_SZ, v.c_str(), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool IsElevated() {
    HANDLE token = nullptr;
    TOKEN_ELEVATION e{};
    DWORD size = 0;
    const bool ok = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
                    GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size);
    if (token) CloseHandle(token);
    return ok && e.TokenIsElevated;
}

// Zainstalowana wersja: L"Full", L"Minimal" albo pusty (brak)
std::wstring InstalledEdition() { return RegString(HKEY_LOCAL_MACHINE, kUninstallKey, L"VeyraEdition"); }

// Procesy o nazwie spełniającej warunek (np. exploder*.exe)
template <class Pred>
std::vector<DWORD> FindProcesses(Pred pred) {
    std::vector<DWORD> ids;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return ids;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (pred(Lower(pe.szExeFile))) ids.push_back(pe.th32ProcessID);
    CloseHandle(snap);
    return ids;
}
bool IsVeyraProcess(const std::wstring& name) { return name.rfind(L"exploder", 0) == 0 && name.size() > 4 && name.ends_with(L".exe"); }
bool VeyraRunning() { return !FindProcesses([](const std::wstring& n) { return n == L"exploder.exe"; }).empty(); }
bool ExplorerRunning() { return !FindProcesses([](const std::wstring& n) { return n == L"explorer.exe"; }).empty(); }

template <class Pred>
void KillProcesses(Pred pred) {
    for (DWORD id : FindProcesses(pred)) {
        if (HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, id)) {
            TerminateProcess(p, 1);  // kod 1 jak taskkill /f — przy 0 Windows uruchamia Eksplorator ponownie
            WaitForSingleObject(p, 5000);
            CloseHandle(p);
        }
    }
}
// Najpierw porządnie (Minimal przywraca wtedy pasek i pulpit Eksploratora), potem na siłę
void KillVeyra() {
    const UINT quit = RegisterWindowMessageW(L"VeyraQuit");
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, L"ExploderTaskbar", nullptr)) != nullptr) PostMessageW(h, quit, 0, 0);
    for (int i = 0; i < 20 && VeyraRunning(); ++i) Sleep(200);
    KillProcesses(IsVeyraProcess);
}

bool StartProcess(const std::wstring& exe, const std::wstring& args = L"", bool hidden = false, DWORD* exitCode = nullptr) {
    std::wstring cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, hidden ? CREATE_NO_WINDOW : 0, nullptr, nullptr, &si, &pi))
        return false;
    if (exitCode) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, exitCode);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// Polecenie PowerShella bez okna; zwraca kod wyjścia (-1 = nie uruchomiono)
int PowerShell(const std::wstring& command) {
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    DWORD code = 0;
    if (!StartProcess(std::wstring(sys) + L"\\WindowsPowerShell\\v1.0\\powershell.exe",
                      L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + command + L"\"", true, &code))
        return -1;
    return static_cast<int>(code);
}

bool CalculatorAppInstalled() { return PowerShell(L"if (Get-AppxPackage Microsoft.WindowsCalculator) { exit 0 } else { exit 1 }") == 0; }
// Systemowy Kalkulator z powrotem: najpierw ponowna rejestracja pakietu (jeśli jest jeszcze w systemie), potem Sklep przez winget
bool RestoreCalculatorApp() {
    if (CalculatorAppInstalled()) return true;
    PowerShell(L"Add-AppxPackage -RegisterByFamilyName -MainPackage Microsoft.WindowsCalculator_8wekyb3d8bbwe -ErrorAction SilentlyContinue");
    if (CalculatorAppInstalled()) return true;
    PowerShell(L"winget install --id 9WZDNCRFHVN5 -s msstore --accept-package-agreements --accept-source-agreements --silent");
    return CalculatorAppInstalled();
}

bool WriteResource(const wchar_t* res, const std::wstring& path) {
    const HRSRC r = FindResourceW(nullptr, res, RT_RCDATA);
    const HGLOBAL h = r ? LoadResource(nullptr, r) : nullptr;
    const void* data = h ? LockResource(h) : nullptr;
    const DWORD size = r ? SizeofResource(nullptr, r) : 0;
    if (!data) return false;
    for (int attempt = 0; attempt < 20; ++attempt) {  // zamknięty przed chwilą proces potrafi jeszcze trzymać plik
        const HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const bool ok = WriteFile(f, data, size, &written, nullptr) && written == size;
            CloseHandle(f);
            return ok;
        }
        Sleep(250);
    }
    return false;
}

// ---------------- Część z uprawnieniami administratora (/wykonaj …) ----------------
int ElevatedInstall(bool full) {
    const std::wstring dir = InstallDir();
    KillVeyra();
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW((dir + L"\\wallpapers").c_str(), nullptr);
    for (const Payload& p : kPayload) {
        const std::wstring path = dir + L"\\" + p.file;
        if (p.fullOnly && !full) DeleteFileW(path.c_str());  // zmiana Full → Minimal
        else if (!WriteResource(p.res, path)) return ErrFiles;
    }
    const std::wstring marker = dir + L"\\" + kMarker;
    if (full) {
        DeleteFileW(marker.c_str());
    } else if (HANDLE f = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
               f != INVALID_HANDLE_VALUE) {
        CloseHandle(f);
    } else {
        return ErrFiles;
    }
    const std::wstring setup = dir + L"\\VeyraSetup.exe";  // deinstalator
    if (Lower(SelfPath()) != Lower(setup) && !CopyFileW(SelfPath().c_str(), setup.c_str(), FALSE)) return ErrFiles;

    // Aplikacje Windows → "Veyra"
    DWORD sizeKb = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied))
        if (e.is_regular_file()) sizeKb += static_cast<DWORD>(e.file_size() / 1024);
    const DWORD one = 1;
    const bool reg = RegSetString(HKEY_LOCAL_MACHINE, kUninstallKey, L"DisplayName", full ? L"Veyra" : L"Veyra Minimal") &&
                     RegSetString(HKEY_LOCAL_MACHINE, kUninstallKey, L"DisplayVersion", L"" VEYRA_VERSION) &&
                     RegSetString(HKEY_LOCAL_MACHINE, kUninstallKey, L"Publisher", L"Promileczek") &&
                     RegSetString(HKEY_LOCAL_MACHINE, kUninstallKey, L"DisplayIcon", setup + L",0") &&
                     RegSetString(HKEY_LOCAL_MACHINE, kUninstallKey, L"InstallLocation", dir) &&
                     RegSetString(HKEY_LOCAL_MACHINE, kUninstallKey, L"UninstallString", L"\"" + setup + L"\" /odinstaluj") &&
                     RegSetString(HKEY_LOCAL_MACHINE, kUninstallKey, L"URLInfoAbout", L"https://github.com/Promileczek/Veyra") &&
                     RegSetString(HKEY_LOCAL_MACHINE, kUninstallKey, L"VeyraEdition", full ? L"Full" : L"Minimal") &&
                     RegSetKeyValueW(HKEY_LOCAL_MACHINE, kUninstallKey, L"NoModify", REG_DWORD, &one, sizeof(one)) == ERROR_SUCCESS &&
                     RegSetKeyValueW(HKEY_LOCAL_MACHINE, kUninstallKey, L"NoRepair", REG_DWORD, &one, sizeof(one)) == ERROR_SUCCESS &&
                     RegSetKeyValueW(HKEY_LOCAL_MACHINE, kUninstallKey, L"EstimatedSize", REG_DWORD, &sizeKb, sizeof(sizeKb)) == ERROR_SUCCESS;
    if (!reg) return ErrRegistry;

    // calc.exe → Kalkulator Veyry (Full); w Minimal zdejmujemy nasze przejęcie
    if (full) {
        if (!RegSetString(HKEY_LOCAL_MACHINE, kIfeoCalc, L"Debugger", L"\"" + dir + L"\\exploder-calc.exe\"")) return ErrRegistry;
    } else if (Lower(RegString(HKEY_LOCAL_MACHINE, kIfeoCalc, L"Debugger")).find(L"exploder-calc.exe") != std::wstring::npos) {
        RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, kIfeoCalc, L"Debugger");
    }
    return Ok;
}

int ElevatedUninstall() {
    KillVeyra();
    if (Lower(RegString(HKEY_LOCAL_MACHINE, kIfeoCalc, L"Debugger")).find(L"exploder-calc.exe") != std::wstring::npos)
        RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, kIfeoCalc, L"Debugger");
    std::wstring dir = RegString(HKEY_LOCAL_MACHINE, kUninstallKey, L"InstallLocation");
    if (dir.empty()) dir = InstallDir();
    std::error_code ec;
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::filesystem::remove_all(dir, ec);
        if (!std::filesystem::exists(dir)) break;
        Sleep(300);
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kUninstallKey);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, kUninstallKey);
    return std::filesystem::exists(dir) ? ErrRemove : Ok;
}

// Uruchamia część administracyjną (UAC) i czeka; zwraca kod wyjścia albo komunikat o odmowie
int RunElevated(HWND owner, const std::wstring& args, bool& cancelled) {
    cancelled = false;
    if (IsElevated()) {  // już jako administrator (np. UAC wyłączone) — bez drugiego procesu
        if (args == L"/wykonaj odinstaluj") return ElevatedUninstall();
        return ElevatedInstall(args == L"/wykonaj instaluj full");
    }
    const std::wstring self = SelfPath();
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = self.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        cancelled = GetLastError() == ERROR_CANCELLED;
        return ErrArgs;
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = ErrArgs;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return static_cast<int>(code);
}

std::wstring ErrorText(int code) {
    switch (code) {
    case ErrFiles: return L"Nie udało się zapisać plików w Program Files. Zamknij programy Veyry i spróbuj ponownie.";
    case ErrRegistry: return L"Nie udało się zapisać wpisów w rejestrze.";
    case ErrRemove: return L"Nie udało się usunąć wszystkich plików z Program Files\\Veyra. Usuń folder ręcznie.";
    default: return L"Nieznany błąd (kod " + std::to_wstring(code) + L").";
    }
}

// Ponowne uruchomienie powłoki: Eksplorator zamknięty, Veyra ze zwykłymi uprawnieniami (ten proces ich nie ma)
void StartVeyraShell(const std::wstring& dir) {
    for (int round = 0; round < 5 && ExplorerRunning(); ++round) {  // Eksplorator potrafi wrócić sam — sprawdzamy
        KillProcesses([](const std::wstring& n) { return n == L"explorer.exe"; });
        Sleep(1500);
    }
    StartProcess(dir + L"\\exploder.exe");
}

// ---------------- Okno ----------------
class SetupApp : public AppWindow {
public:
    bool Init(HINSTANCE inst, Graphics& g, bool uninstall) {
        Options o;
        o.className = L"VeyraSetup";
        o.title = uninstall ? L"Odinstaluj Veyrę" : L"Instalator Veyry";
        o.width = 640.f;
        o.height = 450.f;
        o.resizable = false;
        titleH_ = 40.f;
        showTitle_ = false;
        installed_ = InstalledEdition();
        full_ = installed_ != L"Minimal";
        page_ = uninstall ? Page::Uninstall : Page::Choose;
        if (!Create(inst, g, o)) return false;
        headerFormat_ = MakeFormat(L"Segoe UI", 26.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        subFormat_ = MakeFormat(L"Segoe UI", 13.f);
        cardTitleFormat_ = MakeFormat(L"Segoe UI", 17.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        titleFormat_ = MakeFormat(L"Segoe UI", 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        descFormat_ = MakeFormat(L"Segoe UI", 11.5f);
        buttonFormat_ = MakeFormat(L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        wrapFormat_ = MakeFormat(L"Segoe UI", 12.f);
        if (wrapFormat_) {
            wrapFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            wrapFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        Redraw();
        return true;
    }

    ~SetupApp() override {
        if (worker_.joinable()) worker_.join();
    }

protected:
    enum class Page { Choose, Uninstall, Working, Done };
    enum Hit { HitNone, HitMinimal, HitFull, HitUseNow, HitInstall, HitCancel, HitUninstall, HitClose };

    void Paint(ID2D1DeviceContext* dc) override {
        hits_.clear();
        dc->Clear(kBody);
        const float W = widthDip_, H = heightDip_;
        switch (page_) {
        case Page::Choose: PaintChoose(dc, W, H); break;
        case Page::Uninstall: PaintUninstall(dc, W, H); break;
        case Page::Working: PaintWorking(dc, W, H); break;
        case Page::Done: PaintDone(dc, W, H); break;
        }
    }

    void Header(ID2D1DeviceContext* dc, const wchar_t* title, const std::wstring& sub, float W) {
        Text(dc, title, headerFormat_.Get(), {kPad, 40.f, W - kPad, 76.f}, White(0.96f));
        Text(dc, sub, subFormat_.Get(), {kPad, 78.f, W - kPad, 98.f}, White(0.5f));
    }

    void PaintChoose(ID2D1DeviceContext* dc, float W, float H) {
        std::wstring sub = L"Własna powłoka Windows · wersja " VEYRA_VERSION;
        if (!installed_.empty()) sub += L" · zainstalowana: " + installed_;
        Header(dc, L"Zainstaluj Veyrę", sub, W);

        const float top = 118.f, cardH = 150.f, gap = 16.f, cw = (W - 2 * kPad - gap) / 2;
        Edition(dc, {kPad, top, kPad + cw, top + cardH}, L"Minimal",
                L"Sam wygląd: pulpit z tapetą i panelem Pliki, dock, górny pasek, menu Start i Alt+Tab. Eksplorator działa "
                L"w tle, programy zostają systemowe.",
                !full_, HitMinimal);
        Edition(dc, {kPad + cw + gap, top, W - kPad, top + cardH}, L"Full",
                L"Wszystko z Minimal, a do tego kropki w oknach, okna Pliki, Terminal, Kalkulator (zamiast systemowego), "
                L"Sklep i Ustawienia z aktualizacjami.",
                full_, HitFull);

        // Używaj od razu
        const D2D1_RECT_F row{kPad, top + cardH + 16.f, W - kPad, top + cardH + 80.f};
        Card(dc, row, false, hover_ == HitUseNow);
        Text(dc, L"Używaj Veyry od razu", titleFormat_.Get(), {row.left + 18.f, row.top + 12.f, row.right - 90.f, row.top + 32.f},
             White(0.95f));
        Text(dc, L"Zastąpi Eksplorator Windows teraz i po każdym zalogowaniu", descFormat_.Get(),
             {row.left + 18.f, row.top + 32.f, row.right - 90.f, row.top + 52.f}, White(0.5f));
        const D2D1_RECT_F sw{row.right - 64.f, row.top + 21.f, row.right - 20.f, row.top + 43.f};
        Fill(dc, sw, useNow_ ? kAccent : White(hover_ == HitUseNow ? 0.22f : 0.15f), 11.f);
        brush_->SetColor(useNow_ ? kOnAccent : White(0.9f));
        dc->FillEllipse(D2D1::Ellipse({useNow_ ? sw.right - 11.f : sw.left + 11.f, (sw.top + sw.bottom) / 2}, 7.f, 7.f), brush_.Get());
        hits_.push_back({row, HitUseNow});

        Text(dc, L"Folder: " + InstallDir(), descFormat_.Get(), {kPad + 2.f, row.bottom + 12.f, W - kPad, row.bottom + 30.f},
             White(0.4f));

        // Przyciski
        const float by = H - 58.f;
        const bool same = installed_ == (full_ ? L"Full" : L"Minimal");
        Button(dc, {W - kPad - 150.f, by, W - kPad, by + 36.f}, installed_.empty() ? L"Zainstaluj" : same ? L"Zainstaluj ponownie" : L"Zmień wersję",
               HitInstall, true);
        Button(dc, {W - kPad - 260.f, by, W - kPad - 160.f, by + 36.f}, L"Anuluj", HitCancel, false);
        if (!installed_.empty()) Button(dc, {kPad, by, kPad + 120.f, by + 36.f}, L"Odinstaluj", HitUninstall, false);
    }

    void PaintUninstall(ID2D1DeviceContext* dc, float W, float H) {
        Header(dc, L"Odinstaluj Veyrę", installed_.empty() ? L"Veyra nie jest zainstalowana" : L"Zainstalowana wersja: " + installed_, W);
        const bool full = installed_ != L"Minimal";
        const std::wstring what = full ? L"Zostaną usunięte: powłoka Veyra, Terminal, Kalkulator, Sklep i Ustawienia Veyry.\n\n"
                                         L"Wróci Eksplorator Windows, a calc.exe znowu otworzy systemowy Kalkulator — "
                                         L"jeśli go nie ma, zostanie zainstalowany ze Sklepu Microsoft.\n\n"
                                         L"Twoje ustawienia Veyry (%LOCALAPPDATA%\\Exploder) zostają."
                                       : L"Zostanie usunięta powłoka Veyra i wróci Eksplorator Windows.\n\n"
                                         L"Twoje ustawienia Veyry (%LOCALAPPDATA%\\Exploder) zostają.";
        const D2D1_RECT_F card{kPad, 118.f, W - kPad, 270.f};
        Card(dc, card, false, false);
        Text(dc, what, wrapFormat_.Get(), {card.left + 18.f, card.top + 16.f, card.right - 18.f, card.bottom - 12.f}, White(0.8f));
        const float by = H - 58.f;
        Button(dc, {W - kPad - 150.f, by, W - kPad, by + 36.f}, L"Odinstaluj", HitUninstall, true);
        Button(dc, {W - kPad - 260.f, by, W - kPad - 160.f, by + 36.f}, L"Anuluj", HitCancel, false);
    }

    void PaintWorking(ID2D1DeviceContext* dc, float W, float) {
        Header(dc, uninstalling_ ? L"Odinstalowywanie…" : L"Instalowanie…", uninstalling_ ? L"Veyra" : full_ ? L"Veyra Full" : L"Veyra Minimal", W);
        const D2D1_RECT_F bar{kPad, 160.f, W - kPad, 168.f};
        Fill(dc, bar, White(0.08f), 4.f);
        Fill(dc, {bar.left, bar.top, bar.left + (bar.right - bar.left) * progress_, bar.bottom}, kAccent, 4.f);
        Text(dc, step_, subFormat_.Get(), {kPad, 180.f, W - kPad, 200.f}, White(0.7f));
    }

    void PaintDone(ID2D1DeviceContext* dc, float W, float H) {
        const bool ok = error_.empty();
        Header(dc, ok ? (uninstalling_ ? L"Veyra odinstalowana" : L"Veyra zainstalowana") : L"Coś poszło nie tak",
               uninstalling_ ? L"" : full_ ? L"Veyra Full" : L"Veyra Minimal", W);
        std::wstring msg = error_;
        if (ok && uninstalling_) msg = L"Eksplorator Windows wrócił." + note_;
        else if (ok && (useNow_ || wasRunning_)) msg = L"Veyra działa i będzie uruchamiana po każdym zalogowaniu." + note_;
        else if (ok) msg = L"Folder: " + InstallDir() + L"\n\nWłącz Veyrę w Ustawieniach → System albo uruchom instalator ponownie z opcją „Używaj Veyry od razu”." + note_;
        const D2D1_RECT_F card{kPad, 118.f, W - kPad, 270.f};
        Card(dc, card, false, false);
        Text(dc, msg, wrapFormat_.Get(), {card.left + 18.f, card.top + 16.f, card.right - 18.f, card.bottom - 12.f}, White(0.8f));
        const float by = H - 58.f;
        Button(dc, {W - kPad - 150.f, by, W - kPad, by + 36.f}, L"Zamknij", HitClose, true);
    }

    void Card(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, bool selected, bool hot) {
        Fill(dc, r, White(hot ? 0.075f : 0.05f), 12.f);
        brush_->SetColor(selected ? kAccent : White(0.08f));
        const float w = selected ? 2.f : 1.f;
        dc->DrawRoundedRectangle(D2D1::RoundedRect({r.left + w / 2, r.top + w / 2, r.right - w / 2, r.bottom - w / 2}, 12.f, 12.f), brush_.Get(), w);
    }

    void Edition(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const wchar_t* name, const wchar_t* desc, bool selected, int hit) {
        Card(dc, r, selected, hover_ == hit);
        Text(dc, name, cardTitleFormat_.Get(), {r.left + 18.f, r.top + 16.f, r.right - 50.f, r.top + 42.f}, White(0.96f));
        // Znacznik wyboru (kółko)
        const D2D1_POINT_2F c{r.right - 28.f, r.top + 29.f};
        brush_->SetColor(selected ? kAccent : White(0.25f));
        if (selected) {
            dc->FillEllipse(D2D1::Ellipse(c, 9.f, 9.f), brush_.Get());
            brush_->SetColor(kOnAccent);
            dc->FillEllipse(D2D1::Ellipse(c, 3.5f, 3.5f), brush_.Get());
        } else {
            dc->DrawEllipse(D2D1::Ellipse(c, 8.5f, 8.5f), brush_.Get(), 1.5f);
        }
        Text(dc, desc, wrapFormat_.Get(), {r.left + 18.f, r.top + 54.f, r.right - 18.f, r.bottom - 12.f}, White(0.7f));
        hits_.push_back({r, hit});
    }

    void Button(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const wchar_t* label, int hit, bool primary) {
        const bool hot = hover_ == hit;
        if (primary) Fill(dc, r, hot ? D2D1::ColorF(0.48f, 0.76f, 1.f) : kAccent, 8.f);
        else Fill(dc, r, White(hot ? 0.14f : 0.08f), 8.f);
        Text(dc, label, buttonFormat_.Get(), r, primary ? kOnAccent : White(0.95f));
        hits_.push_back({r, hit});
    }

    int HitAt(D2D1_POINT_2F p) const {
        for (const auto& h : hits_)
            if (Inside(h.rect, p)) return h.id;
        return HitNone;
    }

    void OnMouseMove(D2D1_POINT_2F p) override {
        const int h = HitAt(p);
        if (h != hover_) {
            hover_ = h;
            Redraw();
        }
    }
    void OnMouseLeave() override {
        hover_ = HitNone;
        Redraw();
    }

    void OnMouseDown(D2D1_POINT_2F p, bool right) override {
        if (right) return;
        switch (HitAt(p)) {
        case HitMinimal: full_ = false; break;
        case HitFull: full_ = true; break;
        case HitUseNow: useNow_ = !useNow_; break;
        case HitInstall: StartWork(false); break;
        case HitUninstall:
            if (page_ == Page::Choose) page_ = Page::Uninstall;
            else StartWork(true);
            break;
        case HitCancel:
            if (page_ == Page::Uninstall && !uninstallOnly_) page_ = Page::Choose;
            else PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case HitClose: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        default: return;
        }
        Redraw();
    }

    bool OnKeyDown(UINT vk) override {
        if (vk == VK_ESCAPE && page_ != Page::Working) {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return true;
        }
        return false;
    }

    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override {
        if (msg == WM_CLOSE && page_ == Page::Working) {  // w trakcie pracy nie zamykamy
            result = 0;
            return true;
        }
        if (msg == WM_STEP) {
            progress_ = static_cast<float>(wp) / 1000.f;
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            if (text) step_ = *text;
            Redraw();
            return true;
        }
        if (msg == WM_DONE) {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            error_ = text ? *text : L"";
            page_ = Page::Done;
            installed_ = InstalledEdition();
            Redraw();
            SetForegroundWindow(hwnd_);
            return true;
        }
        return false;
    }

public:
    bool uninstallOnly_ = false;

private:
    void Step(float progress, const std::wstring& text) { PostMessageW(hwnd_, WM_STEP, static_cast<WPARAM>(progress * 1000.f), reinterpret_cast<LPARAM>(new std::wstring(text))); }

    void StartWork(bool uninstall) {
        if (worker_.joinable()) worker_.join();
        uninstalling_ = uninstall;
        page_ = Page::Working;
        progress_ = 0.f;
        step_.clear();
        note_.clear();
        wasRunning_ = VeyraRunning();
        const HWND owner = hwnd_;
        worker_ = std::thread([this, owner, uninstall] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const std::wstring error = uninstall ? Uninstall(owner) : Install(owner);
            CoUninitialize();
            PostMessageW(owner, WM_DONE, 0, reinterpret_cast<LPARAM>(new std::wstring(error)));
        });
    }

    std::wstring Install(HWND owner) {
        const bool full = full_;
        const std::wstring previous = installed_;
        const std::wstring dir = InstallDir();
        Step(0.1f, L"Kopiowanie plików do " + dir + L"…");
        bool cancelled = false;
        const int code = RunElevated(owner, full ? L"/wykonaj instaluj full" : L"/wykonaj instaluj minimal", cancelled);
        if (cancelled) return L"Instalacja wymaga zgody administratora (Program Files).";
        if (code != Ok) return ErrorText(code);

        const std::wstring calc = L"\"" + dir + L"\\exploder-calc.exe\"";
        if (full) {
            Step(0.55f, L"Kalkulator Veyry zamiast systemowego…");
            RegSetString(HKEY_CURRENT_USER, kCalcProtocol, nullptr, L"open");
            RegSetString(HKEY_CURRENT_USER, (std::wstring(kCalcProtocol) + L"\\open\\command").c_str(), nullptr, calc);
            // Kalkulator ze Sklepu omija calc.exe (wyszukiwarka, kafelek) — usuwamy go; deinstalacja Veyry go przywraca
            PowerShell(L"Get-AppxPackage Microsoft.WindowsCalculator | Remove-AppxPackage");
        } else if (Lower(RegString(HKEY_CURRENT_USER, (std::wstring(kCalcProtocol) + L"\\open\\command").c_str(), nullptr))
                       .find(L"exploder-calc.exe") != std::wstring::npos) {
            RegDeleteTreeW(HKEY_CURRENT_USER, kCalcProtocol);
        }
        if (!full && previous == L"Full") {
            Step(0.6f, L"Przywracanie Kalkulatora Windows…");
            if (!RestoreCalculatorApp()) note_ = L"\n\nKalkulatora Windows nie udało się przywrócić — zainstaluj go ze Sklepu Microsoft.";
        }

        // Po zalogowaniu (HKCU — konto użytkownika, nie administratora). Full: Veyra zamiast Eksploratora (Winlogon
        // Shell). Minimal: Eksplorator zostaje powłoką, Veyra startuje z autostartu (Run) i chowa jego pasek i pulpit.
        const std::wstring exe = L"\"" + dir + L"\\exploder.exe\"";
        const bool shellOurs = Lower(RegString(HKEY_CURRENT_USER, kWinlogon, L"Shell")).find(L"exploder.exe") != std::wstring::npos;
        const bool using_ = useNow_ || shellOurs || !RegString(HKEY_CURRENT_USER, kRun, L"Veyra").empty();
        if (full) {
            if (using_) RegSetString(HKEY_CURRENT_USER, kWinlogon, L"Shell", exe);
            RegDeleteKeyValueW(HKEY_CURRENT_USER, kRun, L"Veyra");
        } else {
            if (shellOurs) RegDeleteKeyValueW(HKEY_CURRENT_USER, kWinlogon, L"Shell");
            if (using_) RegSetString(HKEY_CURRENT_USER, kRun, L"Veyra", exe);
        }
        if (useNow_ || wasRunning_) {
            Step(0.85f, L"Uruchamianie Veyry…");
            if (full) StartVeyraShell(dir);
            else StartProcess(dir + L"\\exploder.exe");  // sama uruchomi Eksplorator, jeśli nie działa
        }
        Step(1.f, L"Gotowe");
        return {};
    }

    std::wstring Uninstall(HWND owner) {
        const bool full = installed_ != L"Minimal";
        Step(0.1f, L"Zamykanie Veyry…");
        KillVeyra();
        Sleep(1500);  // Windows potrafi sam uruchomić Eksplorator po zamknięciu powłoki
        if (Lower(RegString(HKEY_CURRENT_USER, kWinlogon, L"Shell")).find(L"exploder.exe") != std::wstring::npos)
            RegDeleteKeyValueW(HKEY_CURRENT_USER, kWinlogon, L"Shell");
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRun, L"Veyra");
        if (!ExplorerRunning()) {
            wchar_t win[MAX_PATH]{};
            GetWindowsDirectoryW(win, MAX_PATH);
            StartProcess(std::wstring(win) + L"\\explorer.exe");
        }
        Step(0.3f, L"Usuwanie plików…");
        bool cancelled = false;
        const int code = RunElevated(owner, L"/wykonaj odinstaluj", cancelled);
        if (cancelled) return L"Deinstalacja wymaga zgody administratora (Program Files).";
        if (code != Ok) return ErrorText(code);
        if (full) {
            Step(0.6f, L"Przywracanie Kalkulatora Windows…");
            RegDeleteTreeW(HKEY_CURRENT_USER, kCalcProtocol);
            if (!RestoreCalculatorApp()) note_ = L"\n\nKalkulatora Windows nie udało się przywrócić — zainstaluj go ze Sklepu Microsoft.";
        }
        Step(1.f, L"Gotowe");
        return {};
    }

    struct HitRect {
        D2D1_RECT_F rect;
        int id;
    };
    std::vector<HitRect> hits_;
    int hover_ = HitNone;
    Page page_ = Page::Choose;
    std::wstring installed_, step_, error_, note_;
    bool full_ = true, useNow_ = true, uninstalling_ = false, wasRunning_ = false;
    float progress_ = 0.f;
    std::thread worker_;
    ComPtr<IDWriteTextFormat> headerFormat_, subFormat_, cardTitleFormat_, titleFormat_, descFormat_, buttonFormat_, wrapFormat_;
};
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(argv, argv + argc);
    LocalFree(argv);
    // Część administracyjna (bez okna)
    if (args.size() >= 3 && args[1] == L"/wykonaj") {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int code = ErrArgs;
        if (args[2] == L"odinstaluj") code = ElevatedUninstall();
        else if (args[2] == L"instaluj" && args.size() >= 4) code = ElevatedInstall(args[3] == L"full");
        CoUninitialize();
        return code;
    }
    const bool uninstall = args.size() >= 2 && args[1] == L"/odinstaluj";
    const std::wstring self = SelfPath();
    // Deinstalator z folderu instalacji: kopia w %TEMP% (folder da się wtedy usunąć), na końcu kopia usuwa się sama
    if (uninstall && Lower(self).rfind(Lower(InstallDir() + L"\\"), 0) == 0) {
        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        const std::wstring copy = std::wstring(temp) + L"VeyraUninstall.exe";
        if (CopyFileW(self.c_str(), copy.c_str(), FALSE) && StartProcess(copy, L"/odinstaluj")) return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Graphics graphics;
    if (!graphics.Init()) return 1;
    auto app = std::make_unique<SetupApp>();
    app->uninstallOnly_ = uninstall;
    if (!app->Init(inst, graphics, uninstall)) return 1;
    const int code = AppWindow::RunMessageLoop();
    app.reset();
    if (uninstall && Lower(self).find(L"\\veyrauninstall.exe") != std::wstring::npos) {
        wchar_t sys[MAX_PATH]{};
        GetSystemDirectoryW(sys, MAX_PATH);
        StartProcess(std::wstring(sys) + L"\\cmd.exe", L"/c ping -n 3 127.0.0.1 >nul & del /f /q \"" + self + L"\"", true);
    }
    return code;
}
