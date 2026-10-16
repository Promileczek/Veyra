#include <windows.h>
#include <objbase.h>
#include <dbghelp.h>
#include <string>
#include "core/Edition.h"
#include "core/Environment.h"
#include "core/Graphics.h"
#include "core/Settings.h"
#include "desktop/Desktop.h"
#include "files/FileWindow.h"
#include "shell/AltTab.h"
#include "shell/ExplorerUi.h"
#include "shell/Notification.h"
#include "shell/NowPlayingHost.h"
#include "shell/WindowAnim.h"
#include "core/UiScale.h"
#include "shell/WindowDots.h"
#include "shell/VeyraCursor.h"
#include "taskbar/Taskbar.h"
#include "topbar/TopBar.h"

// Awaria → %LOCALAPPDATA%\Exploder\crash.dmp (otwierany w Visual Studio / WinDbg razem z exploder.pdb).
// Windows i tak przywróci wtedy explorer.exe.
static LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* info) {
    VeyraCursor::Restore();  // przywróć systemowe kursory
    if (Edition::Minimal()) ExplorerUi::Restore();  // pasek i pulpit Eksploratora z powrotem
    WindowAnim::RestoreSystemAnimation();
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    wcscpy_s(path + n, MAX_PATH - n, L"\\Exploder\\crash.dmp");
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithIndirectlyReferencedMemory, &mei, nullptr,
                          nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Minimal: strażnik czeka na koniec powłoki; gdy zginęła (Menedżer zadań, kod różny od 0) — pasek i pulpit Eksploratora
// z powrotem (po zwykłym zamknięciu Veyra robi to sama)
static int Watchdog(DWORD pid) {
    const HANDLE shell = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!shell) return 0;
    WaitForSingleObject(shell, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(shell, &code);
    CloseHandle(shell);
    if (code != 0) ExplorerUi::RestoreAfterKill();
    return 0;
}

static void StartWatchdog() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --straznik " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsncmp(cmdLine, L"--straznik ", 11) == 0) return Watchdog(static_cast<DWORD>(_wtoi(cmdLine + 11)));
    SetUnhandledExceptionFilter(WriteCrashDump);
    Environment::ResetFromUser();  // nie przekazujemy programom zmiennych tego, kto nas uruchomił
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ExploderShell");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // już działa

    OleInitialize(nullptr);  // COM + schowek plików (kopiuj/wklej w Explodzie)
    // Pierwsze uruchomienie: domyślna tapeta Veyry (potem użytkownik zmienia ją w Ustawieniach — już jej nie nadpisujemy)
    if (!Settings::GetBool(L"defaultWallpaperApplied", false)) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring wallpaper = exe;
        wallpaper = wallpaper.substr(0, wallpaper.find_last_of(L'\\') + 1) + L"wallpapers\\veyra-domyslna.jpg";
        wchar_t previous[MAX_PATH]{};  // tapeta sprzed Veyry → Ustawienia → Twoje tapety
        if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, previous, 0) && *previous && !wcsstr(previous, L"veyra-"))
            Settings::Set(L"previousWallpaper", previous);
        if (GetFileAttributesW(wallpaper.c_str()) != INVALID_FILE_ATTRIBUTES &&
            SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, wallpaper.data(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
            Settings::SetBool(L"defaultWallpaperApplied", true);
    }
    int code = 1;
    {
        // Tryb powłoki: explorer.exe nie działa → my rysujemy pulpit i obsługujemy zasobnik.
        // Minimal: explorer.exe w tle (UWP, powiadomienia), jego pasek i pulpit chowamy — poza tym jak tryb powłoki.
        // Full jako powłoka Windows: Eksplorator, który wszedł na jej miejsce (np. przy aktualizacji), zamykamy —
        // przy starcie i jeszcze przez minutę (potrafi wrócić z opóźnieniem)
        if (!Edition::Minimal() && ExplorerUi::IsConfiguredShell()) {
            ExplorerUi::CloseExplorerShell();
            SetTimer(nullptr, 0, 2000, [](HWND, UINT, UINT_PTR id, DWORD) {
                static int ticks = 0;
                ExplorerUi::CloseExplorerShell();
                if (++ticks >= 30) KillTimer(nullptr, id);
            });
        }
        const bool minimal = Edition::Minimal() && ExplorerUi::EnsureRunning();
        if (minimal) {
            StartWatchdog();
            ExplorerUi::Hide();
            SetTimer(nullptr, 0, 1000, [](HWND, UINT, UINT_PTR, DWORD) { ExplorerUi::KeepHidden(); });
        }
        const bool shellMode = minimal || (!FindWindowW(L"Shell_TrayWnd", nullptr) && !GetShellWindow());
        // Full bez Eksploratora: usługa „teraz odtwarzane” (GSMTC) w naszym procesie — przed górnym paskiem
        if (shellMode && !minimal && Settings::GetBool(L"nowPlayingHost", true)) NowPlayingHost::Start();
        Graphics graphics;
        Desktop desktop;
        Taskbar taskbar;
        TopBar topBar;
        AltTab altTab;
        WindowDots windowDots;
        VeyraCursor veyraCursor;
        UpdateWatcher updates;
        if (!graphics.Init()) {
            MessageBoxW(nullptr, L"Nie udało się zainicjować grafiki (Direct3D/Direct2D).", L"Exploder", MB_ICONERROR);
        } else {
            veyraCursor.Install(inst);
            auto openFolder = [inst, &graphics](ComPtr<IShellItem> folder) {
                if (Edition::Minimal()) Edition::OpenInExplorer(folder.Get());  // Minimal: bez okien "Pliki"
                else OpenFileWindow(inst, graphics, std::move(folder));
            };
            if (shellMode && !desktop.Create(inst, graphics, openFolder)) desktop.Destroy();  // pulpit nieobowiązkowy
            topBar.Create(inst, graphics);  // nieobowiązkowy, jak pulpit
            taskbar.SetBeforeExplorer([&desktop, &topBar, &altTab, &windowDots, &veyraCursor] {
                veyraCursor.Uninstall();
                windowDots.Uninstall();
                altTab.Uninstall();
                CloseAllFileWindows();
                topBar.Destroy();
                desktop.Destroy();
                NowPlayingHost::Stop();  // Eksplorator jako powłoka uruchamia własną
            });
            if (shellMode) altTab.Install(inst, graphics);  // bez explorera systemowy Alt+Tab jest w stylu Win95
            taskbar.SetOnSettingsChanged([&windowDots, &veyraCursor] {
                veyraCursor.ReloadSettings();
                if (Edition::Minimal()) return;  // Minimal: bez kropek
                if (windowDots.Enabled() != Settings::GetBool(L"windowDots", true)) windowDots.SetEnabled(!windowDots.Enabled());
            });
            if (shellMode && !Edition::Minimal() && windowDots.Install(inst, graphics))
                taskbar.SetWindowDotsToggle([&windowDots] { return windowDots.Enabled(); },
                                            [&windowDots](bool on) { windowDots.SetEnabled(on); });
            if (!taskbar.Create(inst, graphics)) {
                MessageBoxW(nullptr, L"Nie udało się utworzyć paska zadań.", L"Exploder", MB_ICONERROR);
            } else {
                updates.Start(inst, graphics, [] { return UiScale::Dpi(GetDpiForSystem()); });  // powiadomienie o nowej wersji
                MSG msg;
                while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                code = 0;
            }
        }
        updates.Stop();
        veyraCursor.Uninstall();
        windowDots.Uninstall();
        altTab.Uninstall();
        CloseAllFileWindows();
        topBar.Destroy();
        desktop.Destroy();
        NowPlayingHost::Stop();
    }
    if (Edition::Minimal()) ExplorerUi::Restore();
    OleUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
