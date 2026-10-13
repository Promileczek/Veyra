#include "ClipboardPanel.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kW = 380.f, kHeader = 48.f, kPad = 10.f, kMaxH = 520.f;
constexpr size_t kMaxEntries = 25;
ClipboardPanel* g_panel = nullptr;

void FreeEntryBitmap(HBITMAP& b) {
    if (b) DeleteObject(b);
    b = nullptr;
}
}  // namespace

bool ClipboardPanel::Start(HINSTANCE inst, Graphics& g) {
    if (!Create(inst, g, L"ExploderClipboard")) return false;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    g.DWrite()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f,
                                 L"pl-PL", &itemFormat_);
    if (itemFormat_) {
        itemFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        itemFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        ComPtr<IDWriteInlineObject> ellipsis;
        g.DWrite()->CreateEllipsisTrimmingSign(itemFormat_.Get(), &ellipsis);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        itemFormat_->SetTrimming(&trimming, ellipsis.Get());
    }
    g.DWrite()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f,
                                 L"pl-PL", &buttonFormat_);
    if (buttonFormat_) {
        buttonFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        buttonFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    // Okno komunikatów nasłuchujące zmian schowka
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = ListenerProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"ExploderClipboardListener";
    RegisterClassExW(&wc);
    listener_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    if (!listener_) return false;
    g_panel = this;
    AddClipboardFormatListener(listener_);
    return true;
}

void ClipboardPanel::Stop() {
    if (listener_) {
        RemoveClipboardFormatListener(listener_);
        DestroyWindow(listener_);
        listener_ = nullptr;
    }
    for (Entry& e : entries_) FreeEntryBitmap(e.bitmap);
    entries_.clear();
    g_panel = nullptr;
}

LRESULT CALLBACK ClipboardPanel::ListenerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLIPBOARDUPDATE && g_panel) {
        g_panel->OnClipboardChanged();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ClipboardPanel::OnClipboardChanged() {
    if (ignoreNext_) {
        ignoreNext_ = false;
        return;
    }
    Entry e;
    bool ok = false;
    for (int attempt = 0; attempt < 5 && !ok; ++attempt) {  // schowek bywa chwilę zajęty przez program, który kopiuje
        if (!OpenClipboard(listener_)) {
            Sleep(30);
            continue;
        }
        ok = true;
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
                if (const auto* text = static_cast<const wchar_t*>(GlobalLock(h))) {
                    e.text.assign(text, wcsnlen(text, 200000));
                    GlobalUnlock(h);
                }
        } else if (IsClipboardFormatAvailable(CF_BITMAP)) {
            if (HANDLE h = GetClipboardData(CF_BITMAP))
                e.bitmap = static_cast<HBITMAP>(CopyImage(h, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
        }
        CloseClipboard();
    }
    if (e.text.empty() && !e.bitmap) return;
    if (!e.text.empty()) {  // ten sam tekst wcześniej → przenosimy na górę
        const auto same = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& o) { return !o.bitmap && o.text == e.text; });
        if (same != entries_.end()) entries_.erase(same);
    }
    entries_.insert(entries_.begin(), std::move(e));
    while (entries_.size() > kMaxEntries) {
        FreeEntryBitmap(entries_.back().bitmap);
        entries_.pop_back();
    }
    if (Visible()) Relayout();
}

void ClipboardPanel::Open(UINT dpi) {
    if (Visible()) return Hide();
    previous_ = GetForegroundWindow();
    MONITORINFO mi{sizeof(mi)};
    POINT cursor{};
    GetCursorPos(&cursor);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
    const LONG cx = (mi.rcWork.left + mi.rcWork.right) / 2;
    // Nad dockiem, na środku (okienko wychodzi nad kotwicę, gdy pod nią brak miejsca)
    Toggle({cx, mi.rcWork.bottom - 1, cx + 1, mi.rcWork.bottom}, dpi);
}

void ClipboardPanel::OnShow() {
    scroll_ = 0.f;
    hover_ = -1;
}

D2D1_SIZE_F ClipboardPanel::ContentSize() {
    float h = kHeader + kPad;
    for (const Entry& e : entries_) h += ItemHeight(e) + 6.f;
    if (entries_.empty()) h += 70.f;
    contentH_ = h;
    return {kW, std::min(h + kPad, kMaxH)};
}

int ClipboardPanel::HitAt(D2D1_POINT_2F p) const {
    if (!entries_.empty() && p.y >= 12.f && p.y < 38.f && p.x >= kW - 132.f && p.x < kW - 14.f) return -2;
    float y = kHeader - scroll_;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const float h = ItemHeight(entries_[i]);
        if (p.y >= std::max(y, kHeader) && p.y < y + h && p.x >= kPad && p.x < kW - kPad) return static_cast<int>(i);
        y += h + 6.f;
    }
    return -1;
}

void ClipboardPanel::Paint(ID2D1DeviceContext* dc) {
    Text(dc, L"Schowek", titleFormat_.Get(), {16.f, 8.f, 200.f, 42.f}, kText);
    if (!entries_.empty()) {
        const D2D1_RECT_F clear{kW - 132.f, 12.f, kW - 14.f, 38.f};
        Fill(dc, clear, hover_ == -2 ? kHover : D2D1::ColorF(1.f, 1.f, 1.f, 0.06f), 7.f);
        Text(dc, L"Wyczyść wszystko", buttonFormat_.Get(), clear, kDim);
    }
    const float bottom = size_.height - 6.f;
    dc->PushAxisAlignedClip({0.f, kHeader, kW, bottom}, D2D1_ANTIALIAS_MODE_ALIASED);
    if (entries_.empty())
        Text(dc, L"Skopiuj tekst albo obraz — pojawi się tutaj", textFormat_.Get(), {16.f, kHeader + 10.f, kW - 16.f, kHeader + 60.f}, kDim);
    float y = kHeader - scroll_;
    for (size_t i = 0; i < entries_.size(); ++i) {
        Entry& e = entries_[i];
        const float h = ItemHeight(e);
        const D2D1_RECT_F r{kPad, y, kW - kPad, y + h};
        y += h + 6.f;
        if (r.bottom < kHeader || r.top > bottom) continue;
        Fill(dc, r, static_cast<int>(i) == hover_ ? D2D1::ColorF(1.f, 1.f, 1.f, 0.14f) : D2D1::ColorF(1.f, 1.f, 1.f, 0.06f), 10.f);
        if (e.bitmap) {
            if (!e.d2d && wic_) {  // miniatura z kopii obrazu
                ComPtr<IWICBitmap> wb;
                ComPtr<IWICFormatConverter> conv;
                if (SUCCEEDED(wic_->CreateBitmapFromHBITMAP(e.bitmap, nullptr, WICBitmapIgnoreAlpha, &wb)) &&
                    SUCCEEDED(wic_->CreateFormatConverter(&conv)) &&
                    SUCCEEDED(conv->Initialize(wb.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                               WICBitmapPaletteTypeMedianCut)) &&
                    SUCCEEDED(dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &e.d2d))) {
                    UINT bw = 0, bh = 0;
                    wb->GetSize(&bw, &bh);
                    e.width = static_cast<float>(bw), e.height = static_cast<float>(bh);
                }
            }
            if (e.d2d && e.width > 0 && e.height > 0) {
                const float maxW = r.right - r.left - 20.f, maxH = h - 16.f;
                const float k = std::min(maxW / e.width, maxH / e.height);
                const float w = e.width * k, hh = e.height * k;
                const D2D1_RECT_F img{r.left + 10.f, r.top + (h - hh) / 2, r.left + 10.f + w, r.top + (h + hh) / 2};
                dc->DrawBitmap(e.d2d.Get(), img, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            }
        } else {
            std::wstring preview = e.text.substr(0, 400);
            std::replace(preview.begin(), preview.end(), L'\t', L' ');
            Text(dc, preview, itemFormat_.Get(), {r.left + 12.f, r.top + 6.f, r.right - 12.f, r.bottom - 6.f}, kText);
        }
    }
    dc->PopAxisAlignedClip();
}

void ClipboardPanel::OnMouseMove(D2D1_POINT_2F p) {
    const int h = p.x < 0 ? -1 : HitAt(p);
    if (h != hover_) {
        hover_ = h;
        Redraw();
    }
}

void ClipboardPanel::OnWheel(int delta) {
    const float maxScroll = std::max(0.f, contentH_ - size_.height + kPad);
    scroll_ = std::clamp(scroll_ - delta / 120.f * 60.f, 0.f, maxScroll);
    Redraw();
}

void ClipboardPanel::OnMouseDown(D2D1_POINT_2F p) {
    const int h = HitAt(p);
    if (h == -2) {
        for (Entry& e : entries_) FreeEntryBitmap(e.bitmap);
        entries_.clear();
        Relayout();
    } else if (h >= 0) {
        Paste(static_cast<size_t>(h));
    }
}

void ClipboardPanel::Paste(size_t index) {
    Entry e = std::move(entries_[index]);
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    // Do schowka (bez dopisywania drugi raz) i na górę listy
    if (OpenClipboard(listener_)) {
        EmptyClipboard();
        if (e.bitmap) {
            if (HANDLE copy = CopyImage(e.bitmap, IMAGE_BITMAP, 0, 0, 0)) SetClipboardData(CF_BITMAP, copy);
        } else if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (e.text.size() + 1) * sizeof(wchar_t))) {
            if (void* dst = GlobalLock(mem)) {
                memcpy(dst, e.text.c_str(), (e.text.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(mem);
                if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
            }
        }
        CloseClipboard();
        ignoreNext_ = true;
    }
    entries_.insert(entries_.begin(), std::move(e));
    Hide();
    // Wklejenie w oknie, które było aktywne przed Win+V
    if (previous_ && IsWindow(previous_)) {
        SetForegroundWindow(previous_);
        Sleep(60);
        INPUT in[4]{};
        for (INPUT& i : in) i.type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_CONTROL;
        in[1].ki.wVk = 'V';
        in[2].ki.wVk = 'V';
        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
        in[3].ki.wVk = VK_CONTROL;
        in[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, in, sizeof(INPUT));
    }
}
