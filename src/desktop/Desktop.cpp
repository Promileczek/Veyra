#include "Desktop.h"
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {
constexpr wchar_t kClassName[] = L"ExploderDesktop";
constexpr UINT WM_DESKTOP_CHANGED = WM_APP + 20;  // zmiana w folderze pulpitu (wątek obserwatora)
constexpr UINT WM_DESKTOP_REDRAW = WM_APP + 21;
constexpr UINT WM_RENAME_DONE = WM_APP + 22;      // wParam: 1 = zatwierdź, 0 = anuluj
constexpr UINT_PTR kRefreshTimer = 1, kRetryTimer = 2, kFilesReloadTimer = 3, kStatsTimer = 4;
constexpr UINT WM_FILES_CHANGED = WM_APP + 30;  // zmiana w folderze panelu "Pliki"

// Ikony plików na pulpicie wyłączone (decyzja użytkownika: sama tapeta + panel aplikacji).
// Kod ikon zostaje — wystarczy przełączyć, gdyby wróciły.
constexpr bool kShowIcons = false;
constexpr float kLauncherW = 680.f, kLauncherH = 480.f, kLauncherRadius = 22.f;

// Siatka ikon (DIP)
constexpr float kCellW = 84.f, kCellH = 98.f, kMargin = 6.f, kIcon = 48.f, kLabelTop = 58.f, kLabelH = 36.f;

const D2D1_COLOR_F kSelectFill{0.55f, 0.75f, 1.f, 0.28f};
const D2D1_COLOR_F kSelectBorder{0.60f, 0.80f, 1.f, 0.55f};
const D2D1_COLOR_F kHoverFill{1.f, 1.f, 1.f, 0.12f};

enum DesktopCmd { CmdNewFolder = 1, CmdRefresh, CmdWallpaper };

WNDPROC g_editProc = nullptr;

LRESULT CALLBACK RenameEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const HWND owner = GetWindow(h, GW_OWNER);
    if (m == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE)) {
        PostMessageW(owner, WM_RENAME_DONE, w == VK_RETURN, 0);
        return 0;
    }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;  // bez "piknięcia"
    if (m == WM_KILLFOCUS) PostMessageW(owner, WM_RENAME_DONE, 1, 0);
    return CallWindowProcW(g_editProc, h, m, w, l);
}

bool Intersects(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
}  // namespace

bool Desktop::Create(HINSTANCE inst, Graphics& g, std::function<void(ComPtr<IShellItem>)> openFolder) {
    inst_ = inst;
    g_ = &g;
    openFolder_ = std::move(openFolder);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    monitor_ = mi.rcMonitor;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClassName, L"Exploder Desktop", WS_POPUP,
                            monitor_.left, monitor_.top, monitor_.right - monitor_.left, monitor_.bottom - monitor_.top,
                            nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);
    widthDip_ = (monitor_.right - monitor_.left) * 96.f / dpi_;
    heightDip_ = (monitor_.bottom - monitor_.top) * 96.f / dpi_;

    using SetShellWindowFn = BOOL(WINAPI*)(HWND);
    if (auto setShell = reinterpret_cast<SetShellWindowFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetShellWindow")))
        setShell(hwnd_);

    if (FAILED(g_->DWrite()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, 12.f, L"pl-PL", &labelFormat_)))
        return false;
    labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    labelFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
    g_->DWrite()->CreateEllipsisTrimmingSign(labelFormat_.Get(), &ellipsis_);
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    labelFormat_->SetTrimming(&trimming, ellipsis_.Get());

    launcher_.Init(*g_, [this] { RequestRedraw(); }, [this] {
        // panel zmienił wysokość (przypięcie/odpięcie, szukanie) → nowe miejsce i szkło pod nim
        if (!iconSurface_) return;
        UpdateWorkArea();
        DrawWallpaper();
        RequestRedraw();
    });
    launcher_.openFolder = openFolder_;
    launcher_.openFiles = [this] {  // kafelek "Pliki" — panel po lewej wraca
        files_.Show();
        files_.SetFocused(true);
    };
    launcher_.Load();
    stats_.Init(*g_);
    statsHeight_ = stats_.Height();
    files_.Init(*g_, hwnd_, WM_FILES_CHANGED,
                {[this] { RequestRedraw(); },
                 [this] {  // panel przesunięty / zamknięty / zwinięty → szkło pod nim od nowa
                     if (!iconSurface_) return;
                     DrawWallpaper();
                     RequestRedraw();
                 },
                 openFolder_});
    if (!CreateResources()) return false;
    UpdateWorkArea();
    DrawWallpaper();
    if (kShowIcons) {
        layout_ = LoadIconLayout();
        Refresh();
    }
    SetWindowPos(hwnd_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (kShowIcons) StartWatcher();
    SetTimer(hwnd_, kStatsTimer, 2000, nullptr);
    RequestRedraw();
    return true;
}

void Desktop::Destroy() {
    if (hwnd_) stats_.SaveUsage();
    StopWatcher();
    CommitRename(false);
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

bool Desktop::CreateResources() {
    dc_.Reset();
    brush_.Reset();
    wallSurface_.Reset();
    iconSurface_.Reset();
    wallVisual_.Reset();
    iconVisual_.Reset();
    root_.Reset();
    compTarget_.Reset();
    lastBounds_ = {};
    for (DesktopItem& item : items_) {
        item.icon.Reset();
        item.iconLoaded = false;
    }
    IDCompositionDevice* dcomp = g_->DComp();
    const UINT w = static_cast<UINT>(monitor_.right - monitor_.left), h = static_cast<UINT>(monitor_.bottom - monitor_.top);
    if (FAILED(g_->D2D()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_)) ||
        FAILED(dcomp->CreateTargetForHwnd(hwnd_, TRUE, &compTarget_)) || FAILED(dcomp->CreateVisual(&root_)) ||
        FAILED(dcomp->CreateVisual(&wallVisual_)) || FAILED(dcomp->CreateVisual(&iconVisual_)) ||
        FAILED(dcomp->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_IGNORE, &wallSurface_)) ||
        FAILED(dcomp->CreateVirtualSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &iconSurface_)))
        return false;
    wallVisual_->SetContent(wallSurface_.Get());
    iconVisual_->SetContent(iconSurface_.Get());
    root_->AddVisual(wallVisual_.Get(), FALSE, nullptr);
    root_->AddVisual(iconVisual_.Get(), TRUE, wallVisual_.Get());
    compTarget_->SetRoot(root_.Get());

    dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return SUCCEEDED(dc_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush_));
}

namespace {
// Rysowanie D2D w prostokąt powierzchni DComp: BeginDraw daje fragment wspólnej tekstury z przesunięciem.
template <typename SurfaceT, typename DrawFn>
bool DrawToSurface(SurfaceT* surface, ID2D1DeviceContext* dc, const RECT& update, float dpi, DrawFn&& draw) {
    ComPtr<IDXGISurface> dxgi;
    POINT offset{};
    if (FAILED(surface->BeginDraw(&update, IID_PPV_ARGS(&dxgi), &offset))) return false;
    const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                               D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                               dpi, dpi);
    ComPtr<ID2D1Bitmap1> target;
    HRESULT hr = dc->CreateBitmapFromDxgiSurface(dxgi.Get(), &props, &target);
    if (SUCCEEDED(hr)) {
        const float k = 96.f / dpi;  // piksele → DIP
        dc->SetTarget(target.Get());
        dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Translation((offset.x - update.left) * k, (offset.y - update.top) * k));
        dc->PushAxisAlignedClip({update.left * k, update.top * k, update.right * k, update.bottom * k},
                                D2D1_ANTIALIAS_MODE_ALIASED);
        draw(dc);
        dc->PopAxisAlignedClip();
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        hr = dc->EndDraw();
        dc->SetTarget(nullptr);
    }
    surface->EndDraw();
    return SUCCEEDED(hr);
}
}  // namespace

void Desktop::DrawWallpaper() {
    if (!wallSurface_) return;
    const UINT w = static_cast<UINT>(monitor_.right - monitor_.left), h = static_cast<UINT>(monitor_.bottom - monitor_.top);
    ComPtr<ID2D1Bitmap> bitmap;  // tylko na czas rysowania — potem zostaje wyłącznie w warstwie DComp
    if (ComPtr<IWICBitmapSource> src = LoadWallpaper(g_->WIC(), w, h)) {
        const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                  static_cast<float>(dpi_), static_cast<float>(dpi_));
        dc_->CreateBitmapFromWicBitmap(src.Get(), &props, &bitmap);
    }
    const RECT full{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
    const D2D1_RECT_F fullDip{0.f, 0.f, widthDip_, heightDip_};
    DrawToSurface(wallSurface_.Get(), dc_.Get(), full, static_cast<float>(dpi_), [&](ID2D1DeviceContext* dc) {
        dc->Clear(D2D1::ColorF(0x1E2A3A));
        if (bitmap) dc->DrawBitmap(bitmap.Get(), &fullDip, 1.f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);

        // Szkło pod panelami (aplikacje, pliki): rozmyta tapeta + przyciemnienie. Liczone raz, razem z tapetą.
        ComPtr<ID2D1Factory> factory;
        dc->GetFactory(&factory);
        ComPtr<ID2D1Effect> blur;
        if (bitmap && SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur))) {
            blur->SetInput(0, bitmap.Get());
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 28.f);
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        }
        ComPtr<ID2D1SolidColorBrush> tint;
        dc->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.07f, 0.11f, 0.45f), &tint);
        const struct {
            D2D1_RECT_F rect;
            float radius;
        } panels[] = {{launcher_.Bounds(), kLauncherRadius}, {files_.Bounds(), 18.f}, {stats_.Bounds(), 22.f}};
        for (const auto& panel : panels) {
            ComPtr<ID2D1RoundedRectangleGeometry> shape;
            if (panel.rect.right <= panel.rect.left ||
                FAILED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(panel.rect, panel.radius, panel.radius), &shape)))
                continue;
            dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), shape.Get()), nullptr);
            if (blur) dc->DrawImage(blur.Get());
            if (tint) dc->FillRectangle(panel.rect, tint.Get());
            dc->PopLayer();
        }
    });
    g_->DComp()->Commit();
}

RECT Desktop::IconBounds() const {
    const bool dragging = mode_ == Mode::Dragging;
    const float dx = dragging ? curPt_.x - downPt_.x : 0.f, dy = dragging ? curPt_.y - downPt_.y : 0.f;
    D2D1_RECT_F b{FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX};
    auto add = [&b](const D2D1_RECT_F& r) {
        b = {std::min(b.left, r.left), std::min(b.top, r.top), std::max(b.right, r.right), std::max(b.bottom, r.bottom)};
    };
    for (const DesktopItem& item : items_) {
        D2D1_RECT_F r = ContentRect(item);
        add({r.left - 2.f, r.top - 2.f, r.right + 2.f, r.bottom + 2.f});
        if (dragging && item.selected) add({r.left + dx - 2.f, r.top + dy - 2.f, r.right + dx + 2.f, r.bottom + dy + 2.f});
    }
    add(launcher_.Bounds());
    add(stats_.Bounds());
    if (const D2D1_RECT_F f = files_.Bounds(); f.right > f.left) add({f.left - 2.f, f.top - 2.f, f.right + 2.f, f.bottom + 2.f});
    if (mode_ == Mode::Selecting) add({std::min(downPt_.x, curPt_.x) - 1.f, std::min(downPt_.y, curPt_.y) - 1.f,
                                       std::max(downPt_.x, curPt_.x) + 1.f, std::max(downPt_.y, curPt_.y) + 1.f});
    if (b.left > b.right) return {};
    const float s = dpi_ / 96.f;
    const LONG w = monitor_.right - monitor_.left, h = monitor_.bottom - monitor_.top;
    return {std::clamp(static_cast<LONG>(std::floor(b.left * s)), 0L, w), std::clamp(static_cast<LONG>(std::floor(b.top * s)), 0L, h),
            std::clamp(static_cast<LONG>(std::ceil(b.right * s)), 0L, w), std::clamp(static_cast<LONG>(std::ceil(b.bottom * s)), 0L, h)};
}

void Desktop::UpdateWorkArea() {
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const float s = 96.f / dpi_;
    work_ = {(wa.left - monitor_.left) * s, (wa.top - monitor_.top) * s, (wa.right - monitor_.left) * s,
             (wa.bottom - monitor_.top) * s};
    cols_ = std::max(1, static_cast<int>((work_.right - work_.left - kMargin) / kCellW));
    rows_ = std::max(1, static_cast<int>((work_.bottom - work_.top - kMargin) / kCellH));

    // Panel: wysokość dopasowana do liczby przypiętych, na środku obszaru roboczego. Przy szukaniu
    // rośnie w dół od tego samego miejsca (wyszukiwarka nie skacze).
    const float lw = std::min(kLauncherW, work_.right - work_.left - 40.f);
    const float maxH = std::min(kLauncherH, work_.bottom - work_.top - 40.f);
    if (!launcher_.Searching()) {
        const float lh = launcher_.PreferredHeight(lw, maxH);
        launcherTop_ = (work_.top + work_.bottom - lh) / 2;
    }
    const float lx = (work_.left + work_.right - lw) / 2;
    const float lh = std::min(launcher_.PreferredHeight(lw, maxH), work_.bottom - 20.f - launcherTop_);
    launcher_.SetBounds({lx, launcherTop_, lx + lw, launcherTop_ + lh});

    // Statystyki: na prawo od panelu aplikacji
    const float sx = std::min(lx + lw + 20.f, work_.right - 16.f - StatsWidget::kWidth);
    stats_.SetBounds({sx, launcherTop_, sx + StatsWidget::kWidth, std::min(launcherTop_ + stats_.Height(), work_.bottom - 8.f)});

    // Panel "Pliki": domyślnie na lewo od panelu aplikacji (potem tam, gdzie go zostawiono)
    const float fw = 320.f, fx = std::max(work_.left + 16.f, lx - 20.f - fw);
    files_.SetDefaultRect({fx, launcherTop_, fx + fw, launcherTop_ + std::max(lh, 420.f)});
    files_.ClampTo({work_.left + 8.f, work_.top + 8.f, work_.right - 8.f, work_.bottom - 8.f});
}

void Desktop::Refresh() {
    std::vector<DesktopItem> fresh = EnumerateDesktopItems();
    for (DesktopItem& f : fresh) {
        for (const DesktopItem& o : items_) {
            if (o.key != f.key) continue;
            f.icon = o.icon;
            f.iconLoaded = o.iconLoaded;
            f.selected = o.selected;
            f.col = o.col;
            f.row = o.row;
            if (o.name == f.name) f.label = o.label;
            break;
        }
    }
    items_ = std::move(fresh);
    hover_ = -1;
    AssignPositions();
    RequestRedraw();
}

// Kolejność: obecne miejsce → zapisany układ → pierwsze wolne pole (kolumnami, jak w Windows).
void Desktop::AssignPositions() {
    std::vector<bool> taken(static_cast<size_t>(cols_ * rows_), false);
    auto valid = [&](int c, int r) { return c >= 0 && r >= 0 && c < cols_ && r < rows_; };
    auto cell = [&](int c, int r) { return static_cast<size_t>(c * rows_ + r); };

    for (DesktopItem& item : items_) {
        if (!(valid(item.col, item.row) && !taken[cell(item.col, item.row)])) {
            item.col = item.row = -1;
            if (auto it = layout_.find(item.key); it != layout_.end()) {
                const auto [c, r] = it->second;
                if (valid(c, r) && !taken[cell(c, r)]) {
                    item.col = c;
                    item.row = r;
                }
            }
        }
        if (item.col >= 0) taken[cell(item.col, item.row)] = true;
    }
    size_t next = 0;
    for (DesktopItem& item : items_) {
        if (item.col >= 0) continue;
        while (next < taken.size() && taken[next]) ++next;
        const size_t slot = next < taken.size() ? next : taken.size() - 1;  // pełna siatka: ostatnie pole
        item.col = static_cast<int>(slot) / rows_;
        item.row = static_cast<int>(slot) % rows_;
        taken[slot] = true;
    }

    IconLayout updated;
    for (const DesktopItem& item : items_) updated[item.key] = {item.col, item.row};
    if (updated != layout_) {
        layout_ = std::move(updated);
        SaveIconLayout(layout_);
    }
}

void Desktop::RequestRedraw() {
    if (redrawPending_ || !hwnd_) return;
    redrawPending_ = true;
    PostMessageW(hwnd_, WM_DESKTOP_REDRAW, 0, 0);
}

D2D1_RECT_F Desktop::CellRect(int col, int row) const {
    const float x = work_.left + kMargin + col * kCellW, y = work_.top + kMargin + row * kCellH;
    return {x, y, x + kCellW, y + kCellH};
}

D2D1_RECT_F Desktop::ContentRect(const DesktopItem& item) const {
    const D2D1_RECT_F c = CellRect(item.col, item.row);
    return {c.left + 4.f, c.top + 2.f, c.right - 4.f, c.bottom - 2.f};
}

int Desktop::ItemAt(float x, float y) const {
    for (size_t i = 0; i < items_.size(); ++i) {
        const D2D1_RECT_F r = ContentRect(items_[i]);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return static_cast<int>(i);
    }
    return -1;
}

D2D1_POINT_2F Desktop::ToDip(LPARAM lp) const {
    return {GET_X_LPARAM(lp) * 96.f / dpi_, GET_Y_LPARAM(lp) * 96.f / dpi_};
}

ID2D1Bitmap* Desktop::IconFor(DesktopItem& item) {
    if (!item.iconLoaded) {
        item.iconLoaded = true;
        const int px = static_cast<int>(std::ceil(kIcon * dpi_ / 96.f));
        if (ComPtr<IWICBitmapSource> src = LoadItemImage(item.item.Get(), px, g_->WIC()))
            dc_->CreateBitmapFromWicBitmap(src.Get(), nullptr, &item.icon);
    }
    return item.icon.Get();
}

IDWriteTextLayout* Desktop::LabelFor(DesktopItem& item) {
    if (!item.label)
        g_->DWrite()->CreateTextLayout(item.name.c_str(), static_cast<UINT32>(item.name.size()), labelFormat_.Get(),
                                       kCellW - 6.f, kLabelH, &item.label);
    return item.label.Get();
}

void Desktop::Render() {
    if (rendering_) return RequestRedraw();
    rendering_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{rendering_};
    launcher_.PrepareIcons(static_cast<float>(dpi_));
    files_.View().PrepareIcons(static_cast<float>(dpi_));
    if (!iconSurface_) {
        if (!CreateResources()) return;
        DrawWallpaper();
    }
    // Rysujemy tylko obszar z ikonami (i to, co trzeba wyczyścić po poprzedniej klatce).
    const RECT cur = IconBounds();
    RECT update{};
    UnionRect(&update, &lastBounds_, &cur);
    if (IsRectEmpty(&update)) return;

    const bool dragging = mode_ == Mode::Dragging;
    const float dx = dragging ? curPt_.x - downPt_.x : 0.f, dy = dragging ? curPt_.y - downPt_.y : 0.f;
    const bool ok = DrawToSurface(iconSurface_.Get(), dc_.Get(), update, static_cast<float>(dpi_), [&](ID2D1DeviceContext* dc) {
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    launcher_.Draw(dc, brush_.Get(), static_cast<float>(dpi_));
    files_.Draw(dc, brush_.Get(), static_cast<float>(dpi_));
    stats_.Draw(dc, brush_.Get());
    for (size_t i = 0; i < items_.size(); ++i) {
        DesktopItem& item = items_[i];
        const bool moving = dragging && item.selected;
        D2D1_RECT_F r = ContentRect(item);
        if (moving) r = {r.left + dx, r.top + dy, r.right + dx, r.bottom + dy};

        const auto rounded = D2D1::RoundedRect(r, 4.f, 4.f);
        if (item.selected) {
            brush_->SetColor(kSelectFill);
            dc->FillRoundedRectangle(rounded, brush_.Get());
            brush_->SetColor(kSelectBorder);
            dc->DrawRoundedRectangle(rounded, brush_.Get(), 1.f);
        } else if (hover_ == static_cast<int>(i) && !dragging) {
            brush_->SetColor(kHoverFill);
            dc->FillRoundedRectangle(rounded, brush_.Get());
        }

        const float cx = (r.left + r.right) / 2, top = r.top + 4.f;
        if (ID2D1Bitmap* icon = IconFor(item)) {
            // miniatury bywają prostokątne — wpisujemy w kwadrat z zachowaniem proporcji
            const D2D1_SIZE_F sz = icon->GetSize();
            const float k = std::min(kIcon / sz.width, kIcon / sz.height);
            const float w = sz.width * k, h = sz.height * k;
            const D2D1_RECT_F dst{cx - w / 2, top + (kIcon - h), cx + w / 2, top + kIcon};
            dc->DrawBitmap(icon, &dst, moving ? 0.75f : 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
        if (renameItem_ == static_cast<int>(i)) continue;  // napis zasłania pole edycji
        if (IDWriteTextLayout* label = LabelFor(item)) {
            const D2D1_POINT_2F at{r.left - 1.f, r.top + kLabelTop - 2.f};
            brush_->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, 0.85f));  // cień — czytelność na jasnej tapecie
            dc->DrawTextLayout({at.x + 1.f, at.y + 1.f}, label, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 1.f));
            dc->DrawTextLayout(at, label, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    if (mode_ == Mode::Selecting) {
        const D2D1_RECT_F band{std::min(downPt_.x, curPt_.x), std::min(downPt_.y, curPt_.y),
                               std::max(downPt_.x, curPt_.x), std::max(downPt_.y, curPt_.y)};
        brush_->SetColor(D2D1::ColorF(0.4f, 0.6f, 1.f, 0.20f));
        dc->FillRectangle(band, brush_.Get());
        brush_->SetColor(D2D1::ColorF(0.5f, 0.7f, 1.f, 0.80f));
        dc->DrawRectangle(band, brush_.Get(), 1.f);
    }

    });
    iconSurface_->Trim(IsRectEmpty(&cur) ? nullptr : &cur, IsRectEmpty(&cur) ? 0 : 1);  // zwalnia puste kafelki
    lastBounds_ = cur;
    if (!ok || FAILED(g_->DComp()->Commit())) {  // urządzenie utracone — spróbujemy za chwilę od nowa
        iconSurface_.Reset();
        SetTimer(hwnd_, kRetryTimer, 500, nullptr);
    }
}

void Desktop::SelectOnly(int index) {
    for (size_t i = 0; i < items_.size(); ++i) items_[i].selected = static_cast<int>(i) == index;
}

void Desktop::UpdateRubberBand() {
    const D2D1_RECT_F band{std::min(downPt_.x, curPt_.x), std::min(downPt_.y, curPt_.y), std::max(downPt_.x, curPt_.x),
                           std::max(downPt_.y, curPt_.y)};
    const bool additive = GetKeyState(VK_CONTROL) < 0;
    for (DesktopItem& item : items_) {
        const bool inside = Intersects(band, ContentRect(item));
        if (additive) item.selected = item.selected || inside;
        else item.selected = inside;
    }
}

// Przesuwa zaznaczone ikony o tyle pól, o ile przeciągnięto; zajęte pole → najbliższe wolne dalej.
void Desktop::DropDragged() {
    const int dc = static_cast<int>(std::lround((curPt_.x - downPt_.x) / kCellW));
    const int dr = static_cast<int>(std::lround((curPt_.y - downPt_.y) / kCellH));
    if (dc == 0 && dr == 0) return;
    const size_t total = static_cast<size_t>(cols_ * rows_);
    std::vector<bool> taken(total, false);
    for (const DesktopItem& item : items_)
        if (!item.selected) taken[static_cast<size_t>(item.col * rows_ + item.row)] = true;

    for (DesktopItem& item : items_) {
        if (!item.selected) continue;
        const int c = std::clamp(item.col + dc, 0, cols_ - 1), r = std::clamp(item.row + dr, 0, rows_ - 1);
        const size_t start = static_cast<size_t>(c * rows_ + r);
        for (size_t k = 0; k < total; ++k) {
            const size_t slot = (start + k) % total;
            if (taken[slot]) continue;
            taken[slot] = true;
            item.col = static_cast<int>(slot) / rows_;
            item.row = static_cast<int>(slot) % rows_;
            break;
        }
    }
    AssignPositions();  // zapisuje układ
}

void Desktop::OpenSelected() {
    for (const DesktopItem& item : items_) {
        if (!item.selected) continue;
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHGetIDListFromObject(item.item.Get(), &pidl))) continue;
        SHELLEXECUTEINFOW sei{sizeof(sei)};
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.hwnd = hwnd_;
        sei.lpIDList = pidl;
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
        CoTaskMemFree(pidl);
    }
}

void Desktop::DeleteSelected(bool permanent) {
    ComPtr<IFileOperation> op;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))) return;
    op->SetOwnerWindow(hwnd_);
    op->SetOperationFlags(permanent ? FOF_NOCONFIRMMKDIR : (FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE));
    bool any = false;
    for (const DesktopItem& item : items_) {
        if (!item.selected || IsRecycleBin(item)) continue;
        any |= SUCCEEDED(op->DeleteItem(item.item.Get(), nullptr));
    }
    if (any) op->PerformOperations();  // obserwator folderu odświeży pulpit
}

void Desktop::StartRename(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    CommitRename(false);
    DesktopItem& item = items_[static_cast<size_t>(index)];
    PWSTR name = nullptr;
    if (FAILED(item.item->GetDisplayName(SIGDN_PARENTRELATIVEEDITING, &name))) return;
    renameOriginal_ = name;
    CoTaskMemFree(name);

    const float s = dpi_ / 96.f;
    const D2D1_RECT_F r = ContentRect(item);
    const int x = monitor_.left + static_cast<int>((r.left - 2.f) * s);
    const int y = monitor_.top + static_cast<int>((r.top + kLabelTop - 3.f) * s);
    const int w = static_cast<int>((r.right - r.left + 4.f) * s), h = static_cast<int>(22.f * s);
    renameEdit_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"EDIT", renameOriginal_.c_str(),
                                  WS_POPUP | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL, x, y, w, h, hwnd_, nullptr, inst_,
                                  nullptr);
    if (!renameEdit_) return;
    renameItem_ = index;
    HFONT font = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SendMessageW(renameEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_editProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(renameEdit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameEditProc)));

    // Zaznacz nazwę bez rozszerzenia (jeśli rozszerzenia są widoczne)
    SFGAOF attrs = 0;
    item.item->GetAttributes(SFGAO_FOLDER, &attrs);
    const size_t dot = renameOriginal_.find_last_of(L'.');
    const LPARAM end = !(attrs & SFGAO_FOLDER) && dot != std::wstring::npos && dot > 0 ? static_cast<LPARAM>(dot) : -1;
    ShowWindow(renameEdit_, SW_SHOW);
    SetForegroundWindow(renameEdit_);
    SetFocus(renameEdit_);
    SendMessageW(renameEdit_, EM_SETSEL, 0, end);
    RequestRedraw();
}

void Desktop::CommitRename(bool accept) {
    if (!renameEdit_) return;
    const HWND edit = renameEdit_;
    const int index = renameItem_;
    renameEdit_ = nullptr;
    renameItem_ = -1;

    wchar_t text[MAX_PATH]{};
    GetWindowTextW(edit, text, ARRAYSIZE(text));
    const auto font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    DestroyWindow(edit);
    if (font) DeleteObject(font);
    RequestRedraw();
    if (!accept || index < 0 || index >= static_cast<int>(items_.size()) || !*text || renameOriginal_ == text) return;

    // SetNameOf zachowuje ukryte rozszerzenie (tak zmienia nazwy explorer)
    DesktopItem& item = items_[static_cast<size_t>(index)];
    PIDLIST_ABSOLUTE abs = nullptr;
    ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD child = nullptr;
    if (FAILED(SHGetIDListFromObject(item.item.Get(), &abs))) return;
    if (SUCCEEDED(SHBindToParent(abs, IID_PPV_ARGS(&parent), &child))) {
        PITEMID_CHILD renamed = nullptr;
        if (SUCCEEDED(parent->SetNameOf(hwnd_, child, text, SHGDN_INFOLDER | SHGDN_FOREDITING, &renamed)) && renamed) {
            ComPtr<IShellItem> newItem;
            if (SUCCEEDED(SHCreateItemWithParent(nullptr, parent.Get(), renamed, IID_PPV_ARGS(&newItem)))) {
                item.key = ItemKey(newItem.Get());  // ikona zostaje na swoim miejscu
                item.label.Reset();
            }
            CoTaskMemFree(renamed);
        }
    }
    CoTaskMemFree(abs);
    Refresh();
}

void Desktop::ShowItemMenu(int index, POINT pt) {
    // Menu kontekstowe Windows dla zaznaczonych elementów z tego samego folderu co kliknięty.
    PIDLIST_ABSOLUTE clicked = nullptr;
    if (FAILED(SHGetIDListFromObject(items_[static_cast<size_t>(index)].item.Get(), &clicked))) return;
    ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD clickedChild = nullptr;
    if (FAILED(SHBindToParent(clicked, IID_PPV_ARGS(&parent), &clickedChild))) {
        CoTaskMemFree(clicked);
        return;
    }
    PIDLIST_ABSOLUTE parentPidl = ILClone(clicked);
    ILRemoveLastID(parentPidl);

    std::vector<PIDLIST_ABSOLUTE> owned;
    std::vector<PCUITEMID_CHILD> children{clickedChild};
    for (size_t i = 0; i < items_.size(); ++i) {
        if (!items_[i].selected || static_cast<int>(i) == index) continue;
        PIDLIST_ABSOLUTE p = nullptr;
        if (FAILED(SHGetIDListFromObject(items_[i].item.Get(), &p))) continue;
        owned.push_back(p);
        if (ILIsParent(parentPidl, p, TRUE)) children.push_back(ILFindLastID(p));
    }

    ComPtr<IContextMenu> menu;
    if (SUCCEEDED(parent->GetUIObjectOf(hwnd_, static_cast<UINT>(children.size()), children.data(), IID_IContextMenu,
                                        nullptr, reinterpret_cast<void**>(menu.GetAddressOf())))) {
        HMENU popup = CreatePopupMenu();
        constexpr UINT kFirst = 1, kLast = 0x7FFF;
        const UINT flags = CMF_NORMAL | CMF_CANRENAME | (GetKeyState(VK_SHIFT) < 0 ? CMF_EXTENDEDVERBS : 0);
        if (SUCCEEDED(menu->QueryContextMenu(popup, 0, kFirst, kLast, flags))) {
            ComPtr<IContextMenu2> menu2;
            menu.As(&menu2);
            activeMenu_ = menu2.Get();
            const UINT cmd = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr);
            activeMenu_ = nullptr;
            if (cmd >= kFirst) {
                wchar_t verb[64]{};
                const bool rename =
                    SUCCEEDED(menu->GetCommandString(cmd - kFirst, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb),
                                                     ARRAYSIZE(verb))) &&
                    _wcsicmp(verb, L"rename") == 0;
                if (rename) {
                    StartRename(index);  // "Zmień nazwę" wymaga widoku — robimy ją sami
                } else {
                    CMINVOKECOMMANDINFOEX ici{sizeof(ici)};
                    ici.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                    ici.hwnd = hwnd_;
                    ici.lpVerb = MAKEINTRESOURCEA(cmd - kFirst);
                    ici.lpVerbW = MAKEINTRESOURCEW(cmd - kFirst);
                    ici.nShow = SW_SHOWNORMAL;
                    ici.ptInvoke = pt;
                    menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&ici));
                }
            }
        }
        DestroyMenu(popup);
    }
    for (PIDLIST_ABSOLUTE p : owned) CoTaskMemFree(p);
    ILFree(parentPidl);
    CoTaskMemFree(clicked);
}

void Desktop::ShowDesktopMenu(POINT pt) {
    HMENU menu = CreatePopupMenu();
    if (kShowIcons) {
        AppendMenuW(menu, MF_STRING, CmdNewFolder, L"Nowy folder");
        AppendMenuW(menu, MF_STRING, CmdRefresh, L"Odśwież");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, CmdWallpaper, L"Zmień tapetę…");
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case CmdNewFolder: NewFolder(); break;
    case CmdRefresh:
        for (DesktopItem& item : items_) item.iconLoaded = false;  // także nowe miniatury
        Refresh();
        break;
    case CmdWallpaper: ChooseWallpaper(); break;
    }
}

void Desktop::NewFolder() {
    ComPtr<IShellItem> desktop;
    ComPtr<IFileOperation> op;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&desktop))) ||
        FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op))))
        return;
    op->SetOwnerWindow(hwnd_);
    op->SetOperationFlags(FOF_RENAMEONCOLLISION | FOF_NOCONFIRMMKDIR);
    if (SUCCEEDED(op->NewItem(desktop.Get(), FILE_ATTRIBUTE_DIRECTORY, L"Nowy folder", nullptr, nullptr)))
        op->PerformOperations();
}

void Desktop::ChooseWallpaper() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) return;
    const COMDLG_FILTERSPEC types[] = {{L"Obrazy", L"*.jpg;*.jpeg;*.png;*.bmp;*.webp;*.jfif"}};
    dlg->SetFileTypes(ARRAYSIZE(types), types);
    dlg->SetTitle(L"Wybierz tapetę");
    ComPtr<IShellItem> picked;
    PWSTR path = nullptr;
    if (FAILED(dlg->Show(hwnd_)) || FAILED(dlg->GetResult(&picked)) || FAILED(picked->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return;
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, path, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    CoTaskMemFree(path);
    DrawWallpaper();
    RequestRedraw();
}

void Desktop::StartWatcher() {
    std::vector<std::wstring> dirs;
    for (const KNOWNFOLDERID* id : {&FOLDERID_Desktop, &FOLDERID_PublicDesktop}) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, 0, nullptr, &p))) dirs.emplace_back(p);
        CoTaskMemFree(p);
    }
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return;
    watcher_ = std::thread([dirs, target = hwnd_, stop = stopEvent_] {
        std::vector<HANDLE> handles{stop};
        for (const std::wstring& d : dirs) {
            const HANDLE h = FindFirstChangeNotificationW(
                d.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES);
            if (h != INVALID_HANDLE_VALUE) handles.push_back(h);
        }
        for (;;) {
            const DWORD r = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
            if (r == WAIT_OBJECT_0 || r < WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + handles.size()) break;
            PostMessageW(target, WM_DESKTOP_CHANGED, 0, 0);
            FindNextChangeNotification(handles[r - WAIT_OBJECT_0]);
        }
        for (size_t i = 1; i < handles.size(); ++i) FindCloseChangeNotification(handles[i]);
    });
}

void Desktop::StopWatcher() {
    if (stopEvent_) SetEvent(stopEvent_);
    if (watcher_.joinable()) watcher_.join();
    if (stopEvent_) CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

LRESULT CALLBACK Desktop::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<Desktop*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Desktop*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Desktop::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r = 0; files_.View().HandleMenuMessage(msg, wp, lp, r)) return r;
    if (activeMenu_ && (msg == WM_INITMENUPOPUP || msg == WM_DRAWITEM || msg == WM_MEASUREITEM)) {
        activeMenu_->HandleMenuMsg(msg, wp, lp);
        return msg == WM_INITMENUPOPUP ? 0 : TRUE;
    }
    switch (msg) {
    case WM_DESKTOP_REDRAW:
        redrawPending_ = false;
        Render();
        return 0;
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        RequestRedraw();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_WINDOWPOSCHANGING:  // pulpit zawsze na samym dole
        reinterpret_cast<WINDOWPOS*>(lp)->hwndInsertAfter = HWND_BOTTOM;
        reinterpret_cast<WINDOWPOS*>(lp)->flags &= ~SWP_NOZORDER;
        return 0;
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETDESKWALLPAPER) {
            DrawWallpaper();
            RequestRedraw();
        } else if (wp == SPI_SETWORKAREA) {
            UpdateWorkArea();
            DrawWallpaper();  // szkło panelu przesuwa się razem z obszarem roboczym
            AssignPositions();
            RequestRedraw();
        }
        return 0;
    case WM_DISPLAYCHANGE: {
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        monitor_ = mi.rcMonitor;
        widthDip_ = (monitor_.right - monitor_.left) * 96.f / dpi_;
        heightDip_ = (monitor_.bottom - monitor_.top) * 96.f / dpi_;
        SetWindowPos(hwnd_, HWND_BOTTOM, monitor_.left, monitor_.top, monitor_.right - monitor_.left,
                     monitor_.bottom - monitor_.top, SWP_NOACTIVATE);
        if (CreateResources()) DrawWallpaper();
        UpdateWorkArea();
        AssignPositions();
        RequestRedraw();
        return 0;
    }
    case WM_FILES_CHANGED:
        SetTimer(hwnd_, kFilesReloadTimer, 200, nullptr);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            SetCursor(files_.CursorAt({pt.x * 96.f / dpi_, pt.y * 96.f / dpi_}));  // strzałki zmiany rozmiaru na krawędziach panelu
            return TRUE;
        }
        break;
    case WM_DESKTOP_CHANGED:
        SetTimer(hwnd_, kRefreshTimer, 250, nullptr);  // zlewamy serię zmian w jedno odświeżenie
        return 0;
    case WM_TIMER:
        if (wp == kStatsTimer) {
            stats_.Sample();
            if (const float h = stats_.Height(); h != statsHeight_) {  // nowa aplikacja na liście → panel wyższy
                statsHeight_ = h;
                UpdateWorkArea();
                DrawWallpaper();
            }
            RequestRedraw();
            return 0;
        }
        KillTimer(hwnd_, wp);
        if (wp == kRefreshTimer) Refresh();
        else if (wp == kFilesReloadTimer) files_.View().Reload();
        else if (wp == kRetryTimer) RequestRedraw();
        return 0;
    case WM_RENAME_DONE:
        CommitRename(wp != 0);
        return 0;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
        }
        curPt_ = ToDip(lp);
        if (filesCapture_ || files_.Contains(curPt_)) {
            files_.OnMouseMove(curPt_);
            launcher_.OnMouseLeave();
            return 0;
        }
        files_.OnMouseLeave();
        if (mode_ == Mode::None) {
            if (launcher_.Contains(curPt_)) launcher_.OnMouseMove(curPt_);
            else launcher_.OnMouseLeave();
        }
        if (mode_ == Mode::Pending) {
            const float tx = GetSystemMetrics(SM_CXDRAG) * 96.f / dpi_, ty = GetSystemMetrics(SM_CYDRAG) * 96.f / dpi_;
            if (std::abs(curPt_.x - downPt_.x) > tx || std::abs(curPt_.y - downPt_.y) > ty) mode_ = Mode::Dragging;
        }
        if (mode_ == Mode::Selecting) UpdateRubberBand();
        if (mode_ == Mode::Dragging || mode_ == Mode::Selecting) {
            RequestRedraw();
        } else if (const int hit = ItemAt(curPt_.x, curPt_.y); hit != hover_) {
            hover_ = hit;
            RequestRedraw();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        launcher_.OnMouseLeave();
        files_.OnMouseLeave();
        if (hover_ != -1) {
            hover_ = -1;
            RequestRedraw();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        CommitRename(true);
        SetFocus(hwnd_);
        downPt_ = curPt_ = ToDip(lp);
        if (files_.OnMouseDown(downPt_, (wp & MK_CONTROL) != 0, (wp & MK_SHIFT) != 0)) {
            launcher_.SetFocused(false);
            filesCapture_ = true;
            SetCapture(hwnd_);
            return 0;
        }
        files_.SetFocused(false);
        if (launcher_.Contains(downPt_)) {
            launcher_.OnClick(downPt_, hwnd_);
            return 0;
        }
        launcher_.SetFocused(false);
        if (!kShowIcons) return 0;
        downItem_ = ItemAt(downPt_.x, downPt_.y);
        const bool ctrl = (wp & MK_CONTROL) != 0;
        if (downItem_ >= 0) {
            DesktopItem& item = items_[static_cast<size_t>(downItem_)];
            if (ctrl) item.selected = !item.selected;
            else if (!item.selected) SelectOnly(downItem_);
            mode_ = item.selected ? Mode::Pending : Mode::None;
        } else {
            if (!ctrl) SelectOnly(-1);
            mode_ = Mode::Selecting;
        }
        SetCapture(hwnd_);
        RequestRedraw();
        return 0;
    }
    case WM_LBUTTONUP: {
        if (filesCapture_) {
            filesCapture_ = false;
            ReleaseCapture();
            files_.OnMouseUp(ToDip(lp));
            return 0;
        }
        const Mode mode = mode_;
        mode_ = Mode::None;  // przed ReleaseCapture — WM_CAPTURECHANGED nie może przerwać upuszczenia
        ReleaseCapture();
        curPt_ = ToDip(lp);
        if (mode == Mode::Dragging) DropDragged();
        else if (mode == Mode::Pending && !(wp & MK_CONTROL)) SelectOnly(downItem_);
        RequestRedraw();
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (filesCapture_) {  // np. menu albo okno dialogowe przejęło mysz
            filesCapture_ = false;
            files_.OnMouseUp(curPt_);
        }
        if (mode_ != Mode::None) {
            mode_ = Mode::None;
            RequestRedraw();
        }
        return 0;
    case WM_LBUTTONDBLCLK: {
        const D2D1_POINT_2F p = ToDip(lp);
        if (files_.Contains(p)) {
            files_.OnDoubleClick(p);
            return 0;
        }
        const int hit = ItemAt(p.x, p.y);
        if (hit >= 0) {
            SelectOnly(hit);
            RequestRedraw();
            OpenSelected();
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        CommitRename(true);
        const D2D1_POINT_2F p = ToDip(lp);
        if (files_.Contains(p)) {
            POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd_, &screen);
            launcher_.SetFocused(false);
            files_.OnRightClick(p, screen);
            return 0;
        }
        if (launcher_.Contains(p)) {
            POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd_, &screen);
            launcher_.OnRightClick(p, screen, hwnd_);
            return 0;
        }
        const int hit = ItemAt(p.x, p.y);
        POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd_, &screen);
        if (hit >= 0) {
            if (!items_[static_cast<size_t>(hit)].selected) SelectOnly(hit);
            RequestRedraw();
            ShowItemMenu(hit, screen);
        } else {
            SelectOnly(-1);
            RequestRedraw();
            ShowDesktopMenu(screen);
        }
        return 0;
    }
    case WM_CHAR:
        if (!files_.OnChar(static_cast<wchar_t>(wp))) launcher_.OnChar(static_cast<wchar_t>(wp), hwnd_);
        return 0;
    case WM_SYSKEYDOWN:  // Alt+strzałki w panelu "Pliki"
        if ((wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP) && files_.OnKey(static_cast<UINT>(wp), false, false, true)) return 0;
        break;
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        const D2D1_POINT_2F p{pt.x * 96.f / dpi_, pt.y * 96.f / dpi_};
        if (files_.Contains(p)) files_.OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
        else if (launcher_.Contains(p)) launcher_.OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    }
    case WM_KILLFOCUS:
        launcher_.SetFocused(false);
        files_.SetFocused(false);
        return 0;
    case WM_KEYDOWN: {
        if (files_.OnKey(static_cast<UINT>(wp), GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0, false)) return 0;
        if (!kShowIcons) return 0;
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        switch (wp) {
        case VK_RETURN: OpenSelected(); break;
        case VK_DELETE: DeleteSelected(GetKeyState(VK_SHIFT) < 0); break;
        case VK_F5: Refresh(); break;
        case VK_F2:
            for (size_t i = 0; i < items_.size(); ++i)
                if (items_[i].selected) {
                    StartRename(static_cast<int>(i));
                    break;
                }
            break;
        case 'A':
            if (ctrl) {
                for (DesktopItem& item : items_) item.selected = true;
                RequestRedraw();
            }
            break;
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
