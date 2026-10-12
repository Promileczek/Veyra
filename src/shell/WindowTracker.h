#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

struct TrackedWindow {
    HWND hwnd = nullptr;
    std::wstring title;
    bool flashing = false;
    bool iconDirty = true;
};

// Śledzi okna, które powinny mieć przycisk na pasku zadań.
// Zdarzenia: WinEvent hooki (pokaż/ukryj/zniszcz/tytuł/cloak/foreground) + shell hook (miganie, ikona).
class WindowTracker {
public:
    using ChangeFn = std::function<void()>;

    void Start(HWND owner, ChangeFn onChange);
    void Stop();

    UINT ShellHookMessage() const { return shellMsg_; }
    bool HandleShellHook(WPARAM code, LPARAM lParam);  // true = lista się zmieniła

    std::vector<TrackedWindow>& Windows() { return windows_; }
    const std::vector<TrackedWindow>& Windows() const { return windows_; }
    bool IsActive(HWND h) const;

    static bool IsTaskbarWindow(HWND h);

private:
    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject,
                                    LONG idChild, DWORD, DWORD);
    void OnEvent(DWORD event, HWND hwnd);
    bool Update(HWND h);
    bool Remove(HWND h);
    TrackedWindow* Find(HWND h);

    static WindowTracker* instance_;
    HWND owner_ = nullptr;
    HWND active_ = nullptr;
    UINT shellMsg_ = 0;
    std::vector<HWINEVENTHOOK> hooks_;
    std::vector<TrackedWindow> windows_;
    ChangeFn onChange_;
};
