#include "WindowDots.h"
#include "../core/Settings.h"
#include "../core/WorkArea.h"
#include "../files/FileWindow.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace {
constexpr wchar_t kClassName[] = L"ExploderWindowDots";
constexpr UINT_PTR kResampleTimer = 1;
WindowDots* g_instance = nullptr;

// Rodzaj okna: zwykłe (przyciski od DWM) albo Chromium/Electron z własnym paskiem tytułu
enum class Kind { None, Standard, Browser, Discord, Electron };

std::wstring ExeName(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return {};
    wchar_t path[MAX_PATH * 2]{};
    DWORD len = ARRAYSIZE(path);
    const BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &len);
    CloseHandle(proc);
    if (!ok) return {};
    std::wstring name(path, len);
    name = name.substr(name.find_last_of(L'\\') + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return name;
}

Kind KindOf(HWND h) {
    wchar_t cls[64]{};
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    if (wcsncmp(cls, L"Chrome_WidgetWin_", 17) == 0) {  // Chromium/Electron rysują własny pasek tytułu
        const std::wstring exe = ExeName(h);
        if (exe == L"brave.exe" || exe == L"chrome.exe" || exe == L"msedge.exe" || exe == L"opera.exe" || exe == L"vivaldi.exe")
            return Kind::Browser;
        if (exe == L"discord.exe") return Kind::Discord;
        const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
        // Electron z natywną ramką — przyciski od DWM, jak w zwykłym oknie
        if ((style & WS_SYSMENU) && (style & WS_CAPTION) == WS_CAPTION) return Kind::Standard;
        // Bez ramki, własne przyciski w prawym górnym rogu (Spotify, VS Code…): tylko okno główne z minimalizacją
        // (nie dymki, menu ani okna podrzędne)
        if ((style & WS_MINIMIZEBOX) && !(GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) && !GetWindow(h, GW_OWNER))
            return Kind::Electron;
        return Kind::None;
    }
    const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    return (style & WS_SYSMENU) && (style & WS_CAPTION) == WS_CAPTION ? Kind::Standard : Kind::None;
}

// Środek pierwszej kropki: grupa przy prawej krawędzi, w miejscu przycisku "zamknij"
// dy: przesunięcie w dół (Brave/Chrome — kropki na wysokości kart, nie przy samej krawędzi)
D2D1_POINT_2F DotsOrigin(float w, float h, float k, float dy) { return {w - 18.f * k - 40.f * k, h / 2 + dy}; }

// hWndInsertAfter, żeby nakładka leżała tuż nad oknem; nullptr = już tam jest
HWND InsertAfterFor(HWND target, HWND self) {
    const HWND prev = GetWindow(target, GW_HWNDPREV);
    if (prev == self) return nullptr;
    if (GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_TOPMOST) return HWND_TOPMOST;
    // Za oknem "zawsze na wierzchu" nakładka sama stałaby się "na wierzchu" — wtedy na szczyt zwykłych okien
    if (!prev || (GetWindowLongPtrW(prev, GWL_EXSTYLE) & WS_EX_TOPMOST)) return HWND_TOP;
    return prev;
}
}  // namespace

bool WindowDots::Install(HINSTANCE inst, Graphics& g) {
    inst_ = inst;
    g_ = &g;
    g_instance = this;
    enabled_ = Settings::GetBool(L"windowDots", true);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;
    systemHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MINIMIZEEND, nullptr, OnEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    objectHook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, OnEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    locationHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (enabled_) ScanAll();
    // Co 1,5 s: nowe okna bez zdarzenia, schowane nakładki, które już mogą się pokazać (np. okno Chromium było
    // ukryte/w animacji, gdy przyszło zdarzenie), martwe okna
    timer_ = SetTimer(nullptr, 0, 1500, [](HWND, UINT, UINT_PTR, DWORD) {
        if (g_instance) g_instance->Periodic();
    });
    return true;
}

void WindowDots::Periodic() {
    if (!enabled_) return;
    for (size_t i = overlays_.size(); i-- > 0;)
        if (!IsWindow(overlays_[i]->target) || !IsWindowVisible(overlays_[i]->target)) Drop(overlays_[i]->target);
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            reinterpret_cast<WindowDots*>(p)->Track(h);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    RECT area{};
    float scale = 1.f;
    for (auto& o : overlays_)
        if (!o->moving && !IsWindowVisible(o->hwnd) && ButtonArea(o->target, area, scale)) Update(*o);
    Restack();  // okno mogło wyjść nad nakładkę bez zmiany aktywnego okna (kropki „znikały”)
}

void WindowDots::Uninstall() {
    if (timer_) KillTimer(nullptr, timer_);
    timer_ = 0;
    for (HWINEVENTHOOK* h : {&systemHook_, &objectHook_, &locationHook_})
        if (*h) {
            UnhookWinEvent(*h);
            *h = nullptr;
        }
    for (auto& o : overlays_)
        if (o->hwnd) DestroyWindow(o->hwnd);
    overlays_.clear();
    g_instance = nullptr;
}

void WindowDots::SetEnabled(bool on) {
    enabled_ = on;
    Settings::SetBool(L"windowDots", on);
    if (on) {
        ScanAll();
    } else {
        for (auto& o : overlays_)
            if (o->hwnd) DestroyWindow(o->hwnd);
        overlays_.clear();
    }
}

void CALLBACK WindowDots::OnEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (g_instance && hwnd && idObject == OBJID_WINDOW && idChild == CHILDID_SELF) g_instance->HandleEvent(event, hwnd, idObject);
}

void WindowDots::HandleEvent(DWORD event, HWND h, LONG) {
    if (!enabled_) return;
    Overlay* o = Find(h);
    switch (event) {
    case EVENT_SYSTEM_FOREGROUND:
        if (!o) Track(GetAncestor(h, GA_ROOT));
        else if (!o->moving) Update(*o);  // mogła zostać schowana (okno w trakcie pokazywania/animacji)
        // Aktywne/nieaktywne okna mają inne kolory paska — nowa próbka dla wszystkich
        for (auto& ov : overlays_) StartResample(*ov);
        Restack();
        break;
    case EVENT_SYSTEM_MOVESIZESTART:  // przeciąganie / zmiana rozmiaru: kropki jadą z oknem (Follow), kolor po puszczeniu
        if (o) o->moving = true;
        break;
    case EVENT_SYSTEM_MOVESIZEEND:
        if (o) {
            o->moving = false;
            Update(*o);
        }
        break;
    case EVENT_SYSTEM_MINIMIZESTART:
        if (o) Hide(*o);
        break;
    case EVENT_SYSTEM_MINIMIZEEND:
    case EVENT_OBJECT_LOCATIONCHANGE:  // maksymalizacja, przywrócenie, przesunięcie (też myszą), wyjście z pełnego ekranu
        if (o && o->moving) Follow(*o);
        else if (o) Update(*o);
        break;
    case EVENT_OBJECT_SHOW:
        if (o) Update(*o);
        else Track(h);
        break;
    case EVENT_OBJECT_DESTROY:
    case EVENT_OBJECT_HIDE:
        if (o) Drop(h);
        break;
    }
}

void WindowDots::ScanAll() {
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            reinterpret_cast<WindowDots*>(p)->Track(h);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    Restack();
}

WindowDots::Overlay* WindowDots::Find(HWND target) {
    for (auto& o : overlays_)
        if (o->target == target) return o.get();
    return nullptr;
}

bool WindowDots::IsCandidate(HWND h) const {
    if (!h || !IsWindow(h) || !IsWindowVisible(h) || GetAncestor(h, GA_ROOT) != h || h == GetShellWindow()) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return false;  // Exploder ma własne kropki
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    wchar_t cls[64]{};  // aplikacje Explodera (Terminal, Kalkulator, Sklep) mają własne kropki
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    if (wcsncmp(cls, L"Exploder", 8) == 0) return false;
    return KindOf(h) != Kind::None;
}

void WindowDots::Track(HWND target) {
    if (!enabled_ || Find(target) || !IsCandidate(target)) return;
    auto o = std::make_unique<Overlay>();
    o->target = target;
    Overlay* raw = o.get();
    overlays_.push_back(std::move(o));
    raw->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, kClassName, L"", WS_POPUP, 0, 0, 1, 1,
                                nullptr, nullptr, inst_, raw);
    if (!raw->hwnd) {
        overlays_.pop_back();
        return;
    }
    Update(*raw);
}

void WindowDots::Drop(HWND target) {
    auto it = std::find_if(overlays_.begin(), overlays_.end(), [target](const auto& o) { return o->target == target; });
    if (it == overlays_.end()) return;
    std::unique_ptr<Overlay> o = std::move(*it);
    overlays_.erase(it);
    if (o->hwnd) DestroyWindow(o->hwnd);
}

bool WindowDots::ButtonArea(HWND t, RECT& out, float& scale, float* dy) const {
    scale = 1.f;
    if (dy) *dy = 0.f;
    if (!t || !IsWindowVisible(t) || IsIconic(t) || WorkArea::IsFullscreenWindow(t)) return false;
    DWORD cloaked = 0;  // np. okno na innym pulpicie wirtualnym
    if (SUCCEEDED(DwmGetWindowAttribute(t, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    const float s = GetDpiForWindow(t) / 96.f;
    RECT frame{};  // widoczne granice okna (bez niewidzialnej ramki do zmiany rozmiaru)
    if (FAILED(DwmGetWindowAttribute(t, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) GetWindowRect(t, &frame);
    // Okna-widma: maleńkie, poza ekranami albo w pełni przezroczyste (puste czarne nakładki w rogach ekranu)
    if (frame.right - frame.left < 160 * s || frame.bottom - frame.top < 80 * s || !MonitorFromRect(&frame, MONITOR_DEFAULTTONULL)) return false;
    BYTE alpha = 255;
    DWORD layered = 0;
    if ((GetWindowLongPtrW(t, GWL_EXSTYLE) & WS_EX_LAYERED) && GetLayeredWindowAttributes(t, nullptr, &alpha, &layered) &&
        (layered & LWA_ALPHA) && alpha < 32)
        return false;

    const Kind kind = KindOf(t);
    if (kind == Kind::Browser || kind == Kind::Discord || kind == Kind::Electron) {
        // Brave/Chrome: 3 przyciski 45×29 DIP; Discord (zmierzone): 36×32 DIP; Electron: 46 DIP szerokości jak w Windows,
        // wysokość VS Code (zmierzone) 35 DIP, pozostałe 32 DIP
        const float ew = 3 * 46.f, eh = kind == Kind::Electron && ExeName(t) == L"code.exe" ? 35.f : 32.f;
        const float bw = kind == Kind::Browser ? 3 * 45.f : kind == Kind::Discord ? 3 * 36.f : ew;
        const float bh = kind == Kind::Browser ? 29.f : kind == Kind::Discord ? 32.f : eh;
        scale = kind == Kind::Browser ? 1.3f : 1.f;
        if (dy && kind == Kind::Browser) *dy = 4.f;
        out = {frame.right - static_cast<LONG>(bw * s), frame.top, frame.right, frame.top + static_cast<LONG>(bh * s)};
        return true;
    }
    if (kind != Kind::Standard) return false;
    RECT cb{}, wr{};
    if (FAILED(DwmGetWindowAttribute(t, DWMWA_CAPTION_BUTTON_BOUNDS, &cb, sizeof(cb))) || cb.right - cb.left < 60) return false;
    GetWindowRect(t, &wr);  // DWMWA_CAPTION_BUTTON_BOUNDS jest względem prostokąta okna
    out = {wr.left + cb.left, wr.top + cb.top, wr.left + cb.right, wr.top + cb.bottom};
    out.top = std::max(out.top, frame.top);
    out.right = std::min(out.right, frame.right);
    return out.right - out.left > 40 && out.bottom - out.top > 10;
}

void WindowDots::Hide(Overlay& o) {
    if (o.hwnd && IsWindowVisible(o.hwnd)) ShowWindow(o.hwnd, SW_HIDE);
    o.hover = false;
}

// Kolor tła = najczęstszy kolor paska tytułu na lewo od przycisków (pomija kreski, ikony i przezroczysty skraj okna).
// Zwraca true, gdy kolor się zmienił. Gdy to miejsce zasłania inne okno — zostaje poprzedni kolor.
bool WindowDots::SampleBackground(Overlay& o, const RECT& area) {
    const int py = (area.top + area.bottom) / 2, px = area.left - 6;
    const HWND under = WindowFromPoint({px, py});
    if (!under || GetAncestor(under, GA_ROOT) != o.target) return false;
    HDC screen = GetDC(nullptr);
    COLORREF samples[7]{};
    for (int i = 0; i < 7; ++i) samples[i] = GetPixel(screen, px - i * 3, py);
    ReleaseDC(nullptr, screen);
    COLORREF c = samples[0];
    int best = 0;
    for (COLORREF a : samples) {
        const int n = static_cast<int>(std::count(std::begin(samples), std::end(samples), a));
        if (n > best) best = n, c = a;
    }
    if (c == CLR_INVALID) return false;
    o.sampled = true;
    const D2D1_COLOR_F next = D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, 1.f);
    const bool changed = next.r != o.background.r || next.g != o.background.g || next.b != o.background.b;
    o.background = next;
    return changed;
}

// Okno tuż po zmianie bywa jeszcze w starych kolorach (np. Discord po aktywacji) — kolejne próbki przez ~2 s
void WindowDots::StartResample(Overlay& o) {
    if (!o.hwnd || !IsWindowVisible(o.hwnd)) return;
    o.resampleTicks = 4;
    SetTimer(o.hwnd, kResampleTimer, 500, nullptr);
}

void WindowDots::Update(Overlay& o) {
    RECT area{};
    float scale = 1.f;
    if (!enabled_ || o.moving || !ButtonArea(o.target, area, scale, &o.dy)) return Hide(o);
    o.scale = scale;
    o.dpi = GetDpiForWindow(o.target);
    SampleBackground(o, area);
    // W tym miejscu nigdy nie było widać okna (przezroczyste, poza ekranem, schowane pod innymi) — bez pustej nakładki;
    // co 1,5 s próba od nowa (Periodic)
    if (!o.sampled) return Hide(o);
    if ((GetWindowLongPtrW(o.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) && !(GetWindowLongPtrW(o.target, GWL_EXSTYLE) & WS_EX_TOPMOST))
        SetWindowPos(o.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    const HWND after = InsertAfterFor(o.target, o.hwnd);
    SetWindowPos(o.hwnd, after, area.left, area.top, area.right - area.left, area.bottom - area.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | (after ? 0 : SWP_NOZORDER));
    Render(o);
    StartResample(o);
}

void WindowDots::Follow(Overlay& o) {
    RECT area{};
    float scale = 1.f, dy = o.dy;
    if (!enabled_ || !ButtonArea(o.target, area, scale, &dy)) return Hide(o);
    RECT cur{};
    GetWindowRect(o.hwnd, &cur);
    const bool redraw = !IsWindowVisible(o.hwnd) || cur.right - cur.left != area.right - area.left || cur.bottom - cur.top != area.bottom - area.top;
    SetWindowPos(o.hwnd, nullptr, area.left, area.top, area.right - area.left, area.bottom - area.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    if (redraw) Render(o);
}

void WindowDots::Restack() {
    for (auto& o : overlays_) {
        if (!IsWindowVisible(o->hwnd)) continue;
        if (const HWND after = InsertAfterFor(o->target, o->hwnd))
            SetWindowPos(o->hwnd, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void WindowDots::Render(Overlay& o) {
    RECT rc{};
    GetClientRect(o.hwnd, &rc);
    const UINT w = static_cast<UINT>(std::max<LONG>(rc.right, 1)), h = static_cast<UINT>(std::max<LONG>(rc.bottom, 1));
    if (!o.surface.Context()) {
        if (!o.surface.Create(*g_, o.hwnd, w, h)) return;
        o.surface.Context()->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &o.brush);
    } else {
        o.surface.Resize(w, h);
    }
    ID2D1DeviceContext* dc = o.surface.Begin();
    if (!dc || !o.brush) return;
    dc->SetDpi(static_cast<float>(o.dpi), static_cast<float>(o.dpi));
    o.widthDip = rc.right * 96.f / o.dpi;
    o.heightDip = rc.bottom * 96.f / o.dpi;
    dc->Clear(o.background);  // zakrywa przyciski Windows
    TrafficLights::Draw(dc, o.brush.Get(), DotsOrigin(o.widthDip, o.heightDip, o.scale, o.dy), o.hover, true, o.scale);
    if (!o.surface.End()) {
        o.surface = Surface{};
        o.brush.Reset();
    }
}

LRESULT CALLBACK WindowDots::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {  // uchwyt od razu — przed pierwszym komunikatem do HandleMessage
        auto* created = static_cast<Overlay*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        created->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
    }
    if (msg == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    auto* o = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return o && g_instance ? g_instance->HandleMessage(*o, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT WindowDots::HandleMessage(Overlay& o, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;  // okno programu nie zmienia się przez kliknięcie kropki
    case WM_TIMER:
        if (wp == kResampleTimer) {
            if (--o.resampleTicks <= 0) KillTimer(o.hwnd, kResampleTimer);
            RECT area{};
            float scale = 1.f;
            if (IsWindowVisible(o.hwnd) && !o.moving && ButtonArea(o.target, area, scale) && SampleBackground(o, area)) Render(o);
        }
        return 0;
    case WM_PAINT:
        ValidateRect(o.hwnd, nullptr);
        if (IsWindowVisible(o.hwnd)) Render(o);
        return 0;
    case WM_MOUSEMOVE:
        if (!o.trackingMouse) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, o.hwnd, 0};
            o.trackingMouse = TrackMouseEvent(&tme) != FALSE;
        }
        if (!o.hover) {  // jak w macOS: symbole na wszystkich kropkach, gdy mysz jest nad grupą
            o.hover = true;
            Render(o);
        }
        return 0;
    case WM_MOUSELEAVE:
        o.trackingMouse = false;
        o.hover = false;
        Render(o);
        return 0;
    case WM_LBUTTONUP: {
        const HWND t = o.target;
        if (!t || !IsWindow(t)) return 0;
        const D2D1_POINT_2F p{GET_X_LPARAM(lp) * 96.f / o.dpi, GET_Y_LPARAM(lp) * 96.f / o.dpi};
        switch (TrafficLights::HitTest(DotsOrigin(o.widthDip, o.heightDip, o.scale, o.dy), p, o.scale)) {
        case 0: PostMessageW(t, WM_SYSCOMMAND, SC_CLOSE, 0); break;
        case 1: PostMessageW(t, WM_SYSCOMMAND, SC_MINIMIZE, 0); break;
        case 2: PostMessageW(t, WM_SYSCOMMAND, IsZoomed(t) ? SC_RESTORE : SC_MAXIMIZE, 0); break;
        }
        return 0;
    }
    }
    return DefWindowProcW(o.hwnd, msg, wp, lp);
}
