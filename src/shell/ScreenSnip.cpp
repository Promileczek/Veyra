#include "ScreenSnip.h"
#include <windowsx.h>
#include <algorithm>

namespace {
constexpr wchar_t kClassName[] = L"ExploderScreenSnip";

HBITMAP CreateDib(HDC dc, int w, int h, BYTE** bits) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // od góry
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(bits), nullptr, 0);
}

struct ButtonInfo {
    wchar_t glyph;
    const wchar_t* label;
};
const ButtonInfo kButtons[] = {{L'\xF407', L"Wycinek"}, {L'\xE7F4', L"Cały ekran"}, {L'\xE711', nullptr}};
}  // namespace

void ScreenSnip::Start(HINSTANCE inst) {
    if (hwnd_) return;
    screen_ = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    screen_.right = screen_.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    screen_.bottom = screen_.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int w = screen_.right - screen_.left, h = screen_.bottom - screen_.top;

    // Zrzut całego ekranu + przyciemniona kopia (tło poza zaznaczeniem)
    HDC screen = GetDC(nullptr);
    BYTE* shotBits = nullptr;
    BYTE* dimBits = nullptr;
    shotDC_ = CreateCompatibleDC(screen);
    dimDC_ = CreateCompatibleDC(screen);
    shot_ = CreateDib(screen, w, h, &shotBits);
    dim_ = CreateDib(screen, w, h, &dimBits);
    if (!shot_ || !dim_ || !shotBits || !dimBits) {
        ReleaseDC(nullptr, screen);
        Finish(false);
        return;
    }
    shotOld_ = SelectObject(shotDC_, shot_);
    dimOld_ = SelectObject(dimDC_, dim_);
    BitBlt(shotDC_, 0, 0, w, h, screen, screen_.left, screen_.top, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screen);
    GdiFlush();
    const size_t bytes = static_cast<size_t>(w) * h * 4;
    for (size_t i = 0; i < bytes; ++i) dimBits[i] = static_cast<BYTE>(shotBits[i] * 115 / 255);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);  // przy kolejnym wycinku klasa już istnieje
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"Wycinek ekranu", WS_POPUP, screen_.left, screen_.top,
                            w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) {
        Finish(false);
        return;
    }
    LayoutToolbar();
    ShowWindow(hwnd_, SW_SHOW);
    // na samą górę — nad dockiem i górnym paskiem (one też są "zawsze na wierzchu")
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

void ScreenSnip::LayoutToolbar() {
    const UINT dpi = GetDpiForWindow(hwnd_);
    auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    // Pasek na środku u góry monitora głównego (jego lewy górny róg to (0,0) ekranu)
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const int bw = px(120), cw = px(44), h = px(44), pad = px(6);
    const int width = pad + bw + px(4) + bw + px(10) + cw + pad;
    const int left = (mi.rcMonitor.left + mi.rcMonitor.right) / 2 - width / 2 - screen_.left;
    const int top = mi.rcMonitor.top - screen_.top + px(14);
    toolbar_ = {left, top, left + width, top + h};
    int x = left + pad;
    buttons_[BtnRegion] = {x, top + pad, x + bw, top + h - pad};
    x += bw + px(4);
    buttons_[BtnFull] = {x, top + pad, x + bw, top + h - pad};
    x += bw + px(10);
    buttons_[BtnClose] = {x, top + pad, x + cw, top + h - pad};

    if (textFont_) DeleteObject(textFont_);
    if (glyphFont_) DeleteObject(glyphFont_);
    textFont_ = CreateFontW(-px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    glyphFont_ = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                             L"Segoe MDL2 Assets");
}

int ScreenSnip::ButtonAt(POINT p) const {
    for (int i = 0; i < BtnCount; ++i)
        if (PtInRect(&buttons_[i], p)) return i;
    return BtnNone;
}

void ScreenSnip::PaintToolbar(HDC dc) {
    const int r = MulDiv(16, static_cast<int>(GetDpiForWindow(hwnd_)), 96);
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 38));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(90, 90, 104));
    HGDIOBJ oldBrush = SelectObject(dc, bg), oldPen = SelectObject(dc, border);
    RoundRect(dc, toolbar_.left, toolbar_.top, toolbar_.right, toolbar_.bottom, r, r);

    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < BtnCount; ++i) {
        const RECT& b = buttons_[i];
        const bool active = i == BtnRegion;  // tryb domyślny: wycinek
        if (active || hover_ == i) {
            HBRUSH fill = CreateSolidBrush(active ? RGB(45, 90, 150) : RGB(60, 60, 70));
            HPEN none = CreatePen(PS_NULL, 0, 0);
            SelectObject(dc, fill);
            SelectObject(dc, none);
            RoundRect(dc, b.left, b.top, b.right, b.bottom, r - 4, r - 4);
            SelectObject(dc, bg);
            SelectObject(dc, border);
            DeleteObject(fill);
            DeleteObject(none);
        }
        SetTextColor(dc, RGB(240, 240, 245));
        RECT glyph = b, text = b;
        if (kButtons[i].label) {
            glyph.right = glyph.left + (b.bottom - b.top) + 4;
            text.left = glyph.right - 2;
        }
        SelectObject(dc, glyphFont_);
        DrawTextW(dc, &kButtons[i].glyph, 1, &glyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (kButtons[i].label) {
            SelectObject(dc, textFont_);
            DrawTextW(dc, kButtons[i].label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(bg);
    DeleteObject(border);
}

RECT ScreenSnip::Selection() const {
    return {std::min(start_.x, end_.x), std::min(start_.y, end_.y), std::max(start_.x, end_.x), std::max(start_.y, end_.y)};
}

void ScreenSnip::Invalidate(const RECT& a, const RECT& b) {
    RECT u{};
    UnionRect(&u, &a, &b);
    InflateRect(&u, 3, 3);
    InvalidateRect(hwnd_, &u, FALSE);
}

void ScreenSnip::CopyToClipboard(const RECT& area) {
    const int w = area.right - area.left, h = area.bottom - area.top;
    if (w < 2 || h < 2) return;
    HDC screen = GetDC(nullptr);
    HBITMAP clip = CreateCompatibleBitmap(screen, w, h);
    HDC dc = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(dc, clip);
    BitBlt(dc, 0, 0, w, h, shotDC_, area.left, area.top, SRCCOPY);
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    if (OpenClipboard(hwnd_)) {
        EmptyClipboard();
        if (SetClipboardData(CF_BITMAP, clip)) clip = nullptr;  // od teraz własność schowka
        CloseClipboard();
    }
    if (clip) DeleteObject(clip);
}

void ScreenSnip::Finish(bool copy) {
    if (copy && hwnd_) CopyToClipboard(Selection());
    if (hwnd_) {
        const HWND h = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(h);
    }
    if (shotDC_) {
        if (shotOld_) SelectObject(shotDC_, shotOld_);
        DeleteDC(shotDC_);
    }
    if (dimDC_) {
        if (dimOld_) SelectObject(dimDC_, dimOld_);
        DeleteDC(dimDC_);
    }
    if (shot_) DeleteObject(shot_);
    if (dim_) DeleteObject(dim_);
    if (textFont_) DeleteObject(textFont_);
    if (glyphFont_) DeleteObject(glyphFont_);
    shotDC_ = dimDC_ = nullptr;
    shot_ = dim_ = nullptr;
    shotOld_ = dimOld_ = nullptr;
    textFont_ = glyphFont_ = nullptr;
    dragging_ = false;
    hover_ = BtnNone;
    start_ = end_ = {};
}

LRESULT CALLBACK ScreenSnip::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<ScreenSnip*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->hwnd_ == hwnd) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ScreenSnip::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR: {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd_, &p);
        SetCursor(LoadCursorW(nullptr, !dragging_ && PtInRect(&toolbar_, p) ? IDC_ARROW : IDC_CROSS));
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        const RECT& p = ps.rcPaint;
        BitBlt(dc, p.left, p.top, p.right - p.left, p.bottom - p.top, dimDC_, p.left, p.top, SRCCOPY);
        const RECT sel = Selection();
        RECT bright{};
        if (IntersectRect(&bright, &sel, &p))
            BitBlt(dc, bright.left, bright.top, bright.right - bright.left, bright.bottom - bright.top, shotDC_, bright.left,
                   bright.top, SRCCOPY);
        if (sel.right > sel.left && sel.bottom > sel.top) {
            HBRUSH border = CreateSolidBrush(RGB(96, 170, 255));
            RECT r = sel;
            for (int i = 0; i < 2; ++i) {  // ramka 2 px
                FrameRect(dc, &r, border);
                InflateRect(&r, 1, 1);
            }
            DeleteObject(border);
        }
        RECT overlap{};
        if (!dragging_ && IntersectRect(&overlap, &toolbar_, &p)) PaintToolbar(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        switch (ButtonAt(p)) {
        case BtnRegion:
            return 0;  // już w tym trybie
        case BtnFull:
            start_ = {0, 0};
            end_ = {screen_.right - screen_.left, screen_.bottom - screen_.top};
            Finish(true);
            return 0;
        case BtnClose:
            Finish(false);
            return 0;
        default:
            break;
        }
        if (PtInRect(&toolbar_, p)) return 0;
        start_ = end_ = p;
        dragging_ = true;
        InvalidateRect(hwnd_, &toolbar_, FALSE);  // pasek chowa się na czas zaznaczania
        SetCapture(hwnd_);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (dragging_) {
            const RECT before = Selection();
            end_ = p;
            Invalidate(before, Selection());
        } else if (const int hit = ButtonAt(p); hit != hover_) {
            hover_ = hit;
            InvalidateRect(hwnd_, &toolbar_, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (dragging_) {
            dragging_ = false;
            ReleaseCapture();
            end_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            Finish(true);
        }
        return 0;
    case WM_RBUTTONUP:
        Finish(false);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) Finish(false);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && !dragging_) PostMessageW(hwnd_, WM_KEYDOWN, VK_ESCAPE, 0);  // np. Alt+Tab
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
