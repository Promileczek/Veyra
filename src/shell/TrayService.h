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
// (Shell_NotifyIcon → WM_COPYDATA). Bez explorer.exe jesteśmy jedynym zasobnikiem. W Veyrze Minimal explorer działa
// w tle: nasze okno jest pierwsze w kolejności (programy szukają go przez FindWindow), a każdy WM_COPYDATA
// (ikony, komunikaty AppBar, położenie ikon) przekazujemy dalej do Eksploratora.
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
    HWND ExplorerTray() const;  // Shell_TrayWnd Eksploratora (Minimal), nullptr = brak
    void EnsureFirst();         // nasze okno przed Eksploratorem; inaczej ikony trafiają tylko do niego
    void BroadcastTaskbarCreated();
    BOOL OnCopyData(const COPYDATASTRUCT* cds);
    TrayIcon* Find(HWND owner, UINT id, const GUID* guid);

    HWND hwnd_ = nullptr;
    bool selfBroadcast_ = false;
    std::vector<TrayIcon> icons_;
    ChangeFn onChange_;
};
