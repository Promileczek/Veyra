#include "WindowAnim.h"
#include "../core/Settings.h"
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {

constexpr wchar_t kClassName[] = L"ExploderWindowAnim";
constexpr double kMinimizeSeconds = 0.30;
constexpr double kRestoreSeconds = 0.26;

std::function<bool(HWND, RECT*)> g_iconRect;
bool g_installed = false;
bool g_disabledSystem = false;  // to my wyłączyliśmy systemową animację
HWINEVENTHOOK g_minimizeHook = nullptr;
HWINEVENTHOOK g_locationHook = nullptr;
std::unordered_map<HWND, RECT> g_rects;  // ostatni prostokąt okna przed zminimalizowaniem (wątek UI)

// Animacja w toku dla okna — nowa (np. przywrócenie w trakcie minimalizowania) przerywa poprzednią
std::mutex g_mutex;
std::unordered_map<HWND, std::shared_ptr<std::atomic<bool>>> g_running;

struct Item {
    HWND hwnd;
    RECT window;  // pełny prostokąt okna (GetWindowRect — miniatura DWM obejmuje też niewidoczne ramki)
    RECT icon;
};

bool Usable(HWND h) {
    if (GetAncestor(h, GA_ROOT) != h) return false;
    wchar_t cls[64]{};
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    return wcscmp(cls, kClassName) != 0;
}

void Remember(HWND h) {
    if (!IsWindowVisible(h) || IsIconic(h)) return;
    RECT r{};
    if (GetWindowRect(h, &r) && r.right - r.left > 0 && r.bottom - r.top > 0) g_rects[h] = r;
}

// Prostokąt okna po przywróceniu: zapamiętany, a gdy go brak — z WINDOWPLACEMENT
bool RestoredRect(HWND h, RECT* out) {
    if (auto it = g_rects.find(h); it != g_rects.end()) {
        *out = it->second;
        return true;
    }
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (!GetWindowPlacement(h, &wp)) return false;
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    if (wp.flags & WPF_RESTORETOMAXIMIZED) {
        *out = mi.rcWork;
        return true;
    }
    *out = wp.rcNormalPosition;  // współrzędne obszaru roboczego (poza oknami narzędziowymi)
    if (!(GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
        OffsetRect(out, mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top);
    return true;
}

double EaseInOutCubic(double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }
double EaseOutQuad(double t) { return 1 - (1 - t) * (1 - t); }

// Klatka dla postępu p (0 = okno, 1 = ikona): położenie płynnie, rozmiar maleje szybciej (kopia „wsysana” do ikony),
// pod koniec zanika
RECT Frame(const Item& it, double p, BYTE* opacity) {
    const double ww = it.window.right - it.window.left, wh = it.window.bottom - it.window.top;
    const double iw = it.icon.right - it.icon.left, ih = it.icon.bottom - it.icon.top;
    const double fit = std::min(iw / ww, ih / wh);  // kopia na końcu mieści się w ikonie (z zachowaniem proporcji)
    const double move = EaseInOutCubic(p), size = EaseOutQuad(p);
    const double scale = 1 + (fit - 1) * size;
    const double cx0 = (it.window.left + it.window.right) / 2.0, cy0 = (it.window.top + it.window.bottom) / 2.0;
    const double cx1 = (it.icon.left + it.icon.right) / 2.0, cy1 = (it.icon.top + it.icon.bottom) / 2.0;
    const double cx = cx0 + (cx1 - cx0) * move, cy = cy0 + (cy1 - cy0) * move;
    const double w = ww * scale, h = wh * scale;
    *opacity = static_cast<BYTE>(255 * (p < 0.55 ? 1.0 : std::max(0.0, 1 - (p - 0.55) / 0.45)));
    return {static_cast<LONG>(std::lround(cx - w / 2)), static_cast<LONG>(std::lround(cy - h / 2)),
            static_cast<LONG>(std::lround(cx + w / 2)), static_cast<LONG>(std::lround(cy + h / 2))};
}

void RegisterClassOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
    });
}

// Wątek animacji: własne przezroczyste okno (bez powierzchni — widać tylko miniatury), klatki co odświeżenie DWM
void Run(std::vector<Item> items, bool restore, bool activate, std::shared_ptr<std::atomic<bool>> cancel) {
    RECT area = items[0].window;
    for (const Item& it : items) {
        UnionRect(&area, &area, &it.window);
        UnionRect(&area, &area, &it.icon);
    }
    RegisterClassOnce();
    const HWND overlay = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP,
        kClassName, L"", WS_POPUP, area.left, area.top, area.right - area.left, area.bottom - area.top, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    std::vector<HTHUMBNAIL> thumbs(items.size(), nullptr);
    if (overlay) {
        SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA);
        for (size_t i = 0; i < items.size(); ++i) DwmRegisterThumbnail(overlay, items[i].hwnd, &thumbs[i]);
        auto draw = [&](double p) {
            for (size_t i = 0; i < items.size(); ++i) {
                if (!thumbs[i]) continue;
                DWM_THUMBNAIL_PROPERTIES props{};
                props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY | DWM_TNP_VISIBLE;
                props.rcDestination = Frame(items[i], p, &props.opacity);
                OffsetRect(&props.rcDestination, -area.left, -area.top);
                props.fVisible = TRUE;
                DwmUpdateThumbnailProperties(thumbs[i], &props);
            }
        };
        draw(restore ? 1.0 : 0.0);
        ShowWindow(overlay, SW_SHOWNOACTIVATE);
        LARGE_INTEGER freq, start, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        const double seconds = restore ? kRestoreSeconds : kMinimizeSeconds;
        for (;;) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            QueryPerformanceCounter(&now);
            const double t = std::min(1.0, double(now.QuadPart - start.QuadPart) / double(freq.QuadPart) / seconds);
            if (*cancel || t >= 1.0) break;
            draw(restore ? 1.0 - t : t);
            DwmFlush();  // następna klatka kompozycji
        }
        if (restore && !*cancel) draw(0.0);
    }
    if (restore && !*cancel) {
        for (const Item& it : items) ShowWindowAsync(it.hwnd, SW_RESTORE);
        if (activate && !SetForegroundWindow(items.back().hwnd)) SwitchToThisWindow(items.back().hwnd, TRUE);
        DwmFlush();  // okno zdąży się narysować pod kopią
        DwmFlush();
    }
    for (HTHUMBNAIL t : thumbs)
        if (t) DwmUnregisterThumbnail(t);
    if (overlay) DestroyWindow(overlay);
    std::lock_guard lock(g_mutex);
    for (const Item& it : items)
        if (auto f = g_running.find(it.hwnd); f != g_running.end() && f->second == cancel) g_running.erase(f);
}

void Start(std::vector<Item> items, bool restore, bool activate) {
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard lock(g_mutex);
        for (const Item& it : items) {
            auto& slot = g_running[it.hwnd];
            if (slot) *slot = true;
            slot = cancel;
        }
    }
    std::thread(Run, std::move(items), restore, activate, std::move(cancel)).detach();
}

void CALLBACK OnLocation(HWINEVENTHOOK, DWORD, HWND h, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF && h && Usable(h)) Remember(h);
}

void CALLBACK OnMinimize(HWINEVENTHOOK, DWORD, HWND h, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !h || !Usable(h)) return;
    Item it{h};
    if (!RestoredRect(h, &it.window) || !g_iconRect || !g_iconRect(h, &it.icon)) return;
    Start({it}, false, false);
    if (g_rects.size() > 512) std::erase_if(g_rects, [](const auto& e) { return !IsWindow(e.first); });
}

}  // namespace

namespace WindowAnim {

void Install(std::function<bool(HWND, RECT*)> iconRect) {
    if (g_installed || !Settings::GetBool(L"windowAnimations", true)) return;
    ANIMATIONINFO ai{sizeof(ai)};
    if (!SystemParametersInfoW(SPI_GETANIMATION, sizeof(ai), &ai, 0) || !ai.iMinAnimate) return;  // wyłączone w Windows
    ai.iMinAnimate = 0;
    if (!SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, 0)) return;  // bez zapisu do profilu
    g_disabledSystem = true;
    g_iconRect = std::move(iconRect);
    EnumWindows([](HWND h, LPARAM) -> BOOL {
        if (Usable(h)) Remember(h);
        return TRUE;
    }, 0);
    g_minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZESTART, nullptr, OnMinimize, 0, 0,
                                     WINEVENT_OUTOFCONTEXT);
    g_locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnLocation, 0, 0,
                                     WINEVENT_OUTOFCONTEXT);
    g_installed = true;
}

void Uninstall() {
    if (g_minimizeHook) UnhookWinEvent(g_minimizeHook);
    if (g_locationHook) UnhookWinEvent(g_locationHook);
    g_minimizeHook = g_locationHook = nullptr;
    g_installed = false;
    {
        std::lock_guard lock(g_mutex);
        for (auto& [h, cancel] : g_running) *cancel = true;
    }
    RestoreSystemAnimation();
}

void RestoreSystemAnimation() {
    if (!g_disabledSystem) return;
    ANIMATIONINFO ai{sizeof(ai), 1};
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, 0);
    g_disabledSystem = false;
}

void Restore(const std::vector<HWND>& windows, bool activate) {
    std::vector<Item> items;
    for (HWND h : windows) {
        Item it{h};
        if (g_installed && IsIconic(h) && RestoredRect(h, &it.window) && g_iconRect && g_iconRect(h, &it.icon))
            items.push_back(it);
    }
    if (items.size() != windows.size()) {  // bez animacji — jak dotąd
        for (HWND h : windows)
            if (IsIconic(h)) ShowWindowAsync(h, SW_RESTORE);
        if (activate && !windows.empty() && !SetForegroundWindow(windows.back())) SwitchToThisWindow(windows.back(), TRUE);
        return;
    }
    if (!items.empty()) Start(std::move(items), true, activate);
}

}  // namespace WindowAnim
