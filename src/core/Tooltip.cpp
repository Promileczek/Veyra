#include "Tooltip.h"
#include "Accent.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr wchar_t kClassName[] = L"ExploderTooltip";
constexpr float kPadX = 12.f, kMaxWidth = 340.f, kRadius = 9.f;
}  // namespace

bool Tooltip::Create(HINSTANCE inst, Graphics& g) {
    g_ = &g;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, kClassName, L"",
                            WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    if (!hwnd_) return false;
    SetBackdrop(hwnd_, Backdrop::Acrylic, 0x90201C1C);

    IDWriteFactory* dw = g_->DWrite();
    if (FAILED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, 12.f, L"pl-PL", &titleFormat_)) ||
        FAILED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, 11.f, L"pl-PL", &subFormat_)))
        return false;
    dw->CreateEllipsisTrimmingSign(subFormat_.Get(), &ellipsis_);
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    for (IDWriteTextFormat* f : {titleFormat_.Get(), subFormat_.Get()}) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetTrimming(&trimming, ellipsis_.Get());
    }
    return true;
}

float Tooltip::Measure(const std::wstring& text, IDWriteTextFormat* format) const {
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS m{};
    if (text.empty() || FAILED(g_->DWrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, 10000.f,
                                                              100.f, &layout)))
        return 0.f;
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void Tooltip::Show(const std::wstring& title, const std::wstring& subtitle, int anchorX, int bottomY, UINT dpi) {
    if (!hwnd_) return;
    title_ = title;
    subtitle_ = subtitle == title ? std::wstring() : subtitle;  // bez powtórzeń ("Discord" / "Discord")
    dpi_ = dpi;
    const float w = std::min(kMaxWidth, std::max(Measure(title_, titleFormat_.Get()), Measure(subtitle_, subFormat_.Get())) + 2 * kPadX);
    const float h = subtitle_.empty() ? 26.f : 42.f;
    const float s = dpi_ / 96.f;
    const int pw = static_cast<int>(std::lround(w * s)), ph = static_cast<int>(std::lround(h * s));

    MONITORINFO mi{sizeof(mi)};
    const POINT anchor{anchorX, bottomY};
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi);
    const int x = std::clamp(anchorX - pw / 2, static_cast<int>(mi.rcMonitor.left) + 4, static_cast<int>(mi.rcMonitor.right) - pw - 4);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, bottomY - ph, pw, ph, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Render();
}

void Tooltip::Hide() {
    if (hwnd_ && IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
}

void Tooltip::Render() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    widthDip_ = rc.right * 96.f / dpi_;
    heightDip_ = rc.bottom * 96.f / dpi_;
    if (!surface_.Context()) {
        if (!surface_.Create(*g_, hwnd_, static_cast<UINT>(std::max<LONG>(rc.right, 1)), static_cast<UINT>(std::max<LONG>(rc.bottom, 1))))
            return;
        surface_.Context()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        surface_.Context()->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &brush_);
    } else {
        surface_.Resize(static_cast<UINT>(std::max<LONG>(rc.right, 1)), static_cast<UINT>(std::max<LONG>(rc.bottom, 1)));
    }
    const int d = static_cast<int>(std::lround(kRadius * 2 * dpi_ / 96.f));
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, d, d), TRUE);

    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc || !brush_) return;
    dc->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    const float W = widthDip_, H = heightDip_, px = 96.f / dpi_;
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto shape = D2D1::RoundedRect({px / 2, px / 2, W - px / 2, H - px / 2}, kRadius, kRadius);
    brush_->SetColor(D2D1::ColorF(0.10f, 0.10f, 0.12f, 0.45f));
    dc->FillRoundedRectangle(shape, brush_.Get());
    brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.22f));
    dc->DrawRoundedRectangle(shape, brush_.Get(), px);

    brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 1.f));
    if (subtitle_.empty()) {
        dc->DrawText(title_.c_str(), static_cast<UINT32>(title_.size()), titleFormat_.Get(), {kPadX, 0.f, W - kPadX, H}, brush_.Get());
    } else {
        dc->DrawText(title_.c_str(), static_cast<UINT32>(title_.size()), titleFormat_.Get(), {kPadX, 3.f, W - kPadX, 22.f}, brush_.Get());
        brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.65f));
        dc->DrawText(subtitle_.c_str(), static_cast<UINT32>(subtitle_.size()), subFormat_.Get(), {kPadX, 21.f, W - kPadX, H - 4.f},
                     brush_.Get());
    }
    if (!surface_.End()) {
        surface_ = Surface{};
        brush_.Reset();
    }
}

LRESULT CALLBACK Tooltip::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;  // kliknięcia przechodzą dalej
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}
