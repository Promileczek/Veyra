#include <windows.h>
#include <objbase.h>
#include <dbghelp.h>
#include "core/Graphics.h"
#include "desktop/Desktop.h"
#include "files/FileWindow.h"
#include "shell/AltTab.h"
#include "shell/WindowDots.h"
#include "taskbar/Taskbar.h"
#include "topbar/TopBar.h"

// Awaria → %LOCALAPPDATA%\Exploder\crash.dmp (otwierany w Visual Studio / WinDbg razem z exploder.pdb).
// Windows i tak przywróci wtedy explorer.exe.
static LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* info) {
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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetUnhandledExceptionFilter(WriteCrashDump);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ExploderShell");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // już działa

    OleInitialize(nullptr);  // COM + schowek plików (kopiuj/wklej w Explodzie)
    int code = 1;
    {
        // Tryb powłoki: explorer.exe nie działa → my rysujemy pulpit i obsługujemy zasobnik.
        const bool shellMode = !FindWindowW(L"Shell_TrayWnd", nullptr) && !GetShellWindow();
        Graphics graphics;
        Desktop desktop;
        Taskbar taskbar;
        TopBar topBar;
        AltTab altTab;
        WindowDots windowDots;
        if (!graphics.Init()) {
            MessageBoxW(nullptr, L"Nie udało się zainicjować grafiki (Direct3D/Direct2D).", L"Exploder", MB_ICONERROR);
        } else {
            auto openFolder = [inst, &graphics](ComPtr<IShellItem> folder) { OpenFileWindow(inst, graphics, std::move(folder)); };
            if (shellMode && !desktop.Create(inst, graphics, openFolder)) desktop.Destroy();  // pulpit nieobowiązkowy
            topBar.Create(inst, graphics);  // nieobowiązkowy, jak pulpit
            taskbar.SetBeforeExplorer([&desktop, &topBar, &altTab, &windowDots] {
                windowDots.Uninstall();
                altTab.Uninstall();
                CloseAllFileWindows();
                topBar.Destroy();
                desktop.Destroy();
            });
            if (shellMode) altTab.Install(inst, graphics);  // bez explorera systemowy Alt+Tab jest w stylu Win95
            if (shellMode && windowDots.Install(inst, graphics))
                taskbar.SetWindowDotsToggle([&windowDots] { return windowDots.Enabled(); },
                                            [&windowDots](bool on) { windowDots.SetEnabled(on); });
            if (!taskbar.Create(inst, graphics)) {
                MessageBoxW(nullptr, L"Nie udało się utworzyć paska zadań.", L"Exploder", MB_ICONERROR);
            } else {
                MSG msg;
                while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                code = 0;
            }
        }
        windowDots.Uninstall();
        altTab.Uninstall();
        CloseAllFileWindows();
        topBar.Destroy();
        desktop.Destroy();
    }
    OleUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
