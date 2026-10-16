#include "ExplorerUi.h"
#include "../core/Settings.h"
#include <shellapi.h>
#include <string>
#include <vector>

namespace {
std::vector<HWND> g_hidden;   // okna Eksploratora schowane przez nas (pokazujemy je z powrotem)
LPARAM g_originalState = -1;  // stan paska (ABS_AUTOHIDE…) sprzed zmiany; -1 = nie zmienialiśmy

void HideWindow(HWND h) {
    if (!h || !IsWindowVisible(h)) return;
    ShowWindow(h, SW_HIDE);
    for (HWND k : g_hidden)
        if (k == h) return;
    g_hidden.push_back(h);
}

// Paski Eksploratora: główny i na pozostałych monitorach
template <class Fn>
void ForEachExplorerTaskbar(Fn fn) {
    for (const wchar_t* cls : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
        HWND h = nullptr;
        while ((h = FindWindowExW(nullptr, h, cls, nullptr)) != nullptr)
            if (ExplorerUi::IsExplorerWindow(h)) fn(h);
    }
}

void HideAll() {
    ForEachExplorerTaskbar([](HWND tray) {
        if (!IsWindowVisible(tray)) return;
        // Autoukrywanie: pasek nie zajmuje miejsca (obszar roboczy dla docku i górnego paska Veyry)
        APPBARDATA abd{sizeof(abd)};
        abd.hWnd = tray;
        const LPARAM state = static_cast<LPARAM>(SHAppBarMessage(ABM_GETSTATE, &abd));
        if (!(state & ABS_AUTOHIDE)) {
            if (g_originalState < 0) {
                g_originalState = state;
                Settings::Set(L"minimalTaskbarState", std::to_wstring(state));  // dla strażnika (RestoreAfterKill)
            }
            abd.lParam = state | ABS_AUTOHIDE;
            SHAppBarMessage(ABM_SETSTATE, &abd);
        }
        HideWindow(tray);
    });
    // Pulpit Eksploratora (ikony, tapeta) — pod spodem jest pulpit Veyry
    if (HWND progman = FindWindowW(L"Progman", nullptr); progman && ExplorerUi::IsExplorerWindow(progman)) HideWindow(progman);
    EnumWindows([](HWND h, LPARAM) -> BOOL {
        wchar_t cls[16]{};
        if (GetClassNameW(h, cls, ARRAYSIZE(cls)) && wcscmp(cls, L"WorkerW") == 0 && ExplorerUi::IsExplorerWindow(h)) HideWindow(h);
        return TRUE;
    }, 0);
}
}  // namespace

namespace ExplorerUi {
bool IsExplorerWindow(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid || pid == GetCurrentProcessId()) return false;
    bool explorer = false;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH]{};
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(p, 0, path, &n)) {
            const wchar_t* name = wcsrchr(path, L'\\');
            explorer = _wcsicmp(name ? name + 1 : path, L"explorer.exe") == 0;
        }
        CloseHandle(p);
    }
    return explorer;
}

bool EnsureRunning() {
    auto running = [] {
        HWND h = nullptr;
        while ((h = FindWindowExW(nullptr, h, L"Shell_TrayWnd", nullptr)) != nullptr)
            if (IsExplorerWindow(h)) return true;
        return false;
    };
    if (running()) return true;
    wchar_t win[MAX_PATH]{};
    GetWindowsDirectoryW(win, MAX_PATH);
    ShellExecuteW(nullptr, L"open", (std::wstring(win) + L"\\explorer.exe").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    for (int i = 0; i < 60 && !running(); ++i) Sleep(250);
    Sleep(500);  // pasek już jest, ale Eksplorator jeszcze go układa
    return running();
}

void Hide() { HideAll(); }

void KeepHidden() { HideAll(); }

void Restore() {
    if (g_originalState >= 0) {
        ForEachExplorerTaskbar([](HWND tray) {
            APPBARDATA abd{sizeof(abd)};
            abd.hWnd = tray;
            abd.lParam = g_originalState;
            SHAppBarMessage(ABM_SETSTATE, &abd);
        });
        g_originalState = -1;
        Settings::Set(L"minimalTaskbarState", L"");
    }
    for (HWND h : g_hidden)
        if (IsWindow(h)) ShowWindow(h, SW_SHOWNA);
    g_hidden.clear();
}

void RestoreAfterKill() {
    Settings::Reload();
    if (const std::wstring saved = Settings::Get(L"minimalTaskbarState"); !saved.empty()) {
        const LPARAM state = _wtoi(saved.c_str());
        ForEachExplorerTaskbar([state](HWND tray) {
            APPBARDATA abd{sizeof(abd)};
            abd.hWnd = tray;
            abd.lParam = state;
            SHAppBarMessage(ABM_SETSTATE, &abd);
        });
        Settings::Set(L"minimalTaskbarState", L"");
    }
    ForEachExplorerTaskbar([](HWND tray) {
        if (!IsWindowVisible(tray)) ShowWindow(tray, SW_SHOWNA);
    });
    if (HWND progman = FindWindowW(L"Progman", nullptr); progman && IsExplorerWindow(progman) && !IsWindowVisible(progman))
        ShowWindow(progman, SW_SHOWNA);
    // Pulpit z ikonami bywa w WorkerW (po zmianie tapety) — tylko ten z widokiem pulpitu; pozostałe WorkerW chowa sam Eksplorator
    EnumWindows([](HWND h, LPARAM) -> BOOL {
        wchar_t cls[16]{};
        if (GetClassNameW(h, cls, ARRAYSIZE(cls)) && wcscmp(cls, L"WorkerW") == 0 && IsExplorerWindow(h) && !IsWindowVisible(h) &&
            FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr))
            ShowWindow(h, SW_SHOWNA);
        return TRUE;
    }, 0);
}

bool IsConfiguredShell() {
    wchar_t value[1024]{}, self[MAX_PATH]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", L"Shell", RRF_RT_REG_SZ, nullptr,
                     value, &size) != ERROR_SUCCESS ||
        !GetModuleFileNameW(nullptr, self, MAX_PATH))
        return false;
    CharLowerW(value);
    CharLowerW(self);
    return wcsstr(value, self) != nullptr;
}

bool CloseExplorerShell() {
    std::vector<DWORD> pids;
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, L"Shell_TrayWnd", nullptr)) != nullptr) {
        DWORD pid = 0;
        if (IsExplorerWindow(h) && GetWindowThreadProcessId(h, &pid)) pids.push_back(pid);
    }
    for (DWORD pid : pids) {
        if (HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid)) {
            TerminateProcess(p, 1);  // kod 1 (jak taskkill /f) — przy 0 i -1 Windows sam uruchamia Eksplorator ponownie
            WaitForSingleObject(p, 5000);
            CloseHandle(p);
        }
    }
    return !pids.empty();
}
}  // namespace ExplorerUi
