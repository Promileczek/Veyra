#include "Taskbar.h"
#include "../core/Accent.h"
#include "../core/Edition.h"
#include "../core/Settings.h"
#include "../core/WorkArea.h"
#include "../desktop/DesktopItems.h"
#include "../files/FileWindow.h"
#include <knownfolders.h>
#include <shlobj.h>
#include <shellapi.h>
#include "AppIcons.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr UINT WM_APPBAR = WM_APP + 1;
constexpr UINT WM_REDRAW = WM_APP + 2;
constexpr UINT WM_START_KEY = WM_APP + 3;  // sam klawisz Windows (wciśnięty i puszczony bez innego klawisza)
Taskbar* g_taskbar = nullptr;              // dla haka klawiatury
constexpr UINT_PTR kClockTimer = 1, kFullscreenTimer = 2;
constexpr int kHotkeyQuit = 1, kHotkeySnip = 2, kHotkeyRun = 3;
constexpr int kNone = -1, kStart = -2, kClock = -3;  // >= 0: indeks przycisku okna
constexpr int kTray = -4;                            // przycisk "Tray" (ikony zasobnika schowane w panelu)
constexpr float kDividerWidth = 13.f;                // separator przed zasobnikiem/zegarem w docku
constexpr float kTrayButton = 56.f;
constexpr wchar_t kClassName[] = L"ExploderTaskbar";

enum MenuCmd { CmdCloseWindow = 1, CmdExplorer, CmdTaskMgr, CmdQuit, CmdWindowDots };

bool Contains(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
}  // namespace

bool Taskbar::Create(HINSTANCE inst, Graphics& g) {
    g_ = &g;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;  // podwójne kliknięcie w ikonę zasobnika
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                            kClassName, L"Exploder Taskbar", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);

    // Dock rysuje własne szkło (DrawGlass): akryl DWM ignoruje zaokrąglony region i zostawiał kwadratowe rogi
    if (!IsDock() && !(theme_.acrylic && SetBackdrop(hwnd_, Backdrop::Acrylic, theme_.acrylicTint)))
        theme_.background.a = 0.95f;  // bez rozmycia: prawie nieprzezroczyste tło
    if (!CreateDeviceResources()) return false;

    tracker_.Start(hwnd_, [this] { RequestRedraw(); });
    // Zasobnik tylko gdy jesteśmy powłoką (explorer.exe nie działa). Wtedy AppBar też nie działa
    // (obsługuje go explorer), więc obszar roboczy ustawiamy sami — ApplyWorkArea.
    tray_.Start(inst, [this] {
        trayFlyout_.Refresh();
        RequestRedraw();
    });
    trayFlyout_.Create(inst, g, tray_, theme_, [this] { RequestRedraw(); });
    startMenu_.Create(inst, g, L"ExploderStartMenu");
    startMenu_.showDesktop = [this] { ToggleDesktop(); };
    runBox_.Create(inst, g, L"ExploderRunBox");
    startMenu_.openRun = [this] { runBox_.Open(dpi_); };
    startMenu_.openFiles = [this] {
        ComPtr<IShellItem> pc;
        SHGetKnownFolderItem(FOLDERID_ComputerFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&pc));
        if (Edition::Minimal()) Edition::OpenInExplorer(pc.Get());
        else OpenFileWindow(inst_, *g_, pc);
    };
    if (tray_.Active()) {
        HideMinimizedWindows(true);
        EnumWindows([](HWND h, LPARAM) -> BOOL {
            ParkMinimized(h);
            return TRUE;
        }, 0);
        minimizeHook_ = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZESTART, nullptr, OnMinimize, 0, 0,
                                        WINEVENT_OUTOFCONTEXT);
        // Klawisz Windows → menu Start (bez explorera nikt go nie obsługuje)
        g_taskbar = this;
        winKeyHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, WinKeyHook, inst, 0);
    }

    APPBARDATA abd{sizeof(abd)};
    abd.hWnd = hwnd_;
    abd.uCallbackMessage = WM_APPBAR;
    appbar_ = SHAppBarMessage(ABM_NEW, &abd) != FALSE;
    ReserveSpace();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);

    // Awaryjne wyjście: Ctrl+Alt+Shift+Q zamyka Exploder i uruchamia explorer.exe.
    RegisterHotKey(hwnd_, kHotkeyQuit, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q');
    // Win+Shift+S — wycinek ekranu (gdy działa explorer, skrót należy do niego i rejestracja się nie uda)
    RegisterHotKey(hwnd_, kHotkeySnip, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, 'S');
    // Win+R — okienko "Uruchom" Veyry (z explorerem skrót należy do niego)
    RegisterHotKey(hwnd_, kHotkeyRun, MOD_WIN | MOD_NOREPEAT, 'R');
    tooltip_.Create(inst, g);
    inst_ = inst;
    ScheduleClock();
    if (!appbar_) SetTimer(hwnd_, kFullscreenTimer, 500, nullptr);  // bez explorera nikt nie przyśle ABN_FULLSCREENAPP
    return true;
}

bool Taskbar::CreateDeviceResources() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (!surface_.Create(*g_, hwnd_, static_cast<UINT>(std::max<LONG>(rc.right, 1)),
                         static_cast<UINT>(std::max<LONG>(rc.bottom, 1))))
        return false;
    ID2D1DeviceContext* dc = surface_.Context();
    dc->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);  // ClearType nie działa na przezroczystym tle
    if (FAILED(dc->CreateSolidColorBrush(theme_.text, &brush_))) return false;

    if (!clockFormat_) {
        if (FAILED(g_->DWrite()->CreateTextFormat(theme_.font, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                  theme_.fontSize, L"pl-PL", &clockFormat_)))
            return false;
        clockFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        clockFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    icons_.clear();
    return true;
}

void Taskbar::RecoverDevice() {
    glassStrip_.Reset();
    glassBlur_.Reset();
    surface_ = Surface{};
    brush_.Reset();
    icons_.clear();
    if (g_->Init() && CreateDeviceResources()) RequestRedraw();
}

float Taskbar::DockWidthDip() const {
    const float n = static_cast<float>(tracker_.Windows().size() + 1);  // + Start
    return 2 * theme_.dockPadding + n * theme_.dockCell + kDividerWidth + TrayWidthDip() + theme_.clockWidth;
}

float Taskbar::TrayWidthDip() const {
    return tray_.Active() ? kTrayButton : 0.f;
}

void Taskbar::ApplyWorkArea(bool restore) {
    if (restore) WorkArea::Restore();
    else WorkArea::Reserve(ABE_BOTTOM, reserved_.bottom - reserved_.top);
}

void Taskbar::ReserveSpace() {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const float hDip = IsDock() ? theme_.dockHeight + 2 * theme_.dockMargin : theme_.height;
    const int h = static_cast<int>(std::lround(hDip * dpi_ / 96.f));

    APPBARDATA abd{sizeof(abd)};
    abd.hWnd = hwnd_;
    abd.uEdge = ABE_BOTTOM;
    abd.rc = mi.rcMonitor;
    abd.rc.top = abd.rc.bottom - h;
    if (appbar_) {
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        abd.rc.top = abd.rc.bottom - h;
        SHAppBarMessage(ABM_SETPOS, &abd);
    }
    reserved_ = abd.rc;
    if (!appbar_) ApplyWorkArea(false);
    PositionWindow();
}

void Taskbar::PositionWindow() {
    RECT r = reserved_;
    if (IsDock()) {
        const float s = dpi_ / 96.f;
        const int areaW = reserved_.right - reserved_.left;
        const int margin = static_cast<int>(std::lround(theme_.dockMargin * s));
        const int w = std::min(static_cast<int>(std::lround(DockWidthDip() * s)), areaW - 2 * margin);
        r.left = reserved_.left + (areaW - w) / 2;
        r.right = r.left + w;
        r.bottom = reserved_.bottom - margin;
        r.top = r.bottom - static_cast<int>(std::lround(theme_.dockHeight * s));
    }
    RECT cur{};
    GetWindowRect(hwnd_, &cur);
    if (!EqualRect(&cur, &r))
        SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE);
    tray_.SetRect(r);
}

void Taskbar::RequestRedraw() {
    if (redrawPending_) return;
    redrawPending_ = true;  // zlewamy serię zdarzeń w jedno rysowanie
    PostMessageW(hwnd_, WM_REDRAW, 0, 0);
}

void Taskbar::ScheduleClock() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const UINT ms = (60 - st.wSecond) * 1000 - st.wMilliseconds + 50;  // budzimy się tylko raz na minutę
    SetTimer(hwnd_, kClockTimer, ms, nullptr);
}

void Taskbar::Layout() {
    auto& wins = tracker_.Windows();
    const float count = static_cast<float>(wins.size() + 1);  // + przycisk Start
    buttonRects_.resize(wins.size());

    const float trayW = TrayWidthDip();

    if (IsDock()) {
        const float pad = theme_.dockPadding;
        const float fixed = 2 * pad + kDividerWidth + trayW + theme_.clockWidth;
        float cell = theme_.dockCell;
        if (fixed + count * cell > widthDip_) cell = std::max(20.f, (widthDip_ - fixed) / count);
        iconSize_ = theme_.dockIconSize * cell / theme_.dockCell;

        const float top = pad, bottom = heightDip_ - pad;
        float x = pad;
        startRect_ = {x, top, x + cell, bottom};
        for (auto& r : buttonRects_) {
            x += cell;
            r = {x, top, x + cell, bottom};
        }
        x += cell;
        dividerRect_ = {x + 6.f, top + 8.f, x + 7.f, bottom - 8.f};
        x += kDividerWidth;
        trayRect_ = trayW > 0.f ? D2D1_RECT_F{x, top, x + trayW, bottom} : D2D1_RECT_F{};
        x += trayW;
        clockRect_ = {x, top, x + theme_.clockWidth, bottom};
    } else {
        const float margin = 8.f, gap = 4.f;
        const float bh = theme_.buttonHeight, y = (heightDip_ - bh) / 2;
        clockRect_ = {widthDip_ - margin - theme_.clockWidth, y, widthDip_ - margin, y + bh};
        dividerRect_ = {};
        iconSize_ = theme_.iconSize;
        trayRect_ = trayW > 0.f ? D2D1_RECT_F{clockRect_.left - trayW, y, clockRect_.left, y + bh} : D2D1_RECT_F{};

        const float avail = clockRect_.left - trayW - 2 * margin;
        float bw = theme_.buttonWidth;
        float total = count * bw + (count - 1) * gap;
        if (total > avail) {
            bw = std::max(16.f, (avail - (count - 1) * gap) / count);
            total = count * bw + (count - 1) * gap;
        }
        float x = theme_.centered ? std::max(margin, (widthDip_ - total) / 2) : margin;
        startRect_ = {x, y, x + bw, y + bh};
        for (auto& r : buttonRects_) {
            x += bw + gap;
            r = {x, y, x + bw, y + bh};
        }
    }
    std::erase_if(icons_, [&](const auto& kv) {
        return std::none_of(wins.begin(), wins.end(), [&](const TrackedWindow& w) { return w.hwnd == kv.first; });
    });
}

void Taskbar::Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius) {
    brush_->SetColor(c);
    if (radius > 0.f)
        surface_.Context()->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
    else
        surface_.Context()->FillRectangle(r, brush_.Get());
}

const AppIcon& Taskbar::IconFor(const TrackedWindow& w) const {
    static const AppIcon empty;
    auto it = icons_.find(w.hwnd);
    return it != icons_.end() ? it->second : empty;
}

void Taskbar::PrepareIcons() {
    const int px = static_cast<int>(std::ceil(iconSize_ * dpi_ / 96.f));
    // Po indeksie i z kopią uchwytu: w trakcie ResolveAppIcon lista okien może się zmienić
    for (size_t i = 0; i < tracker_.Windows().size(); ++i) {
        const HWND h = tracker_.Windows()[i].hwnd;
        if (icons_.count(h) && !tracker_.Windows()[i].iconDirty) continue;
        AppIcon icon = ResolveAppIcon(h, px, g_->WIC(), surface_.Context());
        icons_[h] = std::move(icon);
        for (TrackedWindow& w : tracker_.Windows())
            if (w.hwnd == h) w.iconDirty = false;
    }
}

void Taskbar::Render() {
    if (rendering_) return RequestRedraw();
    rendering_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{rendering_};
    if (surface_.Context()) PrepareIcons();
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc) return;
    const float W = widthDip_, H = heightDip_;

    const bool dock = IsDock();
    const float px = 96.f / dpi_;  // 1 piksel w DIP
    const float hoverRadius = dock ? 12.f : theme_.radius;

    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    if (dock) {
        // Szkło: rozmyta tapeta przycięta do zaokrąglonego kształtu (gładkie rogi, bez regionu okna)
        const auto shape = D2D1::RoundedRect({px / 2, px / 2, W - px / 2, H - px / 2}, theme_.dockRadius, theme_.dockRadius);
        DrawGlass(dc, shape);
        brush_->SetColor(theme_.dockBackground);
        dc->FillRoundedRectangle(shape, brush_.Get());
        brush_->SetColor(theme_.dockBorder);
        dc->DrawRoundedRectangle(shape, brush_.Get(), px);
        Fill(dividerRect_, theme_.dockBorder);
    } else {
        Fill({0, 0, W, H}, theme_.background);
        Fill({0, 0, W, px}, theme_.border);  // 1 px linii u góry
    }

    // Start (na razie tylko logo)
    if (hover_ == kStart) Fill(startRect_, pressed_ == kStart ? theme_.pressed : theme_.hover, hoverRadius);
    {
        const float sq = iconSize_ / 3.f, gp = sq * 0.19f;
        const float cx = (startRect_.left + startRect_.right) / 2, cy = (startRect_.top + startRect_.bottom) / 2;
        for (int i = 0; i < 4; ++i) {
            const float x = cx - sq - gp / 2 + (i % 2) * (sq + gp);
            const float y = cy - sq - gp / 2 + (i / 2) * (sq + gp);
            Fill({x, y, x + sq, y + sq}, theme_.accent, sq * 0.2f);
        }
    }

    // Okna
    auto& wins = tracker_.Windows();
    for (size_t i = 0; i < wins.size() && i < buttonRects_.size(); ++i) {
        TrackedWindow& w = wins[i];
        const D2D1_RECT_F& r = buttonRects_[i];
        const int id = static_cast<int>(i);
        const bool active = tracker_.IsActive(w.hwnd);

        const D2D1_COLOR_F* bg = nullptr;
        if (w.flashing) bg = &theme_.flash;
        else if (pressed_ == id && !dock) bg = &theme_.pressed;
        else if (hover_ == id) bg = active && !dock ? &theme_.hoverActive : &theme_.hover;
        else if (active && !dock) bg = &theme_.activeBg;
        if (bg) Fill(r, *bg, hoverRadius);

        const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2 - 1.f;
        {
            const AppIcon& icon = IconFor(w);
            const float s = std::min(iconSize_, r.right - r.left - 4.f);
            const D2D1_RECT_F dst{cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2};
            const float opacity = dock && pressed_ == id ? 0.6f : 1.f;  // macOS: wciśnięta ikona ciemnieje
            if (icon.builtin != BuiltinIcon::None)
                DrawBuiltinIcon(dc, icon.builtin, dst, opacity);
            else if (icon.bitmap)
                dc->DrawBitmap(icon.bitmap.Get(), &dst, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
        if (dock) {  // kropka pod uruchomioną aplikacją; aktywne okno — jaśniejsza
            brush_->SetColor(active ? theme_.dockDot : theme_.indicator);
            dc->FillEllipse(D2D1::Ellipse({cx, H - 3.5f}, 2.f, 2.f), brush_.Get());
        } else {
            const float iw = active ? 16.f : 6.f;
            Fill({cx - iw / 2, r.bottom - 3.f, cx + iw / 2, r.bottom}, active ? theme_.accent : theme_.indicator, 1.5f);
        }
    }

    // Przycisk "Tray": strzałka + napis; strzałka w dół, gdy panel jest otwarty
    if (trayRect_.right > trayRect_.left) {
        const D2D1_RECT_F& r = trayRect_;
        if (hover_ == kTray || trayFlyout_.Visible()) Fill(r, theme_.hover, hoverRadius);
        const float cx = r.left + 12.f, cy = (r.top + r.bottom) / 2, a = 3.5f;
        const float dir = trayFlyout_.Visible() ? 1.f : -1.f;
        brush_->SetColor(theme_.text);
        dc->DrawLine({cx - a, cy - dir * a / 2}, {cx, cy + dir * a / 2}, brush_.Get(), 1.5f);
        dc->DrawLine({cx, cy + dir * a / 2}, {cx + a, cy - dir * a / 2}, brush_.Get(), 1.5f);
        dc->DrawText(L"Tray", 4, clockFormat_.Get(), {r.left + 18.f, r.top, r.right - 2.f, r.bottom}, brush_.Get());
    }

    // Zegar
    if (hover_ == kClock) Fill(clockRect_, theme_.hover, hoverRadius);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t time[32]{}, date[32]{}, text[72]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, ARRAYSIZE(time));
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, ARRAYSIZE(date), nullptr);
    swprintf_s(text, L"%s\n%s", time, date);
    brush_->SetColor(theme_.text);
    dc->DrawText(text, static_cast<UINT32>(wcslen(text)), clockFormat_.Get(), clockRect_, brush_.Get());

    if (!surface_.End()) RecoverDevice();
}

int Taskbar::HitTest(LPARAM lp) const {
    const float x = GET_X_LPARAM(lp) * 96.f / dpi_, y = GET_Y_LPARAM(lp) * 96.f / dpi_;
    if (Contains(startRect_, x, y)) return kStart;
    if (Contains(clockRect_, x, y)) return kClock;
    for (size_t i = 0; i < buttonRects_.size(); ++i)
        if (Contains(buttonRects_[i], x, y)) return static_cast<int>(i);
    if (Contains(trayRect_, x, y)) return kTray;
    return kNone;
}

void Taskbar::ToggleTrayFlyout() {
    RECT win{};
    GetWindowRect(hwnd_, &win);
    const float s = dpi_ / 96.f;
    const RECT anchor{win.left + static_cast<LONG>(trayRect_.left * s), win.top,
                      win.left + static_cast<LONG>(trayRect_.right * s), win.bottom};
    trayFlyout_.Toggle(anchor, dpi_);
}

// Sam klawisz Windows: wciśnięty i puszczony bez innego klawisza (Win+Shift+S itp. nie otwierają menu).
// Hak tylko obserwuje — nie zjada klawiszy, więc skróty z Win dalej działają.
LRESULT CALLBACK Taskbar::WinKeyHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_taskbar) {
        const auto& k = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        const bool win = k.vkCode == VK_LWIN || k.vkCode == VK_RWIN;
        Taskbar& t = *g_taskbar;
        if (win && down) {
            if (!t.winDown_) t.winUsed_ = false;  // pierwsze wciśnięcie (nie autopowtarzanie)
            t.winDown_ = true;
        } else if (win) {
            if (t.winDown_ && !t.winUsed_) PostMessageW(t.hwnd_, WM_START_KEY, 0, 0);
            t.winDown_ = false;
        } else if (down && k.vkCode == VK_LAUNCH_APP2) {  // klawisz Kalkulator → Kalkulator Veyry
            PostMessageW(t.hwnd_, WM_START_KEY, 1, 0);
        } else if (down && t.winDown_) {
            t.winUsed_ = true;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// Szkło docka: dolny pas tapety (cała szerokość monitora — dock zmienia szerokość, więc pas liczymy raz), rozmyty,
// przycięty do kształtu docka + przyciemnienie. Odświeżane po zmianie tapety albo monitora.
void Taskbar::DrawGlass(ID2D1DeviceContext* dc, const D2D1_ROUNDED_RECT& shape) {
    RECT win{};
    GetWindowRect(hwnd_, &win);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT& m = mi.rcMonitor;
    if (!glassBlur_ || !EqualRect(&m, &glassMonitor_)) {
        glassStrip_.Reset();
        glassBlur_.Reset();
        glassMonitor_ = m;
        const UINT mw = static_cast<UINT>(m.right - m.left), mh = static_cast<UINT>(m.bottom - m.top);
        const UINT stripH = std::min<UINT>(mh, static_cast<UINT>(std::lround((theme_.dockHeight + theme_.dockMargin + 60.f) * dpi_ / 96.f)));
        ComPtr<IWICBitmapClipper> clip;
        if (ComPtr<IWICBitmapSource> wall = LoadWallpaper(g_->WIC(), mw, mh); wall && SUCCEEDED(g_->WIC()->CreateBitmapClipper(&clip))) {
            const WICRect rc{0, static_cast<INT>(mh - stripH), static_cast<INT>(mw), static_cast<INT>(stripH)};
            const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                      static_cast<float>(dpi_), static_cast<float>(dpi_));
            if (SUCCEEDED(clip->Initialize(wall.Get(), &rc)) && SUCCEEDED(dc->CreateBitmapFromWicBitmap(clip.Get(), &props, &glassStrip_)) &&
                SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &glassBlur_))) {
                glassBlur_->SetInput(0, glassStrip_.Get());
                glassBlur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 22.f);
                glassBlur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
            }
        }
        glassStripTop_ = m.bottom - static_cast<LONG>(stripH);
    }
    ComPtr<ID2D1Factory> factory;
    dc->GetFactory(&factory);
    ComPtr<ID2D1RoundedRectangleGeometry> geo;
    if (FAILED(factory->CreateRoundedRectangleGeometry(shape, &geo))) return;
    dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), geo.Get()), nullptr);
    if (glassBlur_) {
        const float k = 96.f / dpi_;  // piksele → DIP
        dc->DrawImage(glassBlur_.Get(), D2D1::Point2F((m.left - win.left) * k, (glassStripTop_ - win.top) * k));
    }
    brush_->SetColor(D2D1::ColorF(0.10f, 0.10f, 0.12f, glassBlur_ ? 0.45f : 0.92f));
    dc->FillRoundedRectangle(shape, brush_.Get());
    dc->PopLayer();
}

void Taskbar::ToggleStartMenu() {
    RECT win{};
    GetWindowRect(hwnd_, &win);
    const float s = dpi_ / 96.f;
    const RECT anchor{win.left + static_cast<LONG>(startRect_.left * s), win.top,
                      win.left + static_cast<LONG>(startRect_.right * s), win.bottom};
    startMenu_.Toggle(anchor, dpi_);
}

void Taskbar::OnClick(int hit) {
    if (hit == kTray) {
        ToggleTrayFlyout();
        return;
    }
    if (hit == kStart) {  // przycisk Windows: małe menu (dawne "pokaż pulpit" jest w menu)
        ToggleStartMenu();
        return;
    }
    auto& wins = tracker_.Windows();
    if (hit < 0 || hit >= static_cast<int>(wins.size())) return;
    const HWND h = wins[static_cast<size_t>(hit)].hwnd;
    if (IsIconic(h)) {
        ShowWindowAsync(h, SW_RESTORE);
        SetForegroundWindow(h);
    } else if (tracker_.IsActive(h)) {
        ShowWindowAsync(h, SW_MINIMIZE);
    } else if (!SetForegroundWindow(h)) {
        SwitchToThisWindow(h, TRUE);
    }
}

// Start = "pokaż pulpit" (z panelem aplikacji): minimalizuje okna, drugi klik je przywraca.
void Taskbar::ToggleDesktop() {
    std::erase_if(hiddenByStart_, [](HWND h) { return !IsWindow(h) || !IsIconic(h); });
    if (!hiddenByStart_.empty()) {
        for (auto it = hiddenByStart_.rbegin(); it != hiddenByStart_.rend(); ++it) ShowWindowAsync(*it, SW_RESTORE);
        hiddenByStart_.clear();
        return;
    }
    for (const TrackedWindow& w : tracker_.Windows()) {
        if (IsIconic(w.hwnd)) continue;
        ShowWindowAsync(w.hwnd, SW_MINIMIZE);
        hiddenByStart_.push_back(w.hwnd);
    }
    if (HWND desktop = GetShellWindow()) SetForegroundWindow(desktop);  // pisanie od razu trafia do wyszukiwarki
}

// Dymek nad ikoną (jak w macOS): nazwa programu + tytuł okna.
void Taskbar::UpdateTooltip() {
    std::wstring title, subtitle;
    D2D1_RECT_F r{};
    auto& wins = tracker_.Windows();
    if (hover_ >= 0 && hover_ < static_cast<int>(wins.size()) && hover_ < static_cast<int>(buttonRects_.size())) {
        TrackedWindow& w = wins[static_cast<size_t>(hover_)];
        title = IconFor(w).appName;
        subtitle = w.title;
        if (title.empty()) std::swap(title, subtitle);
        r = buttonRects_[static_cast<size_t>(hover_)];
    } else if (hover_ == kStart) {
        title = L"Start";
        subtitle = L"Pokaż pulpit i aplikacje";
        r = startRect_;
    } else if (hover_ == kClock) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t date[80]{};
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, date, ARRAYSIZE(date), nullptr);
        title = date;
        r = clockRect_;
    }
    if (title.empty()) {
        tooltip_.Hide();
        return;
    }
    RECT win{};
    GetWindowRect(hwnd_, &win);
    const float s = dpi_ / 96.f;
    tooltip_.Show(title, subtitle, win.left + static_cast<int>((r.left + r.right) / 2 * s), win.top - static_cast<int>(8.f * s), dpi_);
}

void Taskbar::OnRightClick(int hit) {
    auto& wins = tracker_.Windows();
    const bool onWindow = hit >= 0 && hit < static_cast<int>(wins.size());
    const HWND target = onWindow ? wins[static_cast<size_t>(hit)].hwnd : nullptr;

    HMENU menu = CreatePopupMenu();
    if (onWindow) {
        AppendMenuW(menu, MF_STRING, CmdCloseWindow, L"Zamknij okno");
    } else {
        AppendMenuW(menu, MF_STRING, CmdTaskMgr, L"Menedżer zadań");
        if (dotsOn_) AppendMenuW(menu, MF_STRING | (dotsOn_() ? MF_CHECKED : 0), CmdWindowDots, L"Kropki macOS w oknach");
        AppendMenuW(menu, MF_STRING, CmdExplorer, tray_.Active() ? L"Wróć do explorer.exe" : L"Uruchom explorer.exe");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdQuit, L"Zamknij Exploder");
    }
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);  // bez tego menu nie zamyka się po kliknięciu obok
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case CmdCloseWindow: PostMessageW(target, WM_CLOSE, 0, 0); break;
    case CmdWindowDots:
        if (setDots_ && dotsOn_) setDots_(!dotsOn_());
        break;
    case CmdTaskMgr: ShellExecuteW(nullptr, nullptr, L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); break;
    case CmdExplorer:
        if (tray_.Active()) Quit(true);  // dwie powłoki naraz się gryzą
        else ShellExecuteW(nullptr, nullptr, L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case CmdQuit: Quit(false); break;
    }
}

// Bez explorera Windows rysuje zminimalizowane okna jako małe paski w rogu pulpitu (styl Windows 3.1).
// Explorer ukrywa je flagą ARW_HIDE — robimy to samo, a przy wyjściu przywracamy poprzednie ustawienie.
void Taskbar::HideMinimizedWindows(bool hide) {
    MINIMIZEDMETRICS mm{sizeof(mm)};
    if (!SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(mm), &mm, 0)) return;
    if (hide) {
        if (mm.iArrange & ARW_HIDE) return;
        originalArrange_ = mm.iArrange;
        mm.iArrange |= ARW_HIDE;
    } else {
        if (originalArrange_ < 0) return;
        mm.iArrange = originalArrange_;
        originalArrange_ = -1;
    }
    SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS, sizeof(mm), &mm, 0);
}

// Okno zminimalizowane, zanim ARW_HIDE zadziałało (albo przez program, który sam ustawia pozycję),
// zostaje na ekranie jako pasek — przenosimy jego pozycję "zminimalizowaną" poza ekran, jak explorer.
void Taskbar::ParkMinimized(HWND h) {
    if (!IsWindowVisible(h) || !IsIconic(h)) return;
    RECT r{};
    GetWindowRect(h, &r);
    if (r.left <= -30000) return;
    // (SetWindowPlacement z WPF_SETMINPOSITION nie działa — okno ląduje w lewym górnym rogu)
    SetWindowPos(h, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

void CALLBACK Taskbar::OnMinimize(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (idObject == OBJID_WINDOW) ParkMinimized(hwnd);
}

void Taskbar::Quit(bool startExplorer) {
    HideMinimizedWindows(false);
    tray_.Stop();  // najpierw zwalniamy Shell_TrayWnd, inaczej explorer nie uzna się za powłokę
    if (beforeExplorer_) beforeExplorer_();
    if (!appbar_) ApplyWorkArea(true);
    if (startExplorer && !FindWindowW(L"Shell_TrayWnd", nullptr))
        ShellExecuteW(nullptr, nullptr, L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
    DestroyWindow(hwnd_);
}

LRESULT CALLBACK Taskbar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<Taskbar*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Taskbar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Taskbar::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == Settings::ChangedMessage()) {
        Settings::Reload();
        if (onSettingsChanged_) onSettingsChanged_();
        return 0;
    }
    if (msg == tracker_.ShellHookMessage() && msg != 0) {
        if (tracker_.HandleShellHook(wp, lp)) RequestRedraw();
        return 0;
    }
    switch (msg) {
    case WM_REDRAW:
        redrawPending_ = false;
        if (tray_.Prune()) trayFlyout_.Refresh();
        if (IsDock()) PositionWindow();  // nowe/zamknięte okno zmienia szerokość docka
        Layout();
        Render();
        return 0;
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        RequestRedraw();
        return 0;
    case WM_SIZE:
        if (surface_.Context()) surface_.Resize(LOWORD(lp), HIWORD(lp));
        widthDip_ = LOWORD(lp) * 96.f / dpi_;
        heightDip_ = HIWORD(lp) * 96.f / dpi_;
        RequestRedraw();
        return 0;
    case WM_DPICHANGED:
        dpi_ = HIWORD(wp);
        if (surface_.Context()) surface_.Context()->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        icons_.clear();
        ReserveSpace();
        return 0;
    case WM_DISPLAYCHANGE:
        ReserveSpace();
        return 0;
    case WM_APPBAR:
        if (wp == ABN_POSCHANGED) ReserveSpace();
        else if (wp == ABN_FULLSCREENAPP)  // gra/film na pełnym ekranie: chowamy się pod spód
            SetWindowPos(hwnd_, lp ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    case WM_TIMER:
        if (wp == kFullscreenTimer) {
            // Gra/film na pełnym ekranie (też film w przeglądarce, bez zmiany aktywnego okna) → dock znika
            const bool fullscreen = WorkArea::IsFullscreenWindow(GetForegroundWindow());
            if (fullscreen != hiddenForFullscreen_) {
                hiddenForFullscreen_ = fullscreen;
                if (fullscreen) {
                    tooltip_.Hide();
                    trayFlyout_.Hide();
                }
                ShowWindow(hwnd_, fullscreen ? SW_HIDE : SW_SHOWNA);
            }
            return 0;
        }
        if (wp == kClockTimer) {
            RequestRedraw();
            ScheduleClock();
        }
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
        }
        const int hit = HitTest(lp);
        if (hit != hover_) {
            hover_ = hit;
            UpdateTooltip();
            RequestRedraw();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tooltip_.Hide();
        trackingMouse_ = false;
        hover_ = pressed_ = kNone;
        RequestRedraw();
        return 0;
    case WM_LBUTTONDBLCLK:  // drugie kliknięcie traktujemy jak zwykłe
    case WM_LBUTTONDOWN:
        tooltip_.Hide();
        pressed_ = HitTest(lp);
        SetCapture(hwnd_);
        RequestRedraw();
        return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        const int hit = HitTest(lp);
        const int was = pressed_;
        pressed_ = kNone;
        RequestRedraw();
        if (hit == was && hit != kNone) OnClick(hit);
        return 0;
    }
    case WM_RBUTTONUP:
        OnRightClick(HitTest(lp));
        return 0;
    case WM_HOTKEY:
        if (wp == kHotkeyQuit) Quit(true);
        else if (wp == kHotkeyRun) runBox_.Open(dpi_);
        else if (wp == kHotkeySnip) {
            tooltip_.Hide();
            snip_.Start(inst_);
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETDESKWALLPAPER) {  // nowa tapeta → nowe szkło
            glassStrip_.Reset();
            glassBlur_.Reset();
            RequestRedraw();
        }
        return 0;
    case WM_START_KEY:
        if (wp == 1) {  // klawisz Kalkulator
            wchar_t exe[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring path = exe;
            path = path.substr(0, path.find_last_of(L'\\') + 1) + L"exploder-calc.exe";
            if (Edition::Minimal()) path = L"calc.exe";
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            ToggleStartMenu();
        }
        return 0;
    case WM_DESTROY: {
        if (winKeyHook_) UnhookWindowsHookEx(winKeyHook_);
        winKeyHook_ = nullptr;
        g_taskbar = nullptr;
        tracker_.Stop();
        HideMinimizedWindows(false);
        if (minimizeHook_) UnhookWinEvent(minimizeHook_);
        minimizeHook_ = nullptr;
        tray_.Stop();
        if (!appbar_) ApplyWorkArea(true);
        UnregisterHotKey(hwnd_, kHotkeyQuit);
        UnregisterHotKey(hwnd_, kHotkeySnip);
        UnregisterHotKey(hwnd_, kHotkeyRun);
        KillTimer(hwnd_, kClockTimer);
        if (appbar_) {
            APPBARDATA abd{sizeof(abd)};
            abd.hWnd = hwnd_;
            SHAppBarMessage(ABM_REMOVE, &abd);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
