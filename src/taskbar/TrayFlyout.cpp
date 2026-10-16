#include "TrayFlyout.h"
#include "../core/Accent.h"
#include "AppIcons.h"
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr wchar_t kClassName[] = L"ExploderTrayFlyout";
constexpr float kWidth = 260.f, kHeader = 34.f, kRow = 30.f, kPad = 6.f, kRadius = 12.f, kIcon = 16.f, kGap = 8.f;

std::wstring FirstLine(const std::wstring& s) {
    const size_t nl = s.find_first_of(L"\r\n");
    return nl == std::wstring::npos ? s : s.substr(0, nl);
}
}  // namespace

bool TrayFlyout::Create(HINSTANCE inst, Graphics& g, TrayService& tray, const TaskbarTheme& theme,
                        std::function<void()> onVisibilityChange) {
    g_ = &g;
    tray_ = &tray;
    theme_ = &theme;
    onVisibilityChange_ = std::move(onVisibilityChange);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClassName, L"Tray",
                            WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    SetBackdrop(hwnd_, theme_->acrylic ? Backdrop::Acrylic : Backdrop::None, theme_->acrylicTint);

    if (FAILED(g_->DWrite()->CreateTextFormat(theme_->font, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, 13.f, L"pl-PL", &titleFormat_)) ||
        FAILED(g_->DWrite()->CreateTextFormat(theme_->font, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, 12.f, L"pl-PL", &rowFormat_)))
        return false;
    titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rowFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rowFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    g_->DWrite()->CreateEllipsisTrimmingSign(rowFormat_.Get(), &ellipsis_);
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    rowFormat_->SetTrimming(&trimming, ellipsis_.Get());
    return true;
}

bool TrayFlyout::CreateResources() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    surface_ = Surface{};
    brush_.Reset();
    if (!surface_.Create(*g_, hwnd_, static_cast<UINT>(std::max<LONG>(rc.right, 1)),
                         static_cast<UINT>(std::max<LONG>(rc.bottom, 1))))
        return false;
    surface_.Context()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return SUCCEEDED(surface_.Context()->CreateSolidColorBrush(theme_->text, &brush_));
}

void TrayFlyout::Toggle(const RECT& anchor, UINT dpi) {
    if (Visible()) {
        Hide();
        return;
    }
    anchor_ = anchor;
    dpi_ = dpi;
    hover_ = -1;
    Place();
    Render();
    RECT wr{};  // wysuwa się z dołu (nad dockiem)
    GetWindowRect(hwnd_, &wr);
    surface_.Appear(0.18f, 0.94f, (wr.right - wr.left) / 2.f, static_cast<float>(wr.bottom - wr.top), 10.f * dpi_ / 96.f);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);  // aktywny panel → kliknięcie gdziekolwiek indziej go zamknie
    if (onVisibilityChange_) onVisibilityChange_();
}

void TrayFlyout::Hide() {
    if (!Visible()) return;
    ShowWindow(hwnd_, SW_HIDE);
    hover_ = -1;
    if (onVisibilityChange_) onVisibilityChange_();
}

void TrayFlyout::Refresh() {
    if (!Visible()) return;
    Place();
    Render();
}

void TrayFlyout::Place() {
    rows_.clear();
    const auto& icons = tray_->Icons();
    for (size_t i = 0; i < icons.size(); ++i)
        if (!icons[i].hidden) rows_.push_back(i);

    const float s = dpi_ / 96.f;
    const float hDip = kHeader + std::max<size_t>(rows_.size(), 1) * kRow + kPad;
    const int w = static_cast<int>(std::lround(kWidth * s)), h = static_cast<int>(std::lround(hDip * s));
    const int gap = static_cast<int>(std::lround(8.f * s));

    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&anchor_, MONITOR_DEFAULTTOPRIMARY), &mi);
    int x = (anchor_.left + anchor_.right) / 2 - w / 2;
    x = std::clamp(x, static_cast<int>(mi.rcMonitor.left) + gap, static_cast<int>(mi.rcMonitor.right) - w - gap);
    const int y = anchor_.top - h - gap;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void TrayFlyout::Render() {
    if (!surface_.Context() && !CreateResources()) return;
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc) return;
    dc->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    const float W = widthDip_, H = heightDip_, px = 96.f / dpi_;

    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto shape = D2D1::RoundedRect({px / 2, px / 2, W - px / 2, H - px / 2}, kRadius, kRadius);
    brush_->SetColor(theme_->dockBackground);
    dc->FillRoundedRectangle(shape, brush_.Get());
    brush_->SetColor(theme_->dockBorder);
    dc->DrawRoundedRectangle(shape, brush_.Get(), px);

    brush_->SetColor(theme_->text);
    dc->DrawText(L"Tray", 4, titleFormat_.Get(), {14.f, 0.f, W - 14.f, kHeader}, brush_.Get());
    brush_->SetColor(theme_->dockBorder);
    dc->FillRectangle({kPad, kHeader - px, W - kPad, kHeader}, brush_.Get());

    auto& icons = tray_->Icons();
    if (rows_.empty()) {
        brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.6f));
        dc->DrawText(L"Brak ikon", 9, rowFormat_.Get(), {14.f, kHeader, W - 14.f, kHeader + kRow}, brush_.Get());
    }
    for (size_t r = 0; r < rows_.size(); ++r) {
        if (rows_[r] >= icons.size()) break;
        TrayIcon& icon = icons[rows_[r]];
        const float top = kHeader + r * kRow;
        if (hover_ == static_cast<int>(r)) {
            brush_->SetColor(theme_->hover);
            dc->FillRoundedRectangle(D2D1::RoundedRect({kPad, top + 2.f, W - kPad, top + kRow - 2.f}, 6.f, 6.f),
                                     brush_.Get());
        }
        const float iconX = 14.f, cy = top + kRow / 2;
        if (ID2D1Bitmap* bmp = TrayIconBitmap(icon, g_->WIC(), dc)) {
            const D2D1_RECT_F dst{iconX, cy - kIcon / 2, iconX + kIcon, cy + kIcon / 2};
            dc->DrawBitmap(bmp, &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
        std::wstring label = FirstLine(icon.tip);
        if (label.empty() && icon.owner) label = AppNameForWindow(icon.owner);  // pusta podpowiedź (Discord, BlueStacks…): nazwa programu
        if (label.empty()) label = L"(bez opisu)";
        brush_->SetColor(theme_->text);
        dc->DrawText(label.c_str(), static_cast<UINT32>(label.size()), rowFormat_.Get(),
                     {iconX + kIcon + kGap + 2.f, top, W - 14.f, top + kRow}, brush_.Get());
    }
    if (!surface_.End()) {  // urządzenie utracone — odtworzymy przy następnym rysowaniu
        surface_ = Surface{};
        brush_.Reset();
    }
}

int TrayFlyout::HitTest(LPARAM lp) const {
    const float y = GET_Y_LPARAM(lp) * 96.f / dpi_;
    if (y < kHeader) return -1;
    const int row = static_cast<int>((y - kHeader) / kRow);
    return row < static_cast<int>(rows_.size()) ? row : -1;
}

void TrayFlyout::Send(int row, UINT mouseMsg) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const auto& icons = tray_->Icons();
    if (rows_[static_cast<size_t>(row)] >= icons.size()) return;
    const TrayIcon& icon = icons[rows_[static_cast<size_t>(row)]];
    POINT pt;
    GetCursorPos(&pt);
    tray_->SendMouse(icon, mouseMsg, pt);
    // Od wersji 3 explorer dosyła też zdarzenia "wysokiego poziomu".
    if (icon.version >= NOTIFYICON_VERSION) {
        if (mouseMsg == WM_LBUTTONUP) tray_->SendMouse(icon, NIN_SELECT, pt);
        if (mouseMsg == WM_RBUTTONUP) tray_->SendMouse(icon, WM_CONTEXTMENU, pt);
    }
}

LRESULT CALLBACK TrayFlyout::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<TrayFlyout*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<TrayFlyout*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrayFlyout::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE: {
        widthDip_ = LOWORD(lp) * 96.f / dpi_;
        heightDip_ = HIWORD(lp) * 96.f / dpi_;
        if (surface_.Context()) surface_.Resize(LOWORD(lp), HIWORD(lp));
        const int d = static_cast<int>(std::lround(kRadius * 2 * dpi_ / 96.f));
        SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, LOWORD(lp) + 1, HIWORD(lp) + 1, d, d), TRUE);
        if (Visible()) Render();
        return 0;
    }
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        Render();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) Hide();  // kliknięcie poza panelem
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) Hide();
        return 0;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
        }
        const int hit = HitTest(lp);
        if (hit != hover_) {
            hover_ = hit;
            Render();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        hover_ = -1;
        Render();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        Send(HitTest(lp), msg);
        return 0;
    case WM_LBUTTONDBLCLK:
        Send(HitTest(lp), msg);
        Hide();
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
        const int row = HitTest(lp);
        if (row < 0) return 0;
        Send(row, msg);
        Hide();  // program pokazuje swoje okno/menu — panel nie może go zasłaniać
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
