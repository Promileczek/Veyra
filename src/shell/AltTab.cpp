#include "AltTab.h"
#include "../core/UiScale.h"
#include "../core/Accent.h"
#include "WindowTracker.h"
#include <shellscalingapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr wchar_t kClassName[] = L"ExploderAltTab";
constexpr UINT_PTR kShowTimer = 1;
constexpr UINT kShowDelayMs = 120;  // krótkie Alt+Tab przełącza bez pokazywania panelu
constexpr float kCardW = 212.f, kCardH = 170.f, kGap = 10.f, kPad = 18.f, kThumbW = 196.f, kThumbH = 122.f;
constexpr int kMaxColumns = 6, kMaxCards = 18;

AltTab* g_instance = nullptr;
}  // namespace

bool AltTab::Install(HINSTANCE inst, Graphics& g) {
    g_ = &g;
    g_instance = this;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, kClassName, L"", WS_POPUP,
                            0, 0, 1, 1, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    SetBackdrop(hwnd_, Backdrop::Acrylic, 0x80201C1C);
    g_->DWrite()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                   12.f, L"pl-PL", &titleFormat_);
    if (titleFormat_) {
        titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteInlineObject> ellipsis;
        g_->DWrite()->CreateEllipsisTrimmingSign(titleFormat_.Get(), &ellipsis);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        titleFormat_->SetTrimming(&trimming, ellipsis.Get());
    }
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, inst, 0);
    return hook_ != nullptr;
}

void AltTab::Uninstall() {
    if (hook_) UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
    Close();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    g_instance = nullptr;
}

LRESULT CALLBACK AltTab::KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_instance) {
        const auto& k = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (!(k.flags & LLKHF_INJECTED) && g_instance->OnKey(wp, k)) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// Hak musi wracać szybko — ciężką pracę (lista okien, rysowanie) robimy przez PostMessage.
bool AltTab::OnKey(WPARAM wp, const KBDLLHOOKSTRUCT& k) {
    const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
    const bool alt = (k.flags & LLKHF_ALTDOWN) != 0;
    if (k.vkCode == VK_TAB && down && alt) {
        const bool shift = GetAsyncKeyState(VK_SHIFT) < 0;
        if (!active_) {
            active_ = true;
            // Sztuczny klawisz: puszczenie samego Alt nie otworzy menu w aktywnym programie
            INPUT in[2]{};
            in[0].type = in[1].type = INPUT_KEYBOARD;
            in[0].ki.wVk = in[1].ki.wVk = 0xE8;
            in[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT));
            PostMessageW(hwnd_, WM_APP + 1, shift, 0);  // Open
        } else {
            PostMessageW(hwnd_, WM_APP + 2, shift ? static_cast<WPARAM>(-1) : 1, 0);  // Move
        }
        return true;
    }
    if (!active_) return false;
    if ((k.vkCode == VK_LMENU || k.vkCode == VK_RMENU || k.vkCode == VK_MENU) && !down) {
        // Przełączenie po wyjściu z haka (hak musi wracać szybko; AttachThreadInput w haku potrafi zablokować wejście)
        PostMessageW(hwnd_, WM_APP + 4, 0, 0);
        return false;  // puszczenie Alt idzie dalej
    }
    if (!down) return k.vkCode == VK_TAB || k.vkCode == VK_ESCAPE;
    switch (k.vkCode) {
    case VK_ESCAPE: PostMessageW(hwnd_, WM_APP + 3, 0, 0); return true;  // Close
    case VK_LEFT: PostMessageW(hwnd_, WM_APP + 2, static_cast<WPARAM>(-1), 0); return true;
    case VK_RIGHT: PostMessageW(hwnd_, WM_APP + 2, 1, 0); return true;
    case VK_UP: PostMessageW(hwnd_, WM_APP + 2, static_cast<WPARAM>(-columns_), 0); return true;
    case VK_DOWN: PostMessageW(hwnd_, WM_APP + 2, static_cast<WPARAM>(columns_), 0); return true;
    case VK_RETURN: PostMessageW(hwnd_, WM_APP + 4, 0, 0); return true;
    }
    return false;
}

void AltTab::Open(bool backwards) {
    ReleaseThumbnails();
    cards_.clear();
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto* self = reinterpret_cast<AltTab*>(p);
            if (WindowTracker::IsTaskbarWindow(h) && self->cards_.size() < kMaxCards) {
                Card c;
                c.hwnd = h;
                wchar_t title[256]{};
                GetWindowTextW(h, title, ARRAYSIZE(title));
                c.title = title;
                self->cards_.push_back(std::move(c));
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));  // kolejność Z = od ostatnio używanego
    if (cards_.empty()) {
        active_ = false;
        return;
    }
    selected_ = cards_.size() > 1 ? (backwards ? static_cast<int>(cards_.size()) - 1 : 1) : 0;
    hover_ = -1;
    shown_ = false;
    SetTimer(hwnd_, kShowTimer, kShowDelayMs, nullptr);
}

void AltTab::Move(int delta) {
    if (!active_ || cards_.empty()) return;
    const int n = static_cast<int>(cards_.size());
    selected_ = ((selected_ + delta) % n + n) % n;
    if (!shown_) ShowPanel();
    else Render();
}

void AltTab::ShowPanel() {
    KillTimer(hwnd_, kShowTimer);
    if (!active_ || cards_.empty()) return;
    // Panel na monitorze aktywnego okna
    HMONITOR mon = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    dpi_ = UiScale::Dpi(dpiX);
    columns_ = std::min<int>(static_cast<int>(cards_.size()), kMaxColumns);
    const int rows = (static_cast<int>(cards_.size()) + columns_ - 1) / columns_;
    widthDip_ = 2 * kPad + columns_ * kCardW + (columns_ - 1) * kGap;
    heightDip_ = 2 * kPad + rows * kCardH + (rows - 1) * kGap;
    const float s = dpi_ / 96.f;
    const int w = static_cast<int>(std::lround(widthDip_ * s)), h = static_cast<int>(std::lround(heightDip_ * s));
    const int x = (mi.rcMonitor.left + mi.rcMonitor.right - w) / 2, y = (mi.rcMonitor.top + mi.rcMonitor.bottom - h) / 2;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    const int d = static_cast<int>(std::lround(24.f * s));
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d), TRUE);
    Layout();
    ShowWindow(hwnd_, SW_SHOWNA);
    shown_ = true;
    Render();
}

void AltTab::Layout() {
    const float s = dpi_ / 96.f;
    for (size_t i = 0; i < cards_.size(); ++i) {
        Card& c = cards_[i];
        const float x = kPad + static_cast<float>(i % columns_) * (kCardW + kGap);
        const float y = kPad + static_cast<float>(i / columns_) * (kCardH + kGap);
        c.rect = {x, y, x + kCardW, y + kCardH};
        // Także zminimalizowane: DWM trzyma ich ostatni obraz w pełnym rozmiarze. Gdy go nie ma (rozmiar paska
        // zminimalizowanego okna) — duża ikona zamiast podglądu.
        if (FAILED(DwmRegisterThumbnail(hwnd_, c.hwnd, &c.thumb))) continue;
        SIZE src{};
        DwmQueryThumbnailSourceSize(c.thumb, &src);
        if (src.cx <= 240 || src.cy <= 60) {
            DwmUnregisterThumbnail(c.thumb);
            c.thumb = nullptr;
            continue;
        }
        // Podgląd wpisany w pole z zachowaniem proporcji, wyśrodkowany
        const float boxX = (x + 8.f) * s, boxY = (y + 8.f) * s;  // wszystko w pikselach
        const float bw = kThumbW * s, bh = kThumbH * s;
        const float k = std::min(bw / src.cx, bh / src.cy);
        const float tw = src.cx * k, th = src.cy * k;
        DWM_THUMBNAIL_PROPERTIES props{};
        props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        props.rcDestination = {static_cast<LONG>(boxX + (bw - tw) / 2), static_cast<LONG>(boxY + (bh - th) / 2),
                               static_cast<LONG>(boxX + (bw + tw) / 2), static_cast<LONG>(boxY + (bh + th) / 2)};
        props.fVisible = TRUE;
        props.opacity = 255;
        props.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(c.thumb, &props);
    }
}

void AltTab::Render() {
    if (rendering_) return;
    rendering_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{rendering_};
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT w = static_cast<UINT>(std::max<LONG>(rc.right, 1)), h = static_cast<UINT>(std::max<LONG>(rc.bottom, 1));
    if (!surface_.Context()) {
        if (!surface_.Create(*g_, hwnd_, w, h)) return;
        surface_.Context()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        surface_.Context()->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &brush_);
    } else {
        surface_.Resize(w, h);
    }
    // Ikony przed rysowaniem; lista kart mogła się zmienić w trakcie (Open/Close przez komunikaty)
    const int iconPx = static_cast<int>(std::ceil(48.f * dpi_ / 96.f));
    for (size_t i = 0; i < cards_.size(); ++i) {
        if (cards_[i].icon.bitmap || cards_[i].icon.builtin != BuiltinIcon::None || !cards_[i].icon.appName.empty()) continue;
        const HWND target = cards_[i].hwnd;
        AppIcon icon = ResolveAppIcon(target, iconPx, g_->WIC(), surface_.Context());
        if (i < cards_.size() && cards_[i].hwnd == target) cards_[i].icon = std::move(icon);
    }
    if (!active_) return;
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc || !brush_) return;
    dc->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    const float px = 96.f / dpi_;
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto shape = D2D1::RoundedRect({px / 2, px / 2, widthDip_ - px / 2, heightDip_ - px / 2}, 12.f, 12.f);
    brush_->SetColor(D2D1::ColorF(0.08f, 0.08f, 0.10f, 0.40f));
    dc->FillRoundedRectangle(shape, brush_.Get());
    brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.22f));
    dc->DrawRoundedRectangle(shape, brush_.Get(), px);

    for (size_t i = 0; i < cards_.size(); ++i) {
        Card& c = cards_[i];
        const bool sel = static_cast<int>(i) == selected_;
        const auto card = D2D1::RoundedRect(c.rect, 10.f, 10.f);
        if (sel || hover_ == static_cast<int>(i)) {
            brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, sel ? 0.16f : 0.08f));
            dc->FillRoundedRectangle(card, brush_.Get());
        }
        if (sel) {
            brush_->SetColor(D2D1::ColorF(0.40f, 0.70f, 1.f, 0.95f));
            dc->DrawRoundedRectangle(card, brush_.Get(), 2.f);
        }
        auto drawIcon = [&](const D2D1_RECT_F& r) {
            if (c.icon.builtin != BuiltinIcon::None) DrawBuiltinIcon(dc, c.icon.builtin, r, 1.f);
            else if (c.icon.bitmap) dc->DrawBitmap(c.icon.bitmap.Get(), &r, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        };
        if (!c.thumb) {  // zminimalizowane: duża ikona zamiast podglądu
            const float cx = c.rect.left + kCardW / 2, cy = c.rect.top + 8.f + kThumbH / 2;
            drawIcon({cx - 28.f, cy - 28.f, cx + 28.f, cy + 28.f});
        }
        const float ty = c.rect.top + 8.f + kThumbH + 6.f;
        drawIcon({c.rect.left + 10.f, ty + 7.f, c.rect.left + 28.f, ty + 25.f});
        brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.95f));
        const std::wstring& title = c.title.empty() ? c.icon.appName : c.title;
        dc->DrawText(title.c_str(), static_cast<UINT32>(title.size()), titleFormat_.Get(), {c.rect.left + 34.f, ty, c.rect.right - 8.f, ty + 32.f},
                     brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (!surface_.End()) {
        surface_ = Surface{};
        brush_.Reset();
    }
}

void AltTab::Commit(int index) {
    if (!active_) return;
    const HWND target = index >= 0 && index < static_cast<int>(cards_.size()) ? cards_[static_cast<size_t>(index)].hwnd : nullptr;
    Close();
    if (!target || !IsWindow(target)) return;
    // Samo SetForegroundWindow bywa cofane (zdarzenie przychodzi, a na wierzchu zostaje stare okno, np. Discord/Brave).
    // Podpięcie się pod kolejkę wejścia aktywnego okna daje pewną zmianę.
    const HWND fg = GetForegroundWindow();
    const DWORD me = GetCurrentThreadId(), fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const bool attached = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
    BringWindowToTop(target);
    if (!SetForegroundWindow(target)) SwitchToThisWindow(target, TRUE);
    if (attached) AttachThreadInput(me, fgThread, FALSE);
}

void AltTab::Close() {
    active_ = false;
    shown_ = false;
    if (hwnd_) {
        KillTimer(hwnd_, kShowTimer);
        ShowWindow(hwnd_, SW_HIDE);
    }
    ReleaseThumbnails();
}

void AltTab::ReleaseThumbnails() {
    for (Card& c : cards_)
        if (c.thumb) {
            DwmUnregisterThumbnail(c.thumb);
            c.thumb = nullptr;
        }
}

LRESULT CALLBACK AltTab::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {  // uchwyt od razu — HandleMessage woła DefWindowProc(hwnd_) już dla WM_NCCREATE
        auto* created = static_cast<AltTab*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        created->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
    }
    auto* self = reinterpret_cast<AltTab*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AltTab::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP + 1:
        Open(wp != 0);
        return 0;
    case WM_APP + 2:
        Move(static_cast<int>(static_cast<INT_PTR>(wp)));
        return 0;
    case WM_APP + 3:
        Close();
        return 0;
    case WM_APP + 4:
        Commit(selected_);
        return 0;
    case WM_TIMER:
        if (wp == kShowTimer) ShowPanel();
        return 0;
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        if (shown_) Render();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        const float x = GET_X_LPARAM(lp) * 96.f / dpi_, y = GET_Y_LPARAM(lp) * 96.f / dpi_;
        int hit = -1;
        for (size_t i = 0; i < cards_.size(); ++i)
            if (x >= cards_[i].rect.left && x < cards_[i].rect.right && y >= cards_[i].rect.top && y < cards_[i].rect.bottom)
                hit = static_cast<int>(i);
        if (hit != hover_) {
            hover_ = hit;
            if (shown_) Render();
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (hover_ >= 0) Commit(hover_);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
