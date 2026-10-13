#include "Notification.h"
#include "../../apps/settings/Updater.h"
#include "../core/Accent.h"
#include "../core/Edition.h"
#include "../core/Settings.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr float kW = 372.f, kH = 122.f, kRadius = 16.f, kMargin = 14.f;
constexpr UINT_PTR kSlideTimer = 1, kAutoHideTimer = 2;
constexpr D2D1_COLOR_F kAccent{0.38f, 0.70f, 1.f, 1.f}, kOnAccent{0.043f, 0.10f, 0.15f, 1.f};
enum Hit { HitNone = -1, HitAction, HitDismiss, HitClose };

ComPtr<IDWriteTextFormat> Format(IDWriteFactory* dw, const wchar_t* font, DWRITE_FONT_WEIGHT w, float size,
                                 DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(dw->CreateTextFormat(font, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"pl-PL", &f))) return f;
    f->SetTextAlignment(align);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return f;
}
}  // namespace

bool Notification::Create(HINSTANCE inst, Graphics& g) {
    g_ = &g;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VeyraNotification";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // Bez aktywacji: powiadomienie nie przerywa pisania w innym programie
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"Veyra",
                            WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    SetBackdrop(hwnd_, Backdrop::Acrylic, 0x80201C1C);
    IDWriteFactory* dw = g_->DWrite();
    titleFormat_ = Format(dw, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.f);
    bodyFormat_ = Format(dw, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.f);
    buttonFormat_ = Format(dw, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.f, DWRITE_TEXT_ALIGNMENT_CENTER);
    glyphFormat_ = Format(dw, L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_NORMAL, 18.f, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (bodyFormat_) bodyFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    return titleFormat_ && bodyFormat_ && buttonFormat_ && glyphFormat_;
}

void Notification::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void Notification::Show(Content content, UINT dpi) {
    if (!hwnd_) return;
    content_ = std::move(content);
    dpi_ = dpi;
    hover_ = HitNone;
    slide_ = 1.f;
    leaving_ = false;
    Place(slide_);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    Render();
    SetTimer(hwnd_, kSlideTimer, 15, nullptr);
    SetTimer(hwnd_, kAutoHideTimer, 12000, nullptr);
}

void Notification::Leave() {
    if (!Visible() || leaving_) return;
    leaving_ = true;
    KillTimer(hwnd_, kAutoHideTimer);
    SetTimer(hwnd_, kSlideTimer, 15, nullptr);
}

void Notification::Hide() {
    if (!hwnd_) return;
    KillTimer(hwnd_, kSlideTimer);
    KillTimer(hwnd_, kAutoHideTimer);
    ShowWindow(hwnd_, SW_HIDE);
}

void Notification::Place(float slide) {
    const float s = dpi_ / 96.f;
    const int w = static_cast<int>(std::lround(kW * s)), h = static_cast<int>(std::lround(kH * s));
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);  // pod górnym paskiem Veyry
    const int margin = static_cast<int>(std::lround(kMargin * s));
    const int x = work.right - w - margin + static_cast<int>(std::lround(slide * (w + margin)));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, work.top + margin, w, h, SWP_NOACTIVATE);
    const int d = static_cast<int>(std::lround(kRadius * 2 * s));
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d), TRUE);
}

void Notification::Render() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT w = static_cast<UINT>(std::max<LONG>(rc.right, 1)), h = static_cast<UINT>(std::max<LONG>(rc.bottom, 1));
    if (!surface_.Context()) {
        if (!surface_.Create(*g_, hwnd_, w, h)) return;
        surface_.Context()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        surface_.Context()->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush_);
    } else {
        surface_.Resize(w, h);
    }
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc || !brush_) return;
    dc->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    const float px = 96.f / dpi_;
    auto fill = [&](const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius) {
        brush_->SetColor(c);
        dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
    };
    auto text = [&](const std::wstring& t, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
        brush_->SetColor(c);
        dc->DrawText(t.c_str(), static_cast<UINT32>(t.size()), f, r, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto shape = D2D1::RoundedRect({px / 2, px / 2, kW - px / 2, kH - px / 2}, kRadius, kRadius);
    brush_->SetColor(D2D1::ColorF(0.09f, 0.09f, 0.11f, 0.78f));
    dc->FillRoundedRectangle(shape, brush_.Get());
    brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.16f));
    dc->DrawRoundedRectangle(shape, brush_.Get(), px);

    // Ikona: kafelek w akcencie
    const D2D1_RECT_F icon{16.f, 16.f, 56.f, 56.f};
    fill(icon, kAccent, 12.f);
    text(std::wstring(1, content_.glyph), glyphFormat_.Get(), icon, kOnAccent);
    // Tytuł, opis
    text(content_.title, titleFormat_.Get(), {70.f, 14.f, kW - 40.f, 34.f}, D2D1::ColorF(1.f, 1.f, 1.f, 0.96f));
    text(content_.body, bodyFormat_.Get(), {70.f, 34.f, kW - 18.f, 70.f}, D2D1::ColorF(1.f, 1.f, 1.f, 0.62f));
    // Zamknij (×) w rogu — widoczne pod kursorem
    closeRect_ = {kW - 34.f, 10.f, kW - 10.f, 34.f};
    if (hover_ >= 0) {
        if (hover_ == HitClose) fill(closeRect_, D2D1::ColorF(1.f, 1.f, 1.f, 0.12f), 6.f);
        const wchar_t x = L'\xE8BB';
        brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.7f));
        ComPtr<IDWriteTextFormat> closeFormat = Format(g_->DWrite(), L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_NORMAL, 10.f, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (closeFormat) dc->DrawText(&x, 1, closeFormat.Get(), closeRect_, brush_.Get());
    }
    // Przyciski
    actionRect_ = {kW - 16.f - 110.f, kH - 16.f - 30.f, kW - 16.f, kH - 16.f};
    dismissRect_ = {actionRect_.left - 8.f - 90.f, actionRect_.top, actionRect_.left - 8.f, actionRect_.bottom};
    fill(actionRect_, hover_ == HitAction ? D2D1::ColorF(0.48f, 0.76f, 1.f) : kAccent, 8.f);
    text(content_.action, buttonFormat_.Get(), actionRect_, kOnAccent);
    fill(dismissRect_, D2D1::ColorF(1.f, 1.f, 1.f, hover_ == HitDismiss ? 0.16f : 0.08f), 8.f);
    text(content_.dismiss, buttonFormat_.Get(), dismissRect_, D2D1::ColorF(1.f, 1.f, 1.f, 0.9f));
    if (!surface_.End()) {
        surface_ = Surface{};
        brush_.Reset();
    }
}

int Notification::HitAt(D2D1_POINT_2F p) const {
    auto in = [&](const D2D1_RECT_F& r) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; };
    if (in(actionRect_)) return HitAction;
    if (in(dismissRect_)) return HitDismiss;
    if (in(closeRect_)) return HitClose;
    return p.x >= 0 && p.y >= 0 ? 99 : HitNone;  // 99 = gdzieś na karcie (pokazuje ×, wstrzymuje znikanie)
}

LRESULT CALLBACK Notification::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<Notification*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Notification*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Notification::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    const auto dip = [&] { return D2D1_POINT_2F{GET_X_LPARAM(lp) * 96.f / dpi_, GET_Y_LPARAM(lp) * 96.f / dpi_}; };
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        if (Visible()) Render();
        return 0;
    case WM_TIMER:
        if (wp == kSlideTimer) {  // wjazd z prawej / wyjazd w prawo, ok. 180 ms
            if (leaving_) {
                slide_ = std::min(1.f, slide_ + 0.09f);
                Place(slide_ * slide_);
                if (slide_ >= 1.f) Hide();
            } else {
                slide_ = std::max(0.f, slide_ - 0.09f);
                Place(slide_ * slide_);
                if (slide_ <= 0.f) KillTimer(hwnd_, kSlideTimer);
            }
        } else if (wp == kAutoHideTimer && hover_ == HitNone) {
            Leave();
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            tracking_ = TrackMouseEvent(&tme) != FALSE;
        }
        const int h = HitAt(dip());
        if (h != hover_) {
            hover_ = h;
            SetCursor(LoadCursorW(nullptr, h == HitAction || h == HitDismiss || h == HitClose ? IDC_HAND : IDC_ARROW));
            Render();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hover_ = HitNone;
        Render();
        SetTimer(hwnd_, kAutoHideTimer, 6000, nullptr);  // po zjechaniu kursorem — jeszcze chwila
        return 0;
    case WM_LBUTTONUP: {
        const int h = HitAt(dip());
        if (h == HitAction) {
            Leave();
            if (content_.onAction) content_.onAction();
        } else if (h == HitDismiss || h == HitClose) {
            Leave();
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------- Sprawdzanie aktualizacji ----------------
namespace {
constexpr UINT WM_UPDATE_FOUND = WM_APP + 50;  // lp = std::wstring* (wersja)
constexpr UINT_PTR kFirstCheck = 1, kPeriodicCheck = 2;
constexpr UINT kFirstDelayMs = 60 * 1000, kPeriodMs = 6 * 60 * 60 * 1000;
UpdateWatcher* g_watcher = nullptr;

std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L'\\') + 1);
}
}  // namespace

void UpdateWatcher::Start(HINSTANCE inst, Graphics& g, std::function<UINT()> dpi) {
    if (Edition::Minimal()) return;  // aktualizacje są w Ustawieniach Full
    dpi_ = std::move(dpi);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"VeyraUpdateWatcher";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    if (!hwnd_) return;
    g_watcher = this;
    toast_.Create(inst, g);
    SetTimer(hwnd_, kFirstCheck, kFirstDelayMs, nullptr);
    SetTimer(hwnd_, kPeriodicCheck, kPeriodMs, nullptr);
}

void UpdateWatcher::Stop() {
    if (worker_.joinable()) worker_.join();
    toast_.Destroy();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    g_watcher = nullptr;
}

void UpdateWatcher::CheckAsync() {
    if (busy_.exchange(true)) return;
    if (worker_.joinable()) worker_.join();
    Settings::Reload();
    if (!Settings::GetBool(L"autoUpdateCheck", true)) {
        busy_ = false;
        return;
    }
    const bool test = Settings::GetBool(L"updateNotifyTest", false);
    const std::wstring notified = Settings::Get(L"updateNotified");
    const HWND hwnd = hwnd_;
    worker_ = std::thread([this, hwnd, test, notified] {
        const Updater::CheckResult r = Updater::Check();
        if (r.ok && (test || (Updater::Compare(r.latest, Updater::Current()) > 0 && r.latest != notified)))
            PostMessageW(hwnd, WM_UPDATE_FOUND, 0, reinterpret_cast<LPARAM>(new std::wstring(r.latest)));
        busy_ = false;
    });
}

LRESULT CALLBACK UpdateWatcher::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UpdateWatcher* self = g_watcher;
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_TIMER) {
        if (wp == kFirstCheck) KillTimer(hwnd, kFirstCheck);
        self->CheckAsync();
        return 0;
    }
    if (msg == WM_UPDATE_FOUND) {
        std::unique_ptr<std::wstring> version(reinterpret_cast<std::wstring*>(lp));
        Settings::Set(L"updateNotified", *version);
        if (Settings::GetBool(L"updateNotifyTest", false)) Settings::Set(L"updateNotifyTest", L"0");
        Notification::Content c;
        c.title = L"Dostępna Veyra " + *version;
        c.body = L"Masz wersję " + Updater::Current() + L". Aktualizacja zajmie chwilę — Veyra uruchomi się ponownie.";
        c.action = L"Aktualizuj";
        c.dismiss = L"Później";
        c.onAction = [] {
            ShellExecuteW(nullptr, L"open", (ExeDir() + L"exploder-settings.exe").c_str(), L"--aktualizacje", nullptr, SW_SHOWNORMAL);
        };
        self->toast_.Show(std::move(c), self->dpi_ ? self->dpi_() : 96);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
