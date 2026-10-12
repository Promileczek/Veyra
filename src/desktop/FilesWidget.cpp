#include "FilesWidget.h"
#include "../files/FileWindow.h"
#include <knownfolders.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>

namespace {
constexpr float kHeader = 40.f, kRadius = 18.f, kEdge = 6.f, kMinW = 220.f, kMinH = 180.f;

std::wstring StatePath() {
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    std::wstring dir = std::wstring(base, n) + L"\\Exploder";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\files-widget.txt";
}

bool In(const D2D1_RECT_F& r, D2D1_POINT_2F p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }
}  // namespace

void FilesWidget::Init(Graphics& g, HWND owner, UINT changedMessage, Callbacks callbacks) {
    g_ = &g;
    cb_ = std::move(callbacks);
    view_.Init(g, owner, changedMessage, cb_.redraw);
    view_.openInNewWindow = cb_.openWindow;
    IDWriteFactory* dw = g_->DWrite();
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         13.f, L"pl-PL", &titleFormat_);
    dw->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 13.f, L"pl-PL", &glyphFormat_);
    if (titleFormat_) {
        titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteInlineObject> ellipsis;
        dw->CreateEllipsisTrimmingSign(titleFormat_.Get(), &ellipsis);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        titleFormat_->SetTrimming(&trimming, ellipsis.Get());
    }
    if (glyphFormat_) {
        glyphFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        glyphFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    Load();
}

void FilesWidget::Load() {
    std::wstring folder;
    FILE* f = nullptr;
    if (!_wfopen_s(&f, StatePath().c_str(), L"r, ccs=UTF-8") && f) {
        int visible = 1, collapsed = 0;
        float x, y, w, h;
        wchar_t line[2048]{};
        if (fgetws(line, ARRAYSIZE(line), f) && swscanf_s(line, L"%d %d %f %f %f %f", &visible, &collapsed, &x, &y, &w, &h) == 6) {
            visible_ = visible != 0;
            collapsed_ = collapsed != 0;
            rect_ = {x, y, x + w, y + h};
            hasSaved_ = true;
        }
        if (fgetws(line, ARRAYSIZE(line), f)) {
            folder = line;
            while (!folder.empty() && (folder.back() == L'\n' || folder.back() == L'\r')) folder.pop_back();
        }
        fclose(f);
    }
    if (folder.empty() || !view_.NavigateParsing(folder)) view_.NavigateKnown(FOLDERID_Desktop);  // domyślnie: Pulpit
}

void FilesWidget::Save() const {
    FILE* f = nullptr;
    if (_wfopen_s(&f, StatePath().c_str(), L"w, ccs=UTF-8") || !f) return;
    fwprintf(f, L"%d %d %.0f %.0f %.0f %.0f\n%s\n", visible_ ? 1 : 0, collapsed_ ? 1 : 0, rect_.left, rect_.top,
             rect_.right - rect_.left, rect_.bottom - rect_.top, view_.FolderParsing().c_str());
    fclose(f);
}

void FilesWidget::SetDefaultRect(const D2D1_RECT_F& r) {
    if (hasSaved_) return;
    rect_ = r;
    UpdateViewBounds();
}

void FilesWidget::ClampTo(const D2D1_RECT_F& work) {
    const float w = std::clamp(rect_.right - rect_.left, kMinW, work.right - work.left);
    const float h = std::clamp(rect_.bottom - rect_.top, kMinH, work.bottom - work.top);
    const float x = std::clamp(rect_.left, work.left, work.right - w), y = std::clamp(rect_.top, work.top, work.bottom - h);
    rect_ = {x, y, x + w, y + h};
    UpdateViewBounds();
}

D2D1_RECT_F FilesWidget::Bounds() const {
    if (!visible_) return {};
    return collapsed_ ? D2D1_RECT_F{rect_.left, rect_.top, rect_.right, rect_.top + kHeader} : rect_;
}

void FilesWidget::Show() {
    if (visible_ && !collapsed_) return;
    visible_ = true;
    collapsed_ = false;
    Save();
    cb_.glassChanged();
}

void FilesWidget::SetFocused(bool f) {
    if (focused_ == f) return;
    if (!f) view_.CommitRename(true);
    focused_ = f;
    cb_.redraw();
}

void FilesWidget::UpdateViewBounds() { view_.SetBounds({rect_.left + 2.f, rect_.top + kHeader, rect_.right - 2.f, rect_.bottom - 8.f}); }

D2D1_RECT_F FilesWidget::BackButton() const { return {rect_.left + 78.f, rect_.top + 6.f, rect_.left + 106.f, rect_.top + kHeader - 6.f}; }

D2D1_RECT_F FilesWidget::NewFolderButton() const { return {rect_.right - 40.f, rect_.top + 6.f, rect_.right - 12.f, rect_.top + kHeader - 6.f}; }

bool FilesWidget::Contains(D2D1_POINT_2F p) const {
    const D2D1_RECT_F b = Bounds();
    return visible_ && p.x >= b.left - kEdge && p.x < b.right + kEdge && p.y >= b.top - kEdge && p.y < b.bottom + kEdge;
}

int FilesWidget::EdgeAt(D2D1_POINT_2F p) const {
    if (collapsed_) return EdgeNone;
    int e = EdgeNone;
    if (std::abs(p.x - rect_.left) < kEdge) e |= EdgeLeft;
    if (std::abs(p.x - rect_.right) < kEdge) e |= EdgeRight;
    if (std::abs(p.y - rect_.top) < kEdge) e |= EdgeTop;
    if (std::abs(p.y - rect_.bottom) < kEdge) e |= EdgeBottom;
    return e;
}

HCURSOR FilesWidget::CursorAt(D2D1_POINT_2F p) const {
    const int e = drag_ == Drag::Resize ? edges_ : EdgeAt(p);
    LPCWSTR id = IDC_ARROW;
    if ((e & EdgeLeft && e & EdgeTop) || (e & EdgeRight && e & EdgeBottom)) id = IDC_SIZENWSE;
    else if ((e & EdgeRight && e & EdgeTop) || (e & EdgeLeft && e & EdgeBottom)) id = IDC_SIZENESW;
    else if (e & (EdgeLeft | EdgeRight)) id = IDC_SIZEWE;
    else if (e & (EdgeTop | EdgeBottom)) id = IDC_SIZENS;
    return LoadCursorW(nullptr, id);
}

void FilesWidget::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float dpi) {
    if (!visible_) return;
    const D2D1_RECT_F b = Bounds();
    const auto shape = D2D1::RoundedRect(b, kRadius, kRadius);
    if (drag_ == Drag::Move || drag_ == Drag::Resize) {  // szkło liczy się po upuszczeniu — na czas przeciągania ciemne tło
        brush->SetColor(D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.92f));
        dc->FillRoundedRectangle(shape, brush);
    }
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, focused_ ? 0.30f : 0.22f));
    dc->DrawRoundedRectangle(shape, brush, 1.f);

    TrafficLights::Draw(dc, brush, LightsOrigin(), hoverLights_);
    if (view_.CanBack()) {
        if (hoverButton_ == 1) {
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(BackButton(), 6.f, 6.f), brush);
        }
        const wchar_t back = L'\xE72B';
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.9f));
        dc->DrawText(&back, 1, glyphFormat_.Get(), BackButton(), brush);
    }
    const std::wstring title = view_.FolderName();
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.95f));
    dc->DrawText(title.c_str(), static_cast<UINT32>(title.size()), titleFormat_.Get(),
                 {BackButton().right + 6.f, b.top, NewFolderButton().left - 4.f, b.top + kHeader}, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (hoverButton_ == 2) {
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
        dc->FillRoundedRectangle(D2D1::RoundedRect(NewFolderButton(), 6.f, 6.f), brush);
    }
    const wchar_t newFolder = L'\xE8F4';
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.9f));
    dc->DrawText(&newFolder, 1, glyphFormat_.Get(), NewFolderButton(), brush);
    if (collapsed_) return;
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
    dc->FillRectangle({b.left + 12.f, b.top + kHeader - 1.f, b.right - 12.f, b.top + kHeader}, brush);
    view_.Draw(dc, brush, dpi);
}

bool FilesWidget::OnMouseDown(D2D1_POINT_2F p, bool ctrl, bool shift) {
    if (!Contains(p)) return false;
    SetFocused(true);
    switch (TrafficLights::HitTest(LightsOrigin(), p)) {
    case 0:  // zamknij
        visible_ = false;
        focused_ = false;
        Save();
        cb_.glassChanged();
        return true;
    case 1:  // zwiń / rozwiń
        collapsed_ = !collapsed_;
        Save();
        cb_.glassChanged();
        return true;
    case 2:  // duże okno
        if (cb_.openWindow) cb_.openWindow(ComPtr<IShellItem>(view_.Folder()));
        return true;
    default: break;
    }
    if (view_.CanBack() && In(BackButton(), p)) return view_.Back(), Save(), true;
    if (In(NewFolderButton(), p)) {
        if (collapsed_) collapsed_ = false, cb_.glassChanged();
        view_.NewFolder();
        return true;
    }
    dragStart_ = p;
    startRect_ = rect_;
    if ((edges_ = EdgeAt(p)) != EdgeNone) drag_ = Drag::Resize;
    else if (p.y < rect_.top + kHeader) drag_ = Drag::Move;
    else {
        drag_ = Drag::View;
        view_.OnMouseDown(p, ctrl, shift);
    }
    cb_.redraw();
    return true;
}

void FilesWidget::OnMouseMove(D2D1_POINT_2F p) {
    const float dx = p.x - dragStart_.x, dy = p.y - dragStart_.y;
    if (drag_ == Drag::Move) {
        rect_ = {startRect_.left + dx, startRect_.top + dy, startRect_.right + dx, startRect_.bottom + dy};
        UpdateViewBounds();
        cb_.redraw();
        return;
    }
    if (drag_ == Drag::Resize) {
        D2D1_RECT_F r = startRect_;
        if (edges_ & EdgeLeft) r.left = std::min(startRect_.left + dx, r.right - kMinW);
        if (edges_ & EdgeRight) r.right = std::max(startRect_.right + dx, r.left + kMinW);
        if (edges_ & EdgeTop) r.top = std::min(startRect_.top + dy, r.bottom - kMinH);
        if (edges_ & EdgeBottom) r.bottom = std::max(startRect_.bottom + dy, r.top + kMinH);
        rect_ = r;
        UpdateViewBounds();
        cb_.redraw();
        return;
    }
    if (drag_ == Drag::View) return view_.OnMouseMove(p);

    const bool lights = TrafficLights::HitTest(LightsOrigin(), p) >= 0;
    const int button = view_.CanBack() && In(BackButton(), p) ? 1 : In(NewFolderButton(), p) ? 2 : 0;
    if (lights != hoverLights_ || button != hoverButton_) {
        hoverLights_ = lights;
        hoverButton_ = button;
        cb_.redraw();
    }
    if (!collapsed_) view_.OnMouseMove(p);
}

void FilesWidget::OnMouseUp(D2D1_POINT_2F p) {
    const Drag was = drag_;
    drag_ = Drag::None;
    if (was == Drag::View) view_.OnMouseUp(p);
    if (was == Drag::Move || was == Drag::Resize) {
        Save();
        cb_.glassChanged();
    }
    cb_.redraw();
}

void FilesWidget::OnMouseLeave() {
    hoverLights_ = false;
    hoverButton_ = 0;
    view_.OnMouseLeave();
    cb_.redraw();
}

void FilesWidget::OnDoubleClick(D2D1_POINT_2F p) {
    if (p.y < rect_.top + kHeader) {  // dwuklik w pasek tytułu = zwiń/rozwiń (jak w macOS)
        if (TrafficLights::HitTest(LightsOrigin(), p) < 0 && !In(BackButton(), p) && !In(NewFolderButton(), p)) {
            collapsed_ = !collapsed_;
            Save();
            cb_.glassChanged();
        }
        return;
    }
    if (!collapsed_) {
        view_.OnDoubleClick(p);
        Save();
    }
}

void FilesWidget::OnRightClick(D2D1_POINT_2F p, POINT screen) {
    SetFocused(true);
    if (!collapsed_ && p.y >= rect_.top + kHeader) view_.OnRightClick(p, screen);
}

void FilesWidget::OnWheel(int delta) {
    if (!collapsed_) view_.OnWheel(delta);
}

bool FilesWidget::OnKey(UINT vk, bool ctrl, bool shift, bool alt) {
    if (!focused_ || collapsed_) return false;
    const bool handled = view_.OnKey(vk, ctrl, shift, alt);
    if (handled) Save();
    return handled;
}

bool FilesWidget::OnChar(wchar_t ch) { return focused_ && !collapsed_ && view_.OnChar(ch); }
