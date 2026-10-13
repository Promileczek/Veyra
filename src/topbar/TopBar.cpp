#include "TopBar.h"
#include "../core/Accent.h"
#include "../core/UiScale.h"
#include "../core/WorkArea.h"
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr wchar_t kClassName[] = L"ExploderTopBar";
constexpr UINT WM_APPBAR = WM_APP + 1;
constexpr UINT WM_REDRAW = WM_APP + 2;
constexpr UINT WM_MEDIA = WM_APP + 3;
constexpr UINT WM_VOLUME = WM_APP + 4;
constexpr UINT_PTR kStatusTimer = 1, kProgressTimer = 2, kFullscreenTimer = 3, kMediaPollTimer = 4;

// Wymiary (DIP)
constexpr float kWidth = 860.f, kHeight = 36.f, kMargin = 6.f, kRadius = 14.f;

// Segoe MDL2 Assets
constexpr wchar_t kWifi[] = {L'\xE872', L'\xE873', L'\xE874', L'\xE701'};  // 1–4 kreski
constexpr wchar_t kWifiNoInternet = L'\xEB5E', kEthernet = L'\xE839', kOffline = L'\xF384';
constexpr wchar_t kPrev = L'\xE892', kPlay = L'\xE768', kPause = L'\xE769', kNext = L'\xE893', kMusic = L'\xE8D6';
constexpr wchar_t kMute = L'\xE74F', kVol[] = {L'\xE992', L'\xE993', L'\xE994', L'\xE995'};


bool Contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

std::wstring FormatTime(double sec) {
    const int s = std::max(0, static_cast<int>(sec));
    wchar_t buf[16];
    swprintf_s(buf, L"%d:%02d", s / 60, s % 60);
    return buf;
}
}  // namespace

bool TopBar::Create(HINSTANCE inst, Graphics& g) {
    g_ = &g;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, kClassName,
                            L"Exploder TopBar", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    dpi_ = UiScale::Dpi(GetDpiForWindow(hwnd_));
    if (!SetBackdrop(hwnd_, Backdrop::Acrylic, theme_.acrylicTint)) theme_.dockBackground.a = 0.95f;

    IDWriteFactory* dw = g_->DWrite();
    if (FAILED(dw->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, 14.f, L"pl-PL", &glyphFormat_)) ||
        FAILED(dw->CreateTextFormat(theme_.font, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, 12.f, L"pl-PL", &textFormat_)) ||
        FAILED(dw->CreateTextFormat(theme_.font, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, 11.f, L"pl-PL", &smallFormat_)))
        return false;
    glyphFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    glyphFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    dw->CreateEllipsisTrimmingSign(textFormat_.Get(), &ellipsis_);
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    for (IDWriteTextFormat* f : {textFormat_.Get(), smallFormat_.Get()}) {
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetTrimming(&trimming, ellipsis_.Get());
    }
    if (!CreateResources()) return false;

    APPBARDATA abd{sizeof(abd)};
    abd.hWnd = hwnd_;
    abd.uCallbackMessage = WM_APPBAR;
    appbar_ = SHAppBarMessage(ABM_NEW, &abd) != FALSE;  // bez explorera nie działa → WorkArea
    ReserveSpace();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);

    net_ = network_.Query();
    volume_.Start(hwnd_, WM_VOLUME);
    volumePopup_.Init(volume_);
    volumePopup_.Create(inst, g, L"ExploderVolumePopup");
    networkPopup_.Init(network_);
    networkPopup_.Create(inst, g, L"ExploderNetworkPopup");
    media_.Start(hwnd_, WM_MEDIA);
    SetTimer(hwnd_, kMediaPollTimer, 1500, nullptr);  // tryb zastępczy bez explorera (patrz MediaService.h)
    SetTimer(hwnd_, kStatusTimer, 5000, nullptr);
    // Bez explorera nikt nie przyśle ABN_FULLSCREENAPP — sprawdzamy sami co pół sekundy (film w przeglądarce
    // przechodzi na pełny ekran w tym samym oknie, więc zmiana aktywnego okna nie wystarcza).
    if (!appbar_) SetTimer(hwnd_, kFullscreenTimer, 500, nullptr);
    RequestRedraw();
    return true;
}

void TopBar::Destroy() {
    media_.Stop();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

bool TopBar::CreateResources() {
    surface_ = Surface{};
    brush_.Reset();
    cover_.Reset();
    coverVersion_ = ~0u;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (!surface_.Create(*g_, hwnd_, static_cast<UINT>(std::max<LONG>(rc.right, 1)), static_cast<UINT>(std::max<LONG>(rc.bottom, 1))))
        return false;
    surface_.Context()->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    surface_.Context()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return SUCCEEDED(surface_.Context()->CreateSolidColorBrush(theme_.text, &brush_));
}

void TopBar::ReserveSpace() {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const float s = dpi_ / 96.f;
    const int reserve = static_cast<int>(std::lround((kHeight + 2 * kMargin) * s));
    RECT area = mi.rcMonitor;
    area.bottom = area.top + reserve;
    if (appbar_) {
        APPBARDATA abd{sizeof(abd)};
        abd.hWnd = hwnd_;
        abd.uEdge = ABE_TOP;
        abd.rc = area;
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        abd.rc.bottom = abd.rc.top + reserve;
        SHAppBarMessage(ABM_SETPOS, &abd);
        area = abd.rc;
    } else {
        WorkArea::Reserve(ABE_TOP, reserve);
    }
    const int monitorW = area.right - area.left;
    const int w = std::min(static_cast<int>(std::lround(kWidth * s)), monitorW - 2 * static_cast<int>(kMargin * s));
    const int h = static_cast<int>(std::lround(kHeight * s));
    SetWindowPos(hwnd_, HWND_TOPMOST, area.left + (monitorW - w) / 2, area.top + static_cast<int>(std::lround(kMargin * s)), w, h,
                 SWP_NOACTIVATE);
}

void TopBar::RequestRedraw() {
    if (redrawPending_ || !hwnd_) return;
    redrawPending_ = true;
    PostMessageW(hwnd_, WM_REDRAW, 0, 0);
}

void TopBar::Layout() {
    const float H = heightDip_, W = widthDip_;
    netRect_ = {10.f, 4.f, 190.f, H - 4.f};
    volumeRect_ = {W - 86.f, 4.f, W - 10.f, H - 4.f};

    // Środek: okładka | tytuł — wykonawca | ⏮ ⏯ ⏭ | postęp — zwarty blok zawsze na środku: szerokość tytułu
    // dopasowana do tekstu (60–240), przyciski tuż za nim, pasek postępu tylko gdy znany czas utworu
    const MediaState& m = mediaState_;
    float titleW = TextWidth(L"Nic nie gra", smallFormat_.Get()) + 4.f;
    bool progress = false;
    if (m.hasSession) {
        titleW = std::clamp(std::max(TextWidth(m.title.empty() ? L"(bez tytułu)" : m.title, textFormat_.Get()),
                                     TextWidth(m.artist, smallFormat_.Get())) + 6.f, 60.f, 240.f);
        progress = m.durationSec > 0.5;
    }
    const float controls = m.hasSession ? 8.f + 3 * 28.f : 0.f;
    const float block = 24.f + 8.f + titleW + controls + (progress ? 10.f + 130.f : 0.f);
    float x = (W - block) / 2;
    mediaRect_ = {x - 6.f, 3.f, x + block + 6.f, H - 3.f};
    coverRect_ = {x, (H - 24.f) / 2, x + 24.f, (H + 24.f) / 2};
    x += 32.f;
    titleRect_ = {x, 0.f, x + titleW, H};
    x += titleW + 8.f;
    prevRect_ = {x, 4.f, x + 28.f, H - 4.f};
    playRect_ = {x + 28.f, 4.f, x + 56.f, H - 4.f};
    nextRect_ = {x + 56.f, 4.f, x + 84.f, H - 4.f};
    x += 94.f;
    progressRect_ = {x, 0.f, x + 130.f, H};
}

float TopBar::TextWidth(const std::wstring& text, IDWriteTextFormat* format) const {
    ComPtr<IDWriteTextLayout> layout;
    if (text.empty() || !format ||
        FAILED(g_->DWrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, 2000.f, 100.f, &layout)))
        return 0.f;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}

void TopBar::Glyph(wchar_t glyph, const D2D1_RECT_F& r, const D2D1_COLOR_F& color) {
    brush_->SetColor(color);
    surface_.Context()->DrawText(&glyph, 1, glyphFormat_.Get(), r, brush_.Get());
}

void TopBar::Text(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& r, const D2D1_COLOR_F& color) {
    brush_->SetColor(color);
    surface_.Context()->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, r, brush_.Get(),
                                 D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void TopBar::UpdateCover(const MediaState& media) {
    if (media.thumbnailVersion == coverVersion_) return;
    coverVersion_ = media.thumbnailVersion;
    cover_.Reset();
    IWICImagingFactory* wic = g_->WIC();
    if (media.thumbnail.empty()) {  // tryb zastępczy: ikona programu (Spotify, przeglądarka)
        HICON icon = nullptr;
        const int px = static_cast<int>(std::ceil(24.f * dpi_ / 96.f));
        ComPtr<IWICBitmap> bmp;
        ComPtr<IWICFormatConverter> conv;
        if (!media.sourcePath.empty() &&
            PrivateExtractIconsW(media.sourcePath.c_str(), 0, px, px, &icon, nullptr, 1, LR_DEFAULTCOLOR) == 1 && icon) {
            if (SUCCEEDED(wic->CreateBitmapFromHICON(icon, &bmp)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(bmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                           WICBitmapPaletteTypeMedianCut)))
                surface_.Context()->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &cover_);
            DestroyIcon(icon);
        }
        return;
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> conv;
    const UINT px = static_cast<UINT>(std::ceil(24.f * dpi_ / 96.f));
    if (SUCCEEDED(wic->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(media.thumbnail.data()), static_cast<DWORD>(media.thumbnail.size()))) &&
        SUCCEEDED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame.Get(), px, px, WICBitmapInterpolationModeHighQualityCubic)) &&
        SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                   WICBitmapPaletteTypeMedianCut)))
        surface_.Context()->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &cover_);
}

void TopBar::Render() {
    if (!surface_.Context() && !CreateResources()) return;
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc) return;
    const float W = widthDip_, H = heightDip_, px = 96.f / dpi_;
    const D2D1_COLOR_F dim{1.f, 1.f, 1.f, 0.6f};

    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto shape = D2D1::RoundedRect({px / 2, px / 2, W - px / 2, H - px / 2}, kRadius, kRadius);
    brush_->SetColor(theme_.dockBackground);
    dc->FillRoundedRectangle(shape, brush_.Get());
    brush_->SetColor(theme_.dockBorder);
    dc->DrawRoundedRectangle(shape, brush_.Get(), px);

    auto hoverFill = [&](int hit, const D2D1_RECT_F& r) {
        if (hover_ != hit) return;
        brush_->SetColor(theme_.hover);
        dc->FillRoundedRectangle(D2D1::RoundedRect(r, 8.f, 8.f), brush_.Get());
    };

    // Sieć
    hoverFill(HitNetwork, netRect_);
    wchar_t netGlyph = kOffline;
    std::wstring netText = L"Brak sieci";
    if (net_.kind == NetworkInfo::Kind::Wifi) {
        netGlyph = net_.internet ? kWifi[std::clamp(net_.signal / 25, 0, 3)] : kWifiNoInternet;
        netText = net_.name;
    } else if (net_.kind == NetworkInfo::Kind::Ethernet) {
        netGlyph = kEthernet;
        netText = net_.internet ? L"Ethernet" : L"Ethernet — brak internetu";
    }
    Glyph(netGlyph, {netRect_.left + 4.f, 0.f, netRect_.left + 28.f, H}, theme_.text);
    Text(netText, smallFormat_.Get(), {netRect_.left + 32.f, 0.f, netRect_.right - 6.f, H}, theme_.text);

    // Teraz odtwarzane
    const MediaState& m = mediaState_;
    if (!m.hasSession) {
        Glyph(kMusic, {coverRect_.left, 0.f, coverRect_.right, H}, dim);
        Text(L"Nic nie gra", smallFormat_.Get(), titleRect_, dim);
    } else {
        UpdateCover(m);
        hoverFill(HitMedia, {coverRect_.left - 4.f, 4.f, titleRect_.right, H - 4.f});
        if (cover_) {
            dc->PushAxisAlignedClip(coverRect_, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            dc->DrawBitmap(cover_.Get(), &coverRect_, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            dc->PopAxisAlignedClip();
        } else {
            Glyph(kMusic, {coverRect_.left, 0.f, coverRect_.right, H}, theme_.text);
        }
        const float mid = H / 2;
        Text(m.title.empty() ? L"(bez tytułu)" : m.title, textFormat_.Get(), {titleRect_.left, 2.f, titleRect_.right, mid + 1.f},
             theme_.text);
        Text(m.artist, smallFormat_.Get(), {titleRect_.left, mid - 1.f, titleRect_.right, H - 2.f}, dim);

        hoverFill(HitPrev, prevRect_);
        hoverFill(HitPlay, playRect_);
        hoverFill(HitNext, nextRect_);
        Glyph(kPrev, prevRect_, theme_.text);
        Glyph(m.playing ? kPause : kPlay, playRect_, theme_.text);
        Glyph(kNext, nextRect_, theme_.text);

        if (m.durationSec > 0.5) {
            double pos = m.positionSec;
            if (m.playing) pos += (GetTickCount64() - m.positionTick) / 1000.0;
            pos = std::min(pos, m.durationSec);
            const float barY = H / 2 - 1.5f;
            const D2D1_RECT_F track{progressRect_.left, barY, progressRect_.right - 70.f, barY + 3.f};
            const float fill = track.left + (track.right - track.left) * static_cast<float>(pos / m.durationSec);
            brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.2f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(track, 1.5f, 1.5f), brush_.Get());
            brush_->SetColor(theme_.accent);
            dc->FillRoundedRectangle(D2D1::RoundedRect({track.left, track.top, fill, track.bottom}, 1.5f, 1.5f), brush_.Get());
            Text(FormatTime(pos) + L" / " + FormatTime(m.durationSec), smallFormat_.Get(),
                 {track.right + 8.f, 0.f, progressRect_.right + 10.f, H}, dim);
        }
    }

    // Głośność
    hoverFill(HitVolume, volumeRect_);
    if (volume_.Available()) {
        const float level = volume_.Level();
        const bool muted = volume_.Muted();
        const wchar_t g = muted ? kMute : kVol[std::clamp(static_cast<int>(level * 4.f), 0, 3)];
        Glyph(g, {volumeRect_.left + 4.f, 0.f, volumeRect_.left + 28.f, H}, theme_.text);
        Text(muted ? L"wycisz." : std::to_wstring(std::lround(level * 100.f)) + L"%", smallFormat_.Get(),
             {volumeRect_.left + 32.f, 0.f, volumeRect_.right, H}, theme_.text);
    }

    if (!surface_.End()) {
        surface_ = Surface{};
        SetTimer(hwnd_, kProgressTimer, 500, nullptr);  // spróbujemy ponownie
    }
}

int TopBar::HitTest(LPARAM lp) const {
    const float x = GET_X_LPARAM(lp) * 96.f / dpi_, y = GET_Y_LPARAM(lp) * 96.f / dpi_;
    if (Contains(netRect_, x, y)) return HitNetwork;
    if (Contains(volumeRect_, x, y)) return HitVolume;
    if (!mediaState_.hasSession) return HitNone;
    if (Contains(prevRect_, x, y)) return HitPrev;
    if (Contains(playRect_, x, y)) return HitPlay;
    if (Contains(nextRect_, x, y)) return HitNext;
    if (Contains({coverRect_.left - 4.f, 0.f, titleRect_.right, heightDip_}, x, y)) return HitMedia;
    return HitNone;
}

RECT TopBar::ScreenRect(const D2D1_RECT_F& r) const {
    RECT win{};
    GetWindowRect(hwnd_, &win);
    const float s = dpi_ / 96.f;
    return {win.left + static_cast<LONG>(r.left * s), win.top, win.left + static_cast<LONG>(r.right * s), win.bottom};
}

// Gra/film na pełnym ekranie → pasek znika, po wyjściu wraca. Tylko przy zmianie stanu — wcześniej
// ciągłe SetWindowPos(HWND_TOPMOST) wypychało pasek nad przyciemniony ekran wycinania.
void TopBar::UpdateFullscreen(HWND fg) {
    const bool fullscreen = WorkArea::IsFullscreenWindow(fg);
    if (fullscreen == hiddenForFullscreen_) return;
    hiddenForFullscreen_ = fullscreen;
    ShowWindow(hwnd_, fullscreen ? SW_HIDE : SW_SHOWNA);
}

LRESULT CALLBACK TopBar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<TopBar*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<TopBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TopBar::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == UiScale::ChangedMessage()) msg = WM_DISPLAYCHANGE;  // nowa skala Veyry: jak zmiana ekranu
    switch (msg) {
    case WM_REDRAW:
        redrawPending_ = false;
        Layout();
        Render();
        return 0;
    case WM_PAINT:
        ValidateRect(hwnd_, nullptr);
        RequestRedraw();
        return 0;
    case WM_SIZE: {
        if (surface_.Context()) surface_.Resize(LOWORD(lp), HIWORD(lp));
        widthDip_ = LOWORD(lp) * 96.f / dpi_;
        heightDip_ = HIWORD(lp) * 96.f / dpi_;
        const int d = static_cast<int>(std::lround(kRadius * 2 * dpi_ / 96.f));
        SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, LOWORD(lp) + 1, HIWORD(lp) + 1, d, d), TRUE);
        RequestRedraw();
        return 0;
    }
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:  // też zmiana rozdzielczości: skala "auto" zależy od wielkości ekranu
        UiScale::Reload();
        dpi_ = UiScale::Dpi(msg == WM_DPICHANGED ? HIWORD(wp) : GetDpiForWindow(hwnd_));
        if (surface_.Context()) surface_.Context()->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        coverVersion_ = ~0u;
        ReserveSpace();
        return 0;
    case WM_APPBAR:
        if (wp == ABN_POSCHANGED) ReserveSpace();
        else if (wp == ABN_FULLSCREENAPP)
            SetWindowPos(hwnd_, lp ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    case WM_MEDIA: {
        mediaState_ = media_.Snapshot();
        if (mediaState_.playing && mediaState_.durationSec > 0.5) SetTimer(hwnd_, kProgressTimer, 1000, nullptr);
        else KillTimer(hwnd_, kProgressTimer);
        RequestRedraw();
        return 0;
    }
    case WM_VOLUME:
        volumePopup_.Redraw();
        RequestRedraw();
        return 0;
    case WM_TIMER:
        if (wp == kStatusTimer) {
            if (const NetworkInfo n = network_.Query(); !(n == net_)) {
                net_ = n;
                RequestRedraw();
            }
            volume_.Refresh();
        } else if (wp == kMediaPollTimer) {
            if (media_.UsesPolling()) media_.Poll();
        } else if (wp == kFullscreenTimer) {
            UpdateFullscreen(GetForegroundWindow());
        } else if (wp == kProgressTimer) {
            if (!(mediaState_.playing && mediaState_.durationSec > 0.5)) KillTimer(hwnd_, kProgressTimer);
            RequestRedraw();
        }
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
        }
        if (const int hit = HitTest(lp); hit != hover_) {
            hover_ = hit;
            RequestRedraw();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        hover_ = HitNone;
        RequestRedraw();
        return 0;
    case WM_LBUTTONUP:
        switch (HitTest(lp)) {
        case HitPrev: media_.Previous(); break;
        case HitPlay: media_.PlayPause(); break;
        case HitNext: media_.Next(); break;
        case HitVolume: volume_.ToggleMute(); break;
        case HitNetwork: networkPopup_.Toggle(ScreenRect(netRect_), dpi_); break;
        default: break;
        }
        return 0;
    case WM_RBUTTONUP:
        if (HitTest(lp) == HitVolume) volumePopup_.Toggle(ScreenRect(volumeRect_), dpi_);  // suwak głośności
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        const float x = pt.x * 96.f / dpi_, y = pt.y * 96.f / dpi_;
        if (Contains(volumeRect_, x, y))  // kółko nad głośnością: ±2%
            volume_.SetLevel(volume_.Level() + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.02f : -0.02f));
        return 0;
    }
    case WM_DESTROY: {
        KillTimer(hwnd_, kStatusTimer);
        KillTimer(hwnd_, kFullscreenTimer);
        KillTimer(hwnd_, kMediaPollTimer);
        KillTimer(hwnd_, kProgressTimer);
        if (appbar_) {
            APPBARDATA abd{sizeof(abd)};
            abd.hWnd = hwnd_;
            SHAppBarMessage(ABM_REMOVE, &abd);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
