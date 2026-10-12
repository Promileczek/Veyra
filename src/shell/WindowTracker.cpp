#include "WindowTracker.h"
#include <dwmapi.h>
#include <algorithm>

WindowTracker* WindowTracker::instance_ = nullptr;

namespace {
std::wstring GetTitle(HWND h) {
    // Dla okien innych procesów GetWindowText nie wysyła komunikatu, więc nie zawiesi się na zawieszonym oknie.
    const int len = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(len), L'\0');
    if (len > 0) s.resize(static_cast<size_t>(GetWindowTextW(h, s.data(), len + 1)));
    return s;
}
}  // namespace

bool WindowTracker::IsTaskbarWindow(HWND h) {
    if (!IsWindow(h) || !IsWindowVisible(h)) return false;
    if (GetAncestor(h, GA_PARENT) != GetDesktopWindow()) return false;
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    const bool appWindow = (ex & WS_EX_APPWINDOW) != 0;
    if (!appWindow && GetWindow(h, GW_OWNER)) return false;
    if (!appWindow && (ex & WS_EX_NOACTIVATE)) return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    wchar_t cls[64]{};
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    for (const wchar_t* skip : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        if (wcscmp(cls, skip) == 0) return false;
    return true;
}

void WindowTracker::Start(HWND owner, ChangeFn onChange) {
    instance_ = this;
    owner_ = owner;
    onChange_ = std::move(onChange);

    shellMsg_ = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(owner_);

    const DWORD ranges[][2] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},  // DESTROY, SHOW, HIDE
        {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    for (const auto& r : ranges)
        if (HWINEVENTHOOK hook = SetWinEventHook(r[0], r[1], nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT))
            hooks_.push_back(hook);

    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto* self = reinterpret_cast<WindowTracker*>(p);
            if (IsTaskbarWindow(h)) self->windows_.push_back({h, GetTitle(h)});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    std::reverse(windows_.begin(), windows_.end());  // najstarsze (najniżej w Z-order) z lewej
    active_ = GetForegroundWindow();
}

void WindowTracker::Stop() {
    for (HWINEVENTHOOK hook : hooks_) UnhookWinEvent(hook);
    hooks_.clear();
    if (owner_) DeregisterShellHookWindow(owner_);
    instance_ = nullptr;
}

bool WindowTracker::IsActive(HWND h) const {
    return active_ && (active_ == h || GetAncestor(active_, GA_ROOTOWNER) == h);
}

void CALLBACK WindowTracker::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject,
                                        LONG idChild, DWORD, DWORD) {
    if (!instance_ || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    instance_->OnEvent(event, hwnd);
}

void WindowTracker::OnEvent(DWORD event, HWND h) {
    bool changed = false;
    switch (event) {
    case EVENT_SYSTEM_FOREGROUND:
        if (h == owner_) break;  // kliknięcie w pasek / menu kontekstowe
        if (active_ != h) {
            active_ = h;
            changed = true;
        }
        changed |= Update(h);
        if (TrackedWindow* w = Find(GetAncestor(h, GA_ROOTOWNER)); w && w->flashing) {
            w->flashing = false;
            changed = true;
        }
        break;
    case EVENT_OBJECT_DESTROY:
        changed = Remove(h);
        break;
    default:
        changed = Update(h);
        break;
    }
    if (changed && onChange_) onChange_();
}

bool WindowTracker::HandleShellHook(WPARAM code, LPARAM lParam) {
    const HWND h = reinterpret_cast<HWND>(lParam);
    switch (code) {
    case HSHELL_FLASH:
        if (TrackedWindow* w = Find(h); w && !IsActive(h)) {
            w->flashing = true;
            return true;
        }
        return false;
    case HSHELL_REDRAW:
        if (TrackedWindow* w = Find(h)) {
            w->iconDirty = true;
            w->title = GetTitle(h);
            return true;
        }
        return false;
    case HSHELL_WINDOWCREATED:
    case HSHELL_WINDOWREPLACED:
        return Update(h);
    default:
        return false;
    }
}

bool WindowTracker::Update(HWND h) {
    const bool should = IsTaskbarWindow(h);
    TrackedWindow* w = Find(h);
    if (should && !w) {
        windows_.push_back({h, GetTitle(h)});
        return true;
    }
    if (!should && w) return Remove(h);
    if (w) {
        std::wstring title = GetTitle(h);
        if (title != w->title) {
            w->title = std::move(title);
            return true;
        }
    }
    return false;
}

bool WindowTracker::Remove(HWND h) {
    return std::erase_if(windows_, [h](const TrackedWindow& w) { return w.hwnd == h; }) > 0;
}

TrackedWindow* WindowTracker::Find(HWND h) {
    auto it = std::find_if(windows_.begin(), windows_.end(), [h](const TrackedWindow& w) { return w.hwnd == h; });
    return it == windows_.end() ? nullptr : &*it;
}
