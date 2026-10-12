#include "TrayService.h"
#include <shellapi.h>
#include <algorithm>
#include <cstring>

namespace {
// Układ danych, który shell32 wysyła do Shell_TrayWnd: uchwyty jako DWORD (zgodność 32/64 bit).
struct NotifyIconData32 {
    DWORD cbSize;
    DWORD hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    DWORD hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    UINT uTimeoutOrVersion;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    DWORD hBalloonIcon;
};
struct TrayData {
    DWORD signature;
    DWORD message;
    NotifyIconData32 nid;
};
static_assert(sizeof(NotifyIconData32) == 956, "musi odpowiadać 32-bitowemu NOTIFYICONDATAW");

constexpr DWORD kTraySignature = 0x34753423;
constexpr ULONG_PTR kCopyDataNotifyIcon = 1;
constexpr wchar_t kTrayClass[] = L"Shell_TrayWnd";

template <typename H>
H FromHandle32(DWORD v) {
    return reinterpret_cast<H>(static_cast<LONG_PTR>(static_cast<LONG>(v)));  // uchwyty USER: rozszerzenie znaku
}
}  // namespace

bool TrayService::Start(HINSTANCE inst, ChangeFn onChange) {
    if (FindWindowW(kTrayClass, nullptr)) return false;  // explorer.exe działa
    onChange_ = std::move(onChange);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kTrayClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kTrayClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            inst, nullptr);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Po tym komunikacie programy dodają swoje ikony od nowa (jak po restarcie explorera).
    SendNotifyMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
    return true;
}

void TrayService::Stop() {
    for (TrayIcon& i : icons_)
        if (i.icon) DestroyIcon(i.icon);
    icons_.clear();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

LRESULT CALLBACK TrayService::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COPYDATA)
        if (auto* self = reinterpret_cast<TrayService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
            return self->OnCopyData(reinterpret_cast<const COPYDATASTRUCT*>(lp));
    return DefWindowProcW(hwnd, msg, wp, lp);
}

TrayIcon* TrayService::Find(HWND owner, UINT id, const GUID* guid) {
    for (TrayIcon& i : icons_) {
        if (guid ? (i.hasGuid && IsEqualGUID(i.guid, *guid)) : (i.owner == owner && i.id == id)) return &i;
    }
    return nullptr;
}

BOOL TrayService::OnCopyData(const COPYDATASTRUCT* cds) {
    // Inne dwData to m.in. komunikaty AppBar (0) i pytania o położenie ikony (3) — nieobsługiwane.
    if (!cds || cds->dwData != kCopyDataNotifyIcon || !cds->lpData ||
        cds->cbData < offsetof(TrayData, nid) + offsetof(NotifyIconData32, szTip))
        return FALSE;
    TrayData data{};
    std::memcpy(&data, cds->lpData, std::min<size_t>(cds->cbData, sizeof(data)));
    if (data.signature != kTraySignature) return FALSE;
    NotifyIconData32& n = data.nid;

    const HWND owner = FromHandle32<HWND>(n.hWnd);
    const bool byGuid = (n.uFlags & NIF_GUID) != 0;
    TrayIcon* icon = Find(owner, n.uID, byGuid ? &n.guidItem : nullptr);

    switch (data.message) {
    case NIM_ADD:
        if (icon) return FALSE;
        icon = &icons_.emplace_back();
        icon->owner = owner;
        icon->id = n.uID;
        icon->hasGuid = byGuid;
        if (byGuid) icon->guid = n.guidItem;
        break;
    case NIM_MODIFY:
        if (!icon) return FALSE;
        break;
    case NIM_DELETE:
        if (!icon) return FALSE;
        if (icon->icon) DestroyIcon(icon->icon);
        icons_.erase(icons_.begin() + (icon - icons_.data()));
        if (onChange_) onChange_();
        return TRUE;
    case NIM_SETFOCUS:
        return TRUE;
    case NIM_SETVERSION:
        if (!icon) return FALSE;
        icon->version = n.uTimeoutOrVersion;
        return TRUE;
    default:
        return FALSE;
    }

    if (n.uFlags & NIF_MESSAGE) icon->callback = n.uCallbackMessage;
    if (n.uFlags & NIF_ICON) {
        if (icon->icon) DestroyIcon(icon->icon);
        icon->icon = n.hIcon ? CopyIcon(FromHandle32<HICON>(n.hIcon)) : nullptr;
        icon->bitmap.Reset();
    }
    if (n.uFlags & NIF_TIP) {
        n.szTip[ARRAYSIZE(n.szTip) - 1] = L'\0';
        icon->tip = n.szTip;
    }
    if ((n.uFlags & NIF_STATE) && (n.dwStateMask & NIS_HIDDEN)) icon->hidden = (n.dwState & NIS_HIDDEN) != 0;
    // NIF_INFO (dymki/powiadomienia) — obsłuży moduł powiadomień.
    if (onChange_) onChange_();
    return TRUE;
}

bool TrayService::Prune() {
    return std::erase_if(icons_, [](TrayIcon& i) {
               if (IsWindow(i.owner)) return false;
               if (i.icon) DestroyIcon(i.icon);
               return true;
           }) > 0;
}

void TrayService::SendMouse(const TrayIcon& icon, UINT mouseMsg, POINT pt) const {
    if (!icon.callback || !IsWindow(icon.owner)) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(icon.owner, &pid);
    AllowSetForegroundWindow(pid);  // żeby program mógł pokazać swoje menu/okno na wierzchu
    if (icon.version >= NOTIFYICON_VERSION_4)
        PostMessageW(icon.owner, icon.callback, MAKEWPARAM(pt.x, pt.y), MAKELPARAM(mouseMsg, icon.id));
    else
        PostMessageW(icon.owner, icon.callback, icon.id, mouseMsg);
}

void TrayService::SetRect(const RECT& rc) {
    if (hwnd_)
        SetWindowPos(hwnd_, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
}
