#include "Popup.h"
#include "Accent.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kRadius = 14.f;

IDWriteTextFormat* MakeFormat(IDWriteFactory* dw, const wchar_t* font, DWRITE_FONT_WEIGHT weight, float size,
                              ComPtr<IDWriteTextFormat>& out) {
    if (FAILED(dw->CreateTextFormat(font, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"pl-PL",
                                    &out)))
        return nullptr;
    out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return out.Get();
}
}  // namespace

bool Popup::Create(HINSTANCE inst, Graphics& g, const wchar_t* className) {
    g_ = &g;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) return false;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, className, L"", WS_POPUP, 0, 0, 1, 1,
                            nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    SetBackdrop(hwnd_, Backdrop::Acrylic, 0x70201C1C);

    IDWriteFactory* dw = g_->DWrite();
    if (!MakeFormat(dw, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.f, titleFormat_) ||
        !MakeFormat(dw, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f, textFormat_) ||
        !MakeFormat(dw, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.f, smallFormat_) ||
        !MakeFormat(dw, L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_NORMAL, 15.f, glyphFormat_))
        return false;
    glyphFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    ComPtr<IDWriteInlineObject> ellipsis;
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    for (IDWriteTextFormat* f : {titleFormat_.Get(), textFormat_.Get(), smallFormat_.Get()}) {
        ellipsis.Reset();
        dw->CreateEllipsisTrimmingSign(f, &ellipsis);
        f->SetTrimming(&trimming, ellipsis.Get());
    }
    return true;
}

void Popup::Toggle(const RECT& anchor, UINT dpi) {
    if (Visible()) {
        Hide();
        return;
    }
    anchor_ = anchor;
    dpi_ = dpi;
    OnShow();
    Place();
    ShowWindow(hwnd_, SW_SHOW);
    if (!SetForegroundWindow(hwnd_) || GetForegroundWindow() != hwnd_) {
        // Windows blokuje zmianę okna na pierwszym planie — podpięcie pod kolejkę wejścia aktywnego okna to obchodzi
        const HWND fg = GetForegroundWindow();
        const DWORD me = GetCurrentThreadId(), fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
        const bool attached = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
        BringWindowToTop(hwnd_);
        SetForegroundWindow(hwnd_);
        if (attached) AttachThreadInput(me, fgThread, FALSE);
    }
    Render();
}

void Popup::Hide() {
    if (!Visible()) return;
    Capture(false);
    ShowWindow(hwnd_, SW_HIDE);
}

void Popup::Redraw() {
    if (Visible()) Render();
}

void Popup::Relayout() {
    if (!Visible()) return;
    Place();
    Render();
}

void Popup::Place() {
    size_ = ContentSize();
    const float s = dpi_ / 96.f;
    const int w = static_cast<int>(std::lround(size_.width * s)), h = static_cast<int>(std::lround(size_.height * s));
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&anchor_, MONITOR_DEFAULTTOPRIMARY), &mi);
    const int gap = static_cast<int>(8.f * s);
    const int x = std::clamp(static_cast<int>((anchor_.left + anchor_.right) / 2 - w / 2), static_cast<int>(mi.rcMonitor.left) + gap,
                             static_cast<int>(mi.rcMonitor.right) - w - gap);
    // Pod przyciskiem; gdy brak miejsca (np. przycisk w docku na dole) — nad nim
    const int y = anchor_.bottom + gap + h <= mi.rcMonitor.bottom ? anchor_.bottom + gap : std::max<int>(mi.rcMonitor.top, anchor_.top - gap - h);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    const int d = static_cast<int>(std::lround(kRadius * 2 * s));
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d), TRUE);
}

void Popup::Render() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT w = static_cast<UINT>(std::max<LONG>(rc.right, 1)), h = static_cast<UINT>(std::max<LONG>(rc.bottom, 1));
    if (!surface_.Context()) {
        if (!surface_.Create(*g_, hwnd_, w, h)) return;
        surface_.Context()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        surface_.Context()->CreateSolidColorBrush(kText, &brush_);
    } else {
        surface_.Resize(w, h);
    }
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc || !brush_) return;
    dc->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    const float W = size_.width, H = size_.height, px = 96.f / dpi_;
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto shape = D2D1::RoundedRect({px / 2, px / 2, W - px / 2, H - px / 2}, kRadius, kRadius);
    brush_->SetColor(D2D1::ColorF(0.10f, 0.10f, 0.12f, 0.40f));
    dc->FillRoundedRectangle(shape, brush_.Get());
    brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.20f));
    dc->DrawRoundedRectangle(shape, brush_.Get(), px);
    Paint(dc);
    if (!surface_.End()) {
        surface_ = Surface{};
        brush_.Reset();
    }
}

void Popup::Fill(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius) {
    brush_->SetColor(c);
    if (radius > 0.f) dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
    else dc->FillRectangle(r, brush_.Get());
}

void Popup::Text(ID2D1DeviceContext* dc, const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r,
                 const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    dc->DrawText(s.c_str(), static_cast<UINT32>(s.size()), f, r, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Popup::Glyph(ID2D1DeviceContext* dc, wchar_t g, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    dc->DrawText(&g, 1, glyphFormat_.Get(), r, brush_.Get());
}

void Popup::Capture(bool on) {
    if (on == captured_) return;
    captured_ = on;
    if (on) SetCapture(hwnd_);
    else ReleaseCapture();
}

D2D1_POINT_2F Popup::ToDip(LPARAM lp) const { return {GET_X_LPARAM(lp) * 96.f / dpi_, GET_Y_LPARAM(lp) * 96.f / dpi_}; }

LRESULT CALLBACK Popup::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<Popup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Popup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Popup::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        if (Visible()) Render();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) Hide();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) Hide();
        return 0;
    case WM_TIMER:
        OnTimer(wp);
        return 0;
    case WM_CHAR:
        OnChar(static_cast<wchar_t>(wp));
        return 0;
    case WM_MOUSEMOVE:
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
        }
        OnMouseMove(ToDip(lp));
        return 0;
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        OnMouseMove({-1.f, -1.f});
        return 0;
    case WM_LBUTTONDOWN:
        OnMouseDown(ToDip(lp));
        return 0;
    case WM_LBUTTONUP:
        OnMouseUp(ToDip(lp));
        return 0;
    case WM_CAPTURECHANGED:
        captured_ = false;
        return 0;
    case WM_MOUSEWHEEL:
        OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
