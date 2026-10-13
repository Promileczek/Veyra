#include "AppWindow.h"
#include "Accent.h"
#include "TrafficLights.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr UINT WM_REDRAW = WM_APP + 1;
constexpr float kBorder = 6.f;
}  // namespace

bool AppWindow::Create(HINSTANCE inst, Graphics& g, const Options& o) {
    inst_ = inst;
    g_ = &g;
    options_ = o;
    title_ = o.title;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = o.className;
    wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(o.iconId), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(o.iconId), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), 0));
    if (!RegisterClassExW(&wc)) return false;

    // Środek obszaru roboczego monitora z kursorem
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
    UINT dpiX = 96;
    {
        HDC screen = GetDC(nullptr);
        dpiX = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }
    const int w = static_cast<int>(o.width * dpiX / 96.f), h = static_cast<int>(o.height * dpiX / 96.f);
    const RECT& wa = mi.rcWork;
    const DWORD style = o.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, o.className, title_.c_str(), style,
                            (wa.left + wa.right - w) / 2, (wa.top + wa.bottom - h) / 2, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    const MARGINS shadow{0, 0, 0, 1};  // cień DWM mimo braku ramki
    DwmExtendFrameIntoClientArea(hwnd_, &shadow);
    if (o.blur) SetBackdrop(hwnd_, Backdrop::Blur, 0x00000000);

    titleFormat_ = MakeFormat(L"Segoe UI", 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (!surface_.Create(*g_, hwnd_, static_cast<UINT>(std::max<LONG>(rc.right, 1)), static_cast<UINT>(std::max<LONG>(rc.bottom, 1))))
        return false;
    surface_.Context()->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    surface_.Context()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    surface_.Context()->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &brush_);
    widthDip_ = rc.right * 96.f / dpi_;
    heightDip_ = rc.bottom * 96.f / dpi_;
    OnResize();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return true;
}

int AppWindow::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

ComPtr<IDWriteTextFormat> AppWindow::MakeFormat(const wchar_t* font, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) const {
    ComPtr<IDWriteTextFormat> f;
    g_->DWrite()->CreateTextFormat(font, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"pl-PL", &f);
    if (!f) return f;
    f->SetTextAlignment(align);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> ellipsis;
    g_->DWrite()->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
    f->SetTrimming(&trimming, ellipsis.Get());
    return f;
}

void AppWindow::Redraw() {
    if (redrawPending_ || !hwnd_) return;
    redrawPending_ = true;
    PostMessageW(hwnd_, WM_REDRAW, 0, 0);
}

void AppWindow::SetTitle(const std::wstring& t) {
    if (t == title_) return;
    title_ = t;
    SetWindowTextW(hwnd_, t.c_str());  // dymek w docku, Alt+Tab
    Redraw();
}

void AppWindow::Fill(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius) {
    brush_->SetColor(c);
    if (radius > 0.f) dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
    else dc->FillRectangle(r, brush_.Get());
}

void AppWindow::Text(ID2D1DeviceContext* dc, const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    if (!f) return;
    brush_->SetColor(c);
    dc->DrawText(s.c_str(), static_cast<UINT32>(s.size()), f, r, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void AppWindow::Render() {
    if (rendering_) return Redraw();
    rendering_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{rendering_};
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc || !brush_) return;
    Paint(dc);
    if (showTitle_) Text(dc, title_, titleFormat_.Get(), {80.f, 0.f, widthDip_ - 80.f, titleH_}, D2D1::ColorF(1.f, 1.f, 1.f, active_ ? 0.85f : 0.5f));
    TrafficLights::Draw(dc, brush_.Get(), Lights(), hoverLights_, active_);
    if (!surface_.End()) {  // urządzenie utracone — odtworzenie przy następnym rysowaniu
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        surface_ = Surface{};
        brush_.Reset();
        if (surface_.Create(*g_, hwnd_, static_cast<UINT>(std::max<LONG>(rc.right, 1)), static_cast<UINT>(std::max<LONG>(rc.bottom, 1)))) {
            surface_.Context()->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
            surface_.Context()->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &brush_);
            Redraw();
        }
    }
}

// Kropki po prawej (jak na oknach innych programów): grupa kończy się 12 DIP od prawej krawędzi
D2D1_POINT_2F AppWindow::Lights() const { return {widthDip_ - 58.f, 20.f}; }

D2D1_POINT_2F AppWindow::ToDip(LPARAM lp) const { return {GET_X_LPARAM(lp) * 96.f / dpi_, GET_Y_LPARAM(lp) * 96.f / dpi_}; }

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {  // uchwyt od razu — przed pierwszym komunikatem do HandleMessage
        auto* self = static_cast<AppWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        PostQuitMessage(0);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->HandleMessage(msg, wp, lp);
}

LRESULT AppWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r = 0; OnMessage(msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) {  // bez ramki systemowej; zmaksymalizowane okno przycinamy o grubość ramki
            if (IsZoomed(hwnd_)) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                const int f = GetSystemMetricsForDpi(SM_CXFRAME, dpi_) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
                InflateRect(&p->rgrc[0], -f, -f);
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        const D2D1_POINT_2F p{pt.x * 96.f / dpi_, pt.y * 96.f / dpi_};
        if (options_.resizable && !IsZoomed(hwnd_)) {  // krawędzie do zmiany rozmiaru
            const bool l = p.x < kBorder, r = p.x > widthDip_ - kBorder, t = p.y < kBorder, b = p.y > heightDip_ - kBorder;
            if (t && l) return HTTOPLEFT;
            if (t && r) return HTTOPRIGHT;
            if (b && l) return HTBOTTOMLEFT;
            if (b && r) return HTBOTTOMRIGHT;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
            if (t) return HTTOP;
            if (b) return HTBOTTOM;
        }
        if (TrafficLights::HitTest(Lights(), p) < 0 && IsCaption(p)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCRBUTTONUP:
        if (wp == HTCAPTION) {  // menu systemowe okna (Przenieś, Zamknij…)
            const HMENU menu = GetSystemMenu(hwnd_, FALSE);
            const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, hwnd_, nullptr);
            if (cmd) PostMessageW(hwnd_, WM_SYSCOMMAND, static_cast<WPARAM>(cmd), 0);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        active_ = LOWORD(wp) != WA_INACTIVE;
        OnActivate(active_);
        Redraw();
        return 0;
    case WM_REDRAW:
        redrawPending_ = false;
        Render();
        return 0;
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        Render();
        return 0;
    case WM_SIZE:
        if (surface_.Context()) surface_.Resize(LOWORD(lp), HIWORD(lp));
        widthDip_ = LOWORD(lp) * 96.f / dpi_;
        heightDip_ = HIWORD(lp) * 96.f / dpi_;
        OnResize();
        Render();  // od razu — płynniej przy przeciąganiu krawędzi
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {static_cast<LONG>(options_.minWidth * dpi_ / 96.f),
                                                             static_cast<LONG>(options_.minHeight * dpi_ / 96.f)};
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        if (surface_.Context()) surface_.Context()->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_TIMER:
        OnTimer(wp);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp == VK_F11 && options_.resizable) {  // pełny ekran (zmaksymalizowane okno) / powrót
            ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        if (OnKeyDown(static_cast<UINT>(wp))) return 0;
        break;
    case WM_CHAR:
        OnChar(static_cast<wchar_t>(wp));
        return 0;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
        }
        const D2D1_POINT_2F p = ToDip(lp);
        const bool lights = p.y < titleH_ && p.x >= Lights().x - 10.f && p.x <= Lights().x + 50.f;
        if (lights != hoverLights_) {
            hoverLights_ = lights;
            Redraw();
        }
        OnMouseMove(p);
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        if (hoverLights_) {
            hoverLights_ = false;
            Redraw();
        }
        OnMouseLeave();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        const D2D1_POINT_2F p = ToDip(lp);
        if (msg == WM_LBUTTONDOWN) {
            switch (TrafficLights::HitTest(Lights(), p)) {
            case 0: PostMessageW(hwnd_, WM_CLOSE, 0, 0); return 0;
            case 1: ShowWindow(hwnd_, SW_MINIMIZE); return 0;
            case 2:
                if (options_.resizable) ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            default: break;
            }
        }
        SetFocus(hwnd_);
        OnMouseDown(p, msg == WM_RBUTTONDOWN);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        OnDoubleClick(ToDip(lp));
        return 0;
    case WM_LBUTTONUP:
        OnMouseUp(ToDip(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        OnWheel(GET_WHEEL_DELTA_WPARAM(wp), {pt.x * 96.f / dpi_, pt.y * 96.f / dpi_});
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
