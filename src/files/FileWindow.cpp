#include "FileWindow.h"
#include "../desktop/DesktopItems.h"
#include <dwmapi.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr wchar_t kClassName[] = L"ExploderFiles";
constexpr UINT WM_REDRAW = WM_APP + 2;
constexpr UINT WM_FOLDER_CHANGED = WM_APP + 30;
constexpr UINT_PTR kReloadTimer = 1;

// Układ (DIP)
constexpr float kSidebarW = 196.f, kToolbarH = 52.f, kStatusH = 26.f, kSideRow = 28.f, kBorder = 6.f;
constexpr D2D1_POINT_2F kLights{20.f, 26.f};
const D2D1_COLOR_F kBg{0.11f, 0.11f, 0.13f, 1.f};
const D2D1_COLOR_F kSideBg{0.15f, 0.15f, 0.18f, 1.f};
const D2D1_COLOR_F kLine{1.f, 1.f, 1.f, 0.08f};
const D2D1_COLOR_F kText{1.f, 1.f, 1.f, 0.95f};
const D2D1_COLOR_F kDim{1.f, 1.f, 1.f, 0.5f};

std::vector<std::unique_ptr<FileWindow>> g_windows;

std::wstring Name(IShellItem* item, SIGDN type) {
    PWSTR s = nullptr;
    std::wstring out;
    if (item && SUCCEEDED(item->GetDisplayName(type, &s))) {
        out = s;
        CoTaskMemFree(s);
    }
    return out;
}

bool Inside(const D2D1_RECT_F& r, D2D1_POINT_2F p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

D2D1_RECT_F ToolButton(int index) {  // wstecz, dalej, w górę
    const float x = kSidebarW + 12.f + index * 34.f;
    return {x, 12.f, x + 30.f, 40.f};
}
}  // namespace

// ---------------- Zarządzanie oknami ----------------

void OpenFileWindow(HINSTANCE inst, Graphics& g, ComPtr<IShellItem> folder) {
    auto window = std::make_unique<FileWindow>();
    if (window->Create(inst, g, std::move(folder))) g_windows.push_back(std::move(window));
}

void OpenFilePicker(HINSTANCE inst, Graphics& g, ComPtr<IShellItem> folder, const std::wstring& prompt,
                    std::function<void(ComPtr<IShellItem>)> onPick) {
    auto window = std::make_unique<FileWindow>();
    if (!window->Create(inst, g, std::move(folder))) return;
    window->SetPicker(prompt, std::move(onPick));
    g_windows.push_back(std::move(window));
}

void FileWindow::SetPicker(const std::wstring& prompt, std::function<void(ComPtr<IShellItem>)> onPick) {
    pickPrompt_ = prompt;
    view_.pickFile = [this, onPick = std::move(onPick)](ComPtr<IShellItem> item) {
        onPick(std::move(item));
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    };
    RequestRedraw();
}

void CloseAllFileWindows() {
    while (!g_windows.empty()) DestroyWindow(g_windows.back()->Hwnd());  // WM_NCDESTROY usuwa z listy
}

// ---------------- Okno ----------------

bool FileWindow::Create(HINSTANCE inst, Graphics& g, ComPtr<IShellItem> folder) {
    inst_ = inst;
    g_ = &g;
    WNDCLASSEXW wc{sizeof(wc)};
    if (!GetClassInfoExW(inst, kClassName, &wc)) {
        wc = {sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        HICON iconBig = nullptr, iconSmall = nullptr;
        ExtractIconExW(L"imageres.dll", 3, &iconBig, &iconSmall, 1);  // ikona folderu (dock, Alt+Tab)
        wc.hIcon = iconBig;
        wc.hIconSm = iconSmall;
        if (!RegisterClassExW(&wc)) return false;
    }
    // Nowe okno lekko przesunięte względem poprzedniego (kaskada)
    const int offset = static_cast<int>(g_windows.size() % 8) * 28;
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, kClassName, L"Exploder", WS_OVERLAPPEDWINDOW,
                            200 + offset, 120 + offset, 1040, 680, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);
    const UINT w = static_cast<UINT>(MulDiv(1040, static_cast<int>(dpi_), 96)), h = static_cast<UINT>(MulDiv(680, static_cast<int>(dpi_), 96));
    SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(w), static_cast<int>(h), SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
    const MARGINS shadow{0, 0, 0, 1};  // cień DWM mimo braku ramki
    DwmExtendFrameIntoClientArea(hwnd_, &shadow);

    IDWriteFactory* dw = g_->DWrite();
    auto make = [dw](const wchar_t* font, DWRITE_FONT_WEIGHT weight, float size, ComPtr<IDWriteTextFormat>& out) {
        dw->CreateTextFormat(font, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"pl-PL", &out);
        if (!out) return;
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ellipsis;
        dw->CreateEllipsisTrimmingSign(out.Get(), &ellipsis);
        out->SetTrimming(&trimming, ellipsis.Get());
    };
    make(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 15.f, titleFormat_);
    make(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f, sideFormat_);
    make(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.f, sectionFormat_);
    make(L"Segoe MDL2 Assets", DWRITE_FONT_WEIGHT_NORMAL, 14.f, glyphFormat_);
    make(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.f, statusFormat_);
    if (glyphFormat_) glyphFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (!surface_.Create(*g_, hwnd_, static_cast<UINT>(std::max<LONG>(rc.right, 1)), static_cast<UINT>(std::max<LONG>(rc.bottom, 1))))
        return false;
    surface_.Context()->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    surface_.Context()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    surface_.Context()->CreateSolidColorBrush(kText, &brush_);
    widthDip_ = rc.right * 96.f / dpi_;
    heightDip_ = rc.bottom * 96.f / dpi_;

    view_.Init(*g_, hwnd_, WM_FOLDER_CHANGED, [this] { RequestRedraw(); });
    view_.openInNewWindow = [this](ComPtr<IShellItem> f) { OpenFileWindow(inst_, *g_, std::move(f)); };
    BuildSidebar();
    if (!folder || !view_.Navigate(folder)) view_.NavigateKnown(FOLDERID_Desktop);
    Layout();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return true;
}

void FileWindow::BuildSidebar() {
    sidebar_.clear();
    auto section = [this](const wchar_t* label) { sidebar_.push_back({label, L"", nullptr}); };
    auto add = [this](const std::wstring& parsing, const std::wstring& label) {
        ComPtr<IShellItem> item;
        if (FAILED(SHCreateItemFromParsingName(parsing.c_str(), nullptr, IID_PPV_ARGS(&item)))) return;
        sidebar_.push_back({label.empty() ? Name(item.Get(), SIGDN_NORMALDISPLAY) : label, parsing, item});
    };
    auto known = [&](REFKNOWNFOLDERID id) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) add(p, L"");
        CoTaskMemFree(p);
    };
    section(L"Ulubione");
    known(FOLDERID_Desktop);
    known(FOLDERID_Downloads);
    known(FOLDERID_Documents);
    known(FOLDERID_Pictures);
    known(FOLDERID_Music);
    known(FOLDERID_Videos);
    section(L"Lokalizacje");
    add(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", L"");  // Ten komputer
    const DWORD drives = GetLogicalDrives();
    for (int d = 0; d < 26; ++d) {
        if (!(drives & (1u << d))) continue;
        const wchar_t root[] = {static_cast<wchar_t>(L'A' + d), L':', L'\\', 0};
        const UINT type = GetDriveTypeW(root);
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_REMOTE) add(root, L"");
    }
    add(L"::{645FF040-5081-101B-9F08-00AA002F954E}", L"");  // Kosz
    // Ikony paska bocznego: wczytane od razu, poza rysowaniem
    const int iconPx = static_cast<int>(std::ceil(16.f * dpi_ / 96.f));
    for (SidebarEntry& e : sidebar_)
        if (e.item) e.pendingIcon = LoadItemImage(e.item.Get(), iconPx, g_->WIC());
}

void FileWindow::Layout() {
    view_.SetBounds({kSidebarW, kToolbarH, widthDip_, heightDip_ - kStatusH});
}

void FileWindow::RequestRedraw() {
    if (redrawPending_ || !hwnd_) return;
    redrawPending_ = true;
    PostMessageW(hwnd_, WM_REDRAW, 0, 0);
}

D2D1_POINT_2F FileWindow::ToDip(LPARAM lp) const { return {GET_X_LPARAM(lp) * 96.f / dpi_, GET_Y_LPARAM(lp) * 96.f / dpi_}; }

int FileWindow::HitTest(D2D1_POINT_2F p) const {
    if (Inside(ToolButton(0), p)) return HitBack;
    if (Inside(ToolButton(1), p)) return HitForward;
    if (Inside(ToolButton(2), p)) return HitUp;
    if (Inside({widthDip_ - 46.f, 12.f, widthDip_ - 14.f, 40.f}, p)) return HitNewFolder;
    if (p.x < kSidebarW && p.y >= kToolbarH) {
        const int row = static_cast<int>((p.y - kToolbarH) / kSideRow);
        if (row >= 0 && row < static_cast<int>(sidebar_.size()) && !sidebar_[static_cast<size_t>(row)].parsing.empty()) return row;
    }
    return HitNone;
}

void FileWindow::Render() {
    if (rendering_) return RequestRedraw();
    rendering_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{rendering_};
    view_.PrepareIcons(static_cast<float>(dpi_));
    // Tytuł okna = nazwa folderu (dymek w docku, Alt+Tab)
    if (view_.FolderName() != shownTitle_) {
        shownTitle_ = view_.FolderName();
        SetWindowTextW(hwnd_, shownTitle_.c_str());
    }
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc || !brush_) return;
    const float W = widthDip_, H = heightDip_;
    auto fill = [&](const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0.f) {
        brush_->SetColor(c);
        if (radius > 0.f) dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
        else dc->FillRectangle(r, brush_.Get());
    };
    auto text = [&](const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
        brush_->SetColor(c);
        dc->DrawText(s.c_str(), static_cast<UINT32>(s.size()), f, r, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    dc->Clear(kBg);
    // Pasek boczny
    fill({0.f, 0.f, kSidebarW, H}, kSideBg);
    fill({kSidebarW - 1.f, 0.f, kSidebarW, H}, kLine);
    TrafficLights::Draw(dc, brush_.Get(), kLights, hoverLights_, active_);
    const std::wstring current = view_.FolderParsing();
    for (size_t i = 0; i < sidebar_.size(); ++i) {
        SidebarEntry& e = sidebar_[i];
        const float y = kToolbarH + i * kSideRow;
        if (e.parsing.empty()) {
            text(e.label, sectionFormat_.Get(), {14.f, y + 4.f, kSidebarW - 8.f, y + kSideRow}, kDim);
            continue;
        }
        const D2D1_RECT_F row{8.f, y + 1.f, kSidebarW - 8.f, y + kSideRow - 1.f};
        if (_wcsicmp(e.parsing.c_str(), current.c_str()) == 0) fill(row, D2D1::ColorF(1.f, 1.f, 1.f, 0.12f), 6.f);
        else if (hover_ == static_cast<int>(i)) fill(row, D2D1::ColorF(1.f, 1.f, 1.f, 0.06f), 6.f);
        if (e.pendingIcon) {
            dc->CreateBitmapFromWicBitmap(e.pendingIcon.Get(), nullptr, &e.icon);
            e.pendingIcon.Reset();
        }
        if (e.icon) {
            const D2D1_RECT_F dst{16.f, y + 6.f, 32.f, y + 22.f};
            dc->DrawBitmap(e.icon.Get(), &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
        text(e.label, sideFormat_.Get(), {40.f, y, kSidebarW - 10.f, y + kSideRow}, kText);
    }

    // Pasek narzędzi
    fill({kSidebarW, kToolbarH - 1.f, W, kToolbarH}, kLine);
    const wchar_t glyphs[] = {L'\xE72B', L'\xE72A', L'\xE74A'};  // wstecz, dalej, w górę
    const bool enabled[] = {view_.CanBack(), view_.CanForward(), view_.CanUp()};
    for (int i = 0; i < 3; ++i) {
        const D2D1_RECT_F b = ToolButton(i);
        if (enabled[i] && hover_ == HitBack + i) fill(b, D2D1::ColorF(1.f, 1.f, 1.f, 0.08f), 6.f);
        brush_->SetColor(enabled[i] ? kText : D2D1::ColorF(1.f, 1.f, 1.f, 0.25f));
        dc->DrawText(&glyphs[i], 1, glyphFormat_.Get(), b, brush_.Get());
    }
    text(view_.FolderName(), titleFormat_.Get(), {ToolButton(2).right + 14.f, 0.f, W - 60.f, kToolbarH}, kText);
    const D2D1_RECT_F nf{W - 46.f, 12.f, W - 14.f, 40.f};
    if (hover_ == HitNewFolder) fill(nf, D2D1::ColorF(1.f, 1.f, 1.f, 0.08f), 6.f);
    const wchar_t newFolder = L'\xE8F4';
    brush_->SetColor(kText);
    dc->DrawText(&newFolder, 1, glyphFormat_.Get(), nf, brush_.Get());

    // Pliki
    view_.Draw(dc, brush_.Get(), static_cast<float>(dpi_));

    // Pasek stanu
    fill({kSidebarW, H - kStatusH, W, H - kStatusH + 1.f}, kLine);
    const size_t n = view_.ItemCount();
    const std::wstring status = std::to_wstring(n) + (n == 1 ? L" element" : (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20)) ? L" elementy" : L" elementów");
    text(pickPrompt_.empty() ? status : pickPrompt_, statusFormat_.Get(), {kSidebarW + 14.f, H - kStatusH, W - 14.f, H},
         pickPrompt_.empty() ? kDim : kText);

    if (!surface_.End()) {
        surface_ = Surface{};
        brush_.Reset();
    }
}

LRESULT CALLBACK FileWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<FileWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<FileWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        std::erase_if(g_windows, [self](const std::unique_ptr<FileWindow>& w) { return w.get() == self; });  // usuwa obiekt
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->HandleMessage(msg, wp, lp);
}

LRESULT FileWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r = 0; view_.HandleMenuMessage(msg, wp, lp, r)) return r;
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
        const float x = pt.x * 96.f / dpi_, y = pt.y * 96.f / dpi_;
        if (!IsZoomed(hwnd_)) {  // krawędzie do zmiany rozmiaru
            const bool l = x < kBorder, r = x > widthDip_ - kBorder, t = y < kBorder, b = y > heightDip_ - kBorder;
            if (t && l) return HTTOPLEFT;
            if (t && r) return HTTOPRIGHT;
            if (b && l) return HTBOTTOMLEFT;
            if (b && r) return HTBOTTOMRIGHT;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
            if (t) return HTTOP;
            if (b) return HTBOTTOM;
        }
        const D2D1_POINT_2F p{x, y};
        if (y < kToolbarH && TrafficLights::HitTest(kLights, p) < 0 && HitTest(p) == HitNone) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_ACTIVATE:
        active_ = LOWORD(wp) != WA_INACTIVE;
        RequestRedraw();
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
        Layout();
        Render();  // od razu — płynniej przy przeciąganiu krawędzi
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {MulDiv(520, static_cast<int>(dpi_), 96), MulDiv(320, static_cast<int>(dpi_), 96)};
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        if (surface_.Context()) surface_.Context()->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_FOLDER_CHANGED:
        SetTimer(hwnd_, kReloadTimer, 200, nullptr);  // seria zmian → jedno odświeżenie
        return 0;
    case WM_TIMER:
        if (wp == kReloadTimer) {
            KillTimer(hwnd_, kReloadTimer);
            view_.Reload();
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
        }
        const D2D1_POINT_2F p = ToDip(lp);
        const bool lights = TrafficLights::HitTest(kLights, p) >= 0 ||
                            (p.y < kToolbarH && p.x >= kLights.x - 10.f && p.x <= kLights.x + 50.f);
        const int hit = HitTest(p);
        if (lights != hoverLights_ || hit != hover_) {
            hoverLights_ = lights;
            hover_ = hit;
            RequestRedraw();
        }
        view_.OnMouseMove(p);
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        hoverLights_ = false;
        hover_ = HitNone;
        view_.OnMouseLeave();
        RequestRedraw();
        return 0;
    case WM_LBUTTONDOWN: {
        const D2D1_POINT_2F p = ToDip(lp);
        switch (TrafficLights::HitTest(kLights, p)) {
        case 0: PostMessageW(hwnd_, WM_CLOSE, 0, 0); return 0;
        case 1: ShowWindow(hwnd_, SW_MINIMIZE); return 0;
        case 2: ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE); return 0;
        default: break;
        }
        const int hit = HitTest(p);
        if (hit == HitBack) view_.Back();
        else if (hit == HitForward) view_.Forward();
        else if (hit == HitUp) view_.Up();
        else if (hit == HitNewFolder) view_.NewFolder();
        else if (hit >= 0) view_.Navigate(sidebar_[static_cast<size_t>(hit)].item);
        else if (Inside(view_.Bounds(), p)) {
            SetCapture(hwnd_);
            viewCapture_ = true;
            view_.OnMouseDown(p, (wp & MK_CONTROL) != 0, (wp & MK_SHIFT) != 0);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (viewCapture_) {
            viewCapture_ = false;
            ReleaseCapture();
            view_.OnMouseUp(ToDip(lp));
        }
        return 0;
    case WM_LBUTTONDBLCLK: {
        const D2D1_POINT_2F p = ToDip(lp);
        if (Inside(view_.Bounds(), p)) view_.OnDoubleClick(p);
        return 0;
    }
    case WM_RBUTTONUP: {
        const D2D1_POINT_2F p = ToDip(lp);
        if (!Inside(view_.Bounds(), p)) return 0;
        POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd_, &screen);
        view_.OnRightClick(p, screen);
        return 0;
    }
    case WM_MOUSEWHEEL:
        view_.OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    case WM_XBUTTONUP:  // boczne przyciski myszy: wstecz / dalej
        if (GET_XBUTTON_WPARAM(wp) == XBUTTON1) view_.Back();
        else view_.Forward();
        return TRUE;
    case WM_KEYDOWN:
        if (view_.OnKey(static_cast<UINT>(wp), GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0, false)) return 0;
        break;
    case WM_SYSKEYDOWN:  // Alt+strzałki
        if ((wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP) && view_.OnKey(static_cast<UINT>(wp), false, false, true)) return 0;
        break;
    case WM_CHAR:
        view_.OnChar(static_cast<wchar_t>(wp));
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
