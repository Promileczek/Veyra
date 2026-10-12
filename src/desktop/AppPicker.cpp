#include "AppPicker.h"
#include "DesktopItems.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace {
constexpr float kW = 380.f, kHeader = 44.f, kSearch = 40.f, kRow = 40.f, kFooter = 44.f, kPad = 10.f, kIcon = 26.f;
constexpr int kRows = 9;

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}
}  // namespace

D2D1_SIZE_F AppPicker::ContentSize() { return {kW, kHeader + kSearch + kRows * kRow + kFooter + kPad}; }

int AppPicker::VisibleRows() const { return kRows; }

void AppPicker::Open(const RECT& anchor, UINT dpi, std::vector<App> apps) {
    if (Visible()) return Hide();
    apps_ = std::move(apps);
    query_.clear();
    scroll_ = 0;
    hover_ = HoverNone;
    Filter();
    dpi_ = dpi;
    LoadVisibleIcons();
    Toggle(anchor, dpi);
}

void AppPicker::Filter() {
    view_.clear();
    const std::wstring q = Lower(query_);
    for (size_t i = 0; i < apps_.size(); ++i)
        if (q.empty() || apps_[i].lower.find(q) != std::wstring::npos) view_.push_back(i);
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(view_.size()) - kRows));
}

void AppPicker::LoadVisibleIcons() {
    const int px = static_cast<int>(std::ceil(kIcon * dpi_ / 96.f));
    for (int r = 0; r < kRows; ++r) {
        const size_t v = static_cast<size_t>(scroll_ + r);
        if (v >= view_.size()) break;
        App& a = apps_[view_[v]];
        if (a.iconLoaded) continue;
        a.iconLoaded = true;
        a.pendingIcon = LoadItemImage(a.item.Get(), px, g_->WIC());
    }
}

int AppPicker::RowAt(D2D1_POINT_2F p) const {
    const float top = kHeader + kSearch;
    if (p.x < kPad || p.x > kW - kPad) return HoverNone;
    if (p.y >= top + kRows * kRow + 4.f && p.y < top + kRows * kRow + kFooter) return HoverBrowse;
    if (p.y < top || p.y >= top + kRows * kRow) return HoverNone;
    const int v = scroll_ + static_cast<int>((p.y - top) / kRow);
    return v < static_cast<int>(view_.size()) ? v : HoverNone;
}

void AppPicker::Paint(ID2D1DeviceContext* dc) {
    Text(dc, L"Dodaj do panelu", titleFormat_.Get(), {16.f, 12.f, kW - 16.f, 36.f}, kText);

    // Wyszukiwarka (pisanie od razu trafia tutaj)
    const D2D1_RECT_F search{kPad, kHeader, kW - kPad, kHeader + kSearch - 8.f};
    Fill(dc, search, D2D1::ColorF(1.f, 1.f, 1.f, 0.08f), 9.f);
    Glyph(dc, L'\xE721', {search.left + 4.f, search.top, search.left + 32.f, search.bottom}, kDim);
    Text(dc, query_.empty() ? L"Szukaj aplikacji…" : query_ + L"|", textFormat_.Get(),
         {search.left + 34.f, search.top + 7.f, search.right - 8.f, search.bottom}, query_.empty() ? kDim : kText);

    const float top = kHeader + kSearch;
    if (view_.empty()) Text(dc, L"Brak wyników", textFormat_.Get(), {kPad + 12.f, top + 10.f, kW - kPad, top + 34.f}, kDim);
    for (int r = 0; r < kRows; ++r) {
        const size_t v = static_cast<size_t>(scroll_ + r);
        if (v >= view_.size()) break;
        App& a = apps_[view_[v]];
        const D2D1_RECT_F row{kPad, top + r * kRow, kW - kPad, top + (r + 1) * kRow};
        if (hover_ == static_cast<int>(v) && !a.pinned) Fill(dc, row, kHover, 8.f);
        if (a.pendingIcon) {  // tylko Direct2D — bez wywołań powłoki w trakcie rysowania
            dc->CreateBitmapFromWicBitmap(a.pendingIcon.Get(), nullptr, &a.icon);
            a.pendingIcon.Reset();
        }
        const float iy = (row.top + row.bottom - kIcon) / 2;
        if (a.icon) {
            const D2D1_RECT_F ir{row.left + 8.f, iy, row.left + 8.f + kIcon, iy + kIcon};
            dc->DrawBitmap(a.icon.Get(), &ir, a.pinned ? 0.4f : 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
        Text(dc, a.name, textFormat_.Get(), {row.left + 44.f, row.top + 11.f, row.right - 70.f, row.bottom}, a.pinned ? kDim : kText);
        if (a.pinned) Text(dc, L"w panelu", smallFormat_.Get(), {row.right - 64.f, row.top + 13.f, row.right - 6.f, row.bottom}, kDim);
    }
    // Pasek przewijania
    if (static_cast<int>(view_.size()) > kRows) {
        const float h = kRows * kRow, thumb = std::max(24.f, h * kRows / view_.size());
        const float y = top + (h - thumb) * scroll_ / (view_.size() - kRows);
        Fill(dc, {kW - 6.f, y, kW - 3.f, y + thumb}, D2D1::ColorF(1.f, 1.f, 1.f, 0.25f), 1.5f);
    }

    const D2D1_RECT_F browse{kPad, top + kRows * kRow + 4.f, kW - kPad, top + kRows * kRow + kFooter};
    Fill(dc, {kPad, browse.top - 2.f, kW - kPad, browse.top - 1.f}, D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
    if (hover_ == HoverBrowse) Fill(dc, browse, kHover, 8.f);
    Glyph(dc, L'\xE8E5', {browse.left + 6.f, browse.top, browse.left + 40.f, browse.bottom}, kText);
    Text(dc, L"Inny plik lub program…", textFormat_.Get(), {browse.left + 44.f, browse.top + 12.f, browse.right, browse.bottom}, kText);
}

void AppPicker::OnMouseMove(D2D1_POINT_2F p) {
    const int h = RowAt(p);
    if (h != hover_) {
        hover_ = h;
        Redraw();
    }
}

void AppPicker::OnMouseDown(D2D1_POINT_2F p) {
    const int h = RowAt(p);
    if (h == HoverBrowse) {
        Hide();
        if (onBrowse) onBrowse();
        return;
    }
    if (h < 0) return;
    App& a = apps_[view_[static_cast<size_t>(h)]];
    if (a.pinned) return;
    a.pinned = true;  // można dodać kilka po kolei — okienko zostaje otwarte
    if (onPick) onPick(a.item);
    Redraw();
}

void AppPicker::OnWheel(int delta) {
    const int maxScroll = std::max(0, static_cast<int>(view_.size()) - kRows);
    const int next = std::clamp(scroll_ + (delta > 0 ? -3 : 3), 0, maxScroll);
    if (next == scroll_) return;
    scroll_ = next;
    LoadVisibleIcons();
    Redraw();
}

void AppPicker::OnChar(wchar_t ch) {
    if (ch == L'\b') {
        if (query_.empty()) return;
        query_.pop_back();
    } else if (ch >= L' ') {
        query_ += ch;
    } else {
        return;
    }
    scroll_ = 0;
    hover_ = HoverNone;
    Filter();
    LoadVisibleIcons();
    Redraw();
}
