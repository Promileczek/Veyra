#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <vector>

struct TrayIcon {
    HWND owner = nullptr;
    UINT id = 0;
    GUID guid{};
    bool hasGuid = false;
    UINT callback = 0;
    UINT version = 0;
    bool hidden = false;
    HICON icon = nullptr;  // własna kopia (CopyIcon), zwalniana przy usunięciu
    std::wstring tip;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;  // cache paska; zerowany przy zmianie ikony
};

// Zasobnik systemowy: ukryte okno "Shell_TrayWnd", do którego programy wysyłają swoje ikony
// (Shell_NotifyIcon → WM_COPYDATA). Działa tylko gdy explorer.exe nie jest uruchomiony —
// inaczej to explorer dostaje ikony.
class TrayService {
public:
    using ChangeFn = std::function<void()>;

    bool Start(HINSTANCE inst, ChangeFn onChange);  // false = explorer działa albo błąd
    void Stop();
    bool Active() const { return hwnd_ != nullptr; }

    std::vector<TrayIcon>& Icons() { return icons_; }
    const std::vector<TrayIcon>& Icons() const { return icons_; }

    bool Prune();  // usuwa ikony programów, które zniknęły bez NIM_DELETE (np. crash)
    void SendMouse(const TrayIcon& icon, UINT mouseMsg, POINT screenPt) const;
    void SetRect(const RECT& rc);  // niektóre programy pytają o położenie paska przez GetWindowRect

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    BOOL OnCopyData(const COPYDATASTRUCT* cds);
    TrayIcon* Find(HWND owner, UINT id, const GUID* guid);

    HWND hwnd_ = nullptr;
    std::vector<TrayIcon> icons_;
    ChangeFn onChange_;
};
