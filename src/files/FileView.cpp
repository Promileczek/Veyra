#include "FileView.h"
#include "../core/FolderIcon.h"
#include "../desktop/DesktopItems.h"
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kCellW = 88.f, kCellH = 98.f, kIcon = 44.f, kPad = 10.f, kLabelTop = 56.f, kLabelH = 36.f;
constexpr int kIconsPerFrame = 10;  // ikony ładujemy porcjami, żeby duży folder nie zamrażał okna
constexpr wchar_t kRecycleBin[] = L"::{645FF040-5081-101B-9F08-00AA002F954E}";

enum BgCmd { CmdNewFolder = 1, CmdPaste, CmdRefresh, CmdNewWindow, CmdProperties, CmdEmptyBin };

std::wstring Name(IShellItem* item, SIGDN type) {
    PWSTR s = nullptr;
    std::wstring out;
    if (item && SUCCEEDED(item->GetDisplayName(type, &s))) {
        out = s;
        CoTaskMemFree(s);
    }
    return out;
}

bool Intersects(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
}  // namespace

FileView::~FileView() { StopWatcher(); }

void FileView::Init(Graphics& g, HWND owner, UINT changedMessage, std::function<void()> requestRedraw) {
    g_ = &g;
    owner_ = owner;
    changedMessage_ = changedMessage;
    requestRedraw_ = std::move(requestRedraw);
    IDWriteFactory* dw = g_->DWrite();
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         11.5f, L"pl-PL", &labelFormat_);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         13.f, L"pl-PL", &emptyFormat_);
    if (!labelFormat_ || !emptyFormat_) return;
    labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    labelFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
    dw->CreateEllipsisTrimmingSign(labelFormat_.Get(), &ellipsis_);
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    labelFormat_->SetTrimming(&trimming, ellipsis_.Get());
    emptyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    emptyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ---------------- Nawigacja ----------------

bool FileView::Navigate(ComPtr<IShellItem> folder, bool addToHistory) {
    if (!folder) return false;
    CommitRename(true);
    folder_ = std::move(folder);
    folderName_ = Name(folder_.Get(), SIGDN_NORMALDISPLAY);
    if (addToHistory) {
        if (!history_.empty()) history_.resize(historyPos_ + 1);
        history_.push_back(folder_);
        historyPos_ = history_.size() - 1;
    }
    items_.clear();
    scroll_ = 0.f;
    hover_ = anchor_ = -1;
    Load();
    StartWatcher();
    requestRedraw_();
    return true;
}

bool FileView::NavigateKnown(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;  // przez ścieżkę: SHGetKnownFolderItem(Desktop) dałby wirtualny korzeń powłoki
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) {
        const bool ok = NavigateParsing(path);
        CoTaskMemFree(path);
        if (ok) return true;
    }
    ComPtr<IShellItem> item;
    return SUCCEEDED(SHGetKnownFolderItem(id, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&item))) && Navigate(item);
}

bool FileView::NavigateParsing(const std::wstring& parsing) {
    ComPtr<IShellItem> item;
    return SUCCEEDED(SHCreateItemFromParsingName(parsing.c_str(), nullptr, IID_PPV_ARGS(&item))) && Navigate(item);
}

std::wstring FileView::FolderParsing() const { return Name(folder_.Get(), SIGDN_DESKTOPABSOLUTEPARSING); }

void FileView::Back() {
    if (!CanBack()) return;
    --historyPos_;
    Navigate(history_[historyPos_], false);
}

void FileView::Forward() {
    if (!CanForward()) return;
    ++historyPos_;
    Navigate(history_[historyPos_], false);
}

bool FileView::CanUp() const {
    ComPtr<IShellItem> parent;
    return folder_ && SUCCEEDED(folder_->GetParent(&parent));
}

void FileView::Up() {
    ComPtr<IShellItem> parent;
    if (folder_ && SUCCEEDED(folder_->GetParent(&parent))) Navigate(parent);
}

void FileView::Load() {
    std::vector<FileItem> fresh;
    ComPtr<IEnumShellItems> en;
    if (folder_ && SUCCEEDED(folder_->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en)))) {
        ComPtr<IShellItem> child;
        while (en->Next(1, &child, nullptr) == S_OK) {
            SFGAOF attrs = 0;
            child->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM | SFGAO_HIDDEN, &attrs);
            if (!(attrs & SFGAO_HIDDEN)) {
                FileItem f;
                f.name = Name(child.Get(), SIGDN_NORMALDISPLAY);
                f.parsing = Name(child.Get(), SIGDN_DESKTOPABSOLUTEPARSING);
                f.folder = (attrs & SFGAO_FOLDER) && !(attrs & SFGAO_STREAM);  // .zip to plik
                f.item = child;
                fresh.push_back(std::move(f));
            }
            child.Reset();
        }
    }
    std::sort(fresh.begin(), fresh.end(), [](const FileItem& a, const FileItem& b) {
        if (a.folder != b.folder) return a.folder;  // najpierw foldery
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
    });
    // Zachowujemy ikony i zaznaczenie istniejących elementów
    for (FileItem& f : fresh)
        for (const FileItem& o : items_)
            if (o.parsing == f.parsing) {
                f.icon = o.icon;
                f.iconLoaded = o.iconLoaded;
                f.selected = o.selected;
                if (o.name == f.name) f.label = o.label;
                break;
            }
    items_ = std::move(fresh);
    hover_ = -1;
    ClampScroll();
}

void FileView::Reload() {
    if (renaming_ >= 0) return;  // nie przerywamy wpisywania nazwy
    Load();
    requestRedraw_();
}

void FileView::StartWatcher() {
    StopWatcher();
    PWSTR path = nullptr;
    if (!folder_ || FAILED(folder_->GetDisplayName(SIGDN_FILESYSPATH, &path))) return;  // tylko foldery na dysku
    const std::wstring dir = path;
    CoTaskMemFree(path);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return;
    watcher_ = std::thread([dir, owner = owner_, msg = changedMessage_, self = this, stop = stopEvent_] {
        const HANDLE change = FindFirstChangeNotificationW(
            dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE);
        if (change == INVALID_HANDLE_VALUE) return;
        const HANDLE handles[] = {stop, change};
        while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            PostMessageW(owner, msg, 0, reinterpret_cast<LPARAM>(self));
            FindNextChangeNotification(change);
        }
        FindCloseChangeNotification(change);
    });
}

void FileView::StopWatcher() {
    if (stopEvent_) SetEvent(stopEvent_);
    if (watcher_.joinable()) watcher_.join();
    if (stopEvent_) CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

// ---------------- Układ ----------------

void FileView::SetBounds(const D2D1_RECT_F& r) {
    bounds_ = r;
    ClampScroll();
}

int FileView::Columns() const { return std::max(1, static_cast<int>((bounds_.right - bounds_.left - 2 * kPad) / kCellW)); }
// Siatka na środku — ten sam margines z lewej i z prawej
float FileView::GridLeft() const { return bounds_.left + std::max(kPad, (bounds_.right - bounds_.left - Columns() * kCellW) / 2.f); }

D2D1_RECT_F FileView::CellRect(size_t index) const {
    const int cols = Columns();
    const float x = GridLeft() + static_cast<float>(index % cols) * kCellW;
    const float y = bounds_.top + kPad + static_cast<float>(index / cols) * kCellH - scroll_;
    return {x, y, x + kCellW, y + kCellH};
}

float FileView::ContentHeight() const {
    const size_t rows = (items_.size() + Columns() - 1) / Columns();
    return 2 * kPad + rows * kCellH;
}

void FileView::ClampScroll() {
    const float maxScroll = std::max(0.f, ContentHeight() - (bounds_.bottom - bounds_.top));
    scroll_ = std::clamp(scroll_, 0.f, maxScroll);
}

int FileView::ItemAt(D2D1_POINT_2F p) const {
    if (p.x < bounds_.left || p.x >= bounds_.right || p.y < bounds_.top || p.y >= bounds_.bottom) return -1;
    const int cols = Columns();
    const int col = static_cast<int>((p.x - GridLeft()) / kCellW);
    const int row = static_cast<int>((p.y - bounds_.top - kPad + scroll_) / kCellH);
    if (col < 0 || col >= cols || row < 0 || p.x < GridLeft() || p.y < bounds_.top + kPad - scroll_) return -1;
    const size_t index = static_cast<size_t>(row) * cols + col;
    if (index >= items_.size()) return -1;
    const D2D1_RECT_F r = CellRect(index);
    return (p.x >= r.left + 3.f && p.x < r.right - 3.f && p.y >= r.top + 2.f && p.y < r.bottom - 2.f) ? static_cast<int>(index) : -1;
}

void FileView::EnsureVisible(size_t index) {
    const D2D1_RECT_F r = CellRect(index);
    if (r.top < bounds_.top) scroll_ -= bounds_.top - r.top + kPad;
    else if (r.bottom > bounds_.bottom) scroll_ += r.bottom - bounds_.bottom + kPad;
    ClampScroll();
}

// ---------------- Rysowanie ----------------

void FileView::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float dpi) {
    if (!labelFormat_) return;
    dc->PushAxisAlignedClip(bounds_, D2D1_ANTIALIAS_MODE_ALIASED);
    if (items_.empty()) {
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.45f));
        const std::wstring empty = folder_ ? L"Ten folder jest pusty" : L"";
        dc->DrawText(empty.c_str(), static_cast<UINT32>(empty.size()), emptyFormat_.Get(), bounds_, brush);
    }
    (void)dpi;
    bool pending = false;
    for (size_t i = 0; i < items_.size(); ++i) {
        const D2D1_RECT_F cell = CellRect(i);
        if (cell.bottom < bounds_.top || cell.top > bounds_.bottom) continue;
        FileItem& f = items_[i];
        const D2D1_RECT_F r{cell.left + 3.f, cell.top + 2.f, cell.right - 3.f, cell.bottom - 2.f};
        if (f.selected) {
            brush->SetColor(D2D1::ColorF(0.40f, 0.62f, 1.f, 0.30f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(r, 8.f, 8.f), brush);
        } else if (hover_ == static_cast<int>(i)) {
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.09f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(r, 8.f, 8.f), brush);
        }
        if (f.pendingIcon) {  // bez wywołań powłoki — tylko zamiana gotowego obrazu na bitmapę
            dc->CreateBitmapFromWicBitmap(f.pendingIcon.Get(), nullptr, &f.icon);
            f.pendingIcon.Reset();
        }
        const float cx = (r.left + r.right) / 2;
        if (f.folder) {  // folder: własna ikona Veyry (bez wczytywania ikony z powłoki)
            DrawVeyraFolder(dc, brush, {cx - kIcon / 2, r.top + 6.f, cx + kIcon / 2, r.top + 6.f + kIcon});
        } else {
            if (!f.iconLoaded) pending = true;
            if (f.icon) {
                const D2D1_SIZE_F sz = f.icon->GetSize();
                const float k = std::min(kIcon / sz.width, kIcon / sz.height);
                const float w = sz.width * k, h = sz.height * k;
                const D2D1_RECT_F dst{cx - w / 2, r.top + 6.f + (kIcon - h), cx + w / 2, r.top + 6.f + kIcon};
                dc->DrawBitmap(f.icon.Get(), &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            }
        }
        if (renaming_ == static_cast<int>(i)) {  // pole zmiany nazwy
            const D2D1_RECT_F box{r.left - 2.f, r.top + kLabelTop - 2.f, r.right + 2.f, r.top + kLabelTop + 20.f};
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.95f));
            dc->FillRoundedRectangle(D2D1::RoundedRect(box, 4.f, 4.f), brush);
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(g_->DWrite()->CreateTextLayout(editText_.c_str(), static_cast<UINT32>(editText_.size()), labelFormat_.Get(),
                                                         box.right - box.left - 4.f, 20.f, &layout))) {
                layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                brush->SetColor(D2D1::ColorF(0.08f, 0.08f, 0.1f, 1.f));
                dc->DrawTextLayout({box.left + 2.f, box.top + 1.f}, layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                DWRITE_HIT_TEST_METRICS m{};
                float cxp = 0.f, cyp = 0.f;
                layout->HitTestTextPosition(static_cast<UINT32>(caret_), FALSE, &cxp, &cyp, &m);
                dc->DrawLine({box.left + 2.f + cxp, box.top + 3.f}, {box.left + 2.f + cxp, box.bottom - 3.f}, brush, 1.f);
            }
            continue;
        }
        if (!f.label)
            g_->DWrite()->CreateTextLayout(f.name.c_str(), static_cast<UINT32>(f.name.size()), labelFormat_.Get(), kCellW - 8.f,
                                           kLabelH, &f.label);
        if (f.label) {
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.95f));
            dc->DrawTextLayout({r.left + 1.f, r.top + kLabelTop}, f.label.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    if (mode_ == Mode::Selecting) {
        const D2D1_RECT_F band{std::min(downPt_.x, curPt_.x), std::min(downPt_.y, curPt_.y), std::max(downPt_.x, curPt_.x),
                               std::max(downPt_.y, curPt_.y)};
        brush->SetColor(D2D1::ColorF(0.4f, 0.6f, 1.f, 0.18f));
        dc->FillRectangle(band, brush);
        brush->SetColor(D2D1::ColorF(0.5f, 0.7f, 1.f, 0.8f));
        dc->DrawRectangle(band, brush, 1.f);
    }
    // pasek przewijania
    const float viewH = bounds_.bottom - bounds_.top, contentH = ContentHeight();
    if (contentH > viewH + 1.f) {
        const float thumbH = std::max(24.f, viewH * viewH / contentH);
        const float y = bounds_.top + (viewH - thumbH) * scroll_ / (contentH - viewH);
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.25f));
        dc->FillRoundedRectangle(D2D1::RoundedRect({bounds_.right - 6.f, y, bounds_.right - 3.f, y + thumbH}, 1.5f, 1.5f), brush);
    }
    dc->PopAxisAlignedClip();
    if (pending) requestRedraw_();  // dociągnij resztę ikon w następnej klatce
}

void FileView::PrepareIcons(float dpi) {
    const int iconPx = static_cast<int>(std::ceil(kIcon * dpi / 96.f));
    int loaded = 0;
    for (size_t i = 0; i < items_.size() && loaded < kIconsPerFrame; ++i) {
        const D2D1_RECT_F cell = CellRect(i);
        if (cell.bottom < bounds_.top || cell.top > bounds_.bottom || items_[i].iconLoaded) continue;
        if (items_[i].folder) { items_[i].iconLoaded = true; continue; }  // folder rysujemy sami (ikona Veyry)
        items_[i].iconLoaded = true;
        ++loaded;
        const std::wstring parsing = items_[i].parsing;
        const ComPtr<IShellItem> item = items_[i].item;
        ComPtr<IWICBitmapSource> src = LoadItemImage(item.Get(), iconPx, g_->WIC());
        // W trakcie wczytywania lista mogła się zmienić (odświeżenie folderu) — sprawdzamy, czy to wciąż ten sam plik
        if (i < items_.size() && items_[i].parsing == parsing) items_[i].pendingIcon = std::move(src);
    }
}

// ---------------- Zaznaczanie i mysz ----------------

std::vector<size_t> FileView::Selected() const {
    std::vector<size_t> out;
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].selected) out.push_back(i);
    return out;
}

void FileView::SelectOnly(int index) {
    for (size_t i = 0; i < items_.size(); ++i) items_[i].selected = static_cast<int>(i) == index;
    anchor_ = index;
}

void FileView::OnMouseDown(D2D1_POINT_2F p, bool ctrl, bool shift) {
    CommitRename(true);
    downPt_ = curPt_ = p;
    downItem_ = ItemAt(p);
    if (downItem_ >= 0) {
        FileItem& f = items_[static_cast<size_t>(downItem_)];
        if (shift && anchor_ >= 0) {
            const int a = std::min(anchor_, downItem_), b = std::max(anchor_, downItem_);
            for (size_t i = 0; i < items_.size(); ++i)
                items_[i].selected = (ctrl && items_[i].selected) || (static_cast<int>(i) >= a && static_cast<int>(i) <= b);
        } else if (ctrl) {
            f.selected = !f.selected;
            anchor_ = downItem_;
        } else if (!f.selected) {
            SelectOnly(downItem_);
        }
        mode_ = Mode::Pending;
    } else {
        if (!ctrl) SelectOnly(-1);
        mode_ = Mode::Selecting;
    }
    requestRedraw_();
}

void FileView::OnMouseMove(D2D1_POINT_2F p) {
    curPt_ = p;
    if (mode_ == Mode::Selecting) {
        const D2D1_RECT_F band{std::min(downPt_.x, p.x), std::min(downPt_.y, p.y), std::max(downPt_.x, p.x), std::max(downPt_.y, p.y)};
        for (size_t i = 0; i < items_.size(); ++i) {
            const D2D1_RECT_F c = CellRect(i);
            items_[i].selected = Intersects(band, {c.left + 8.f, c.top + 4.f, c.right - 8.f, c.bottom - 4.f});
        }
        requestRedraw_();
        return;
    }
    const int hit = ItemAt(p);
    if (hit != hover_) {
        hover_ = hit;
        requestRedraw_();
    }
}

void FileView::OnMouseUp(D2D1_POINT_2F) {
    if (mode_ == Mode::Pending && downItem_ >= 0 && !(GetKeyState(VK_CONTROL) < 0) && !(GetKeyState(VK_SHIFT) < 0))
        SelectOnly(downItem_);
    mode_ = Mode::None;
    requestRedraw_();
}

void FileView::OnMouseLeave() {
    if (hover_ != -1) {
        hover_ = -1;
        requestRedraw_();
    }
}

void FileView::OnDoubleClick(D2D1_POINT_2F p) {
    const int hit = ItemAt(p);
    if (hit >= 0) OpenItem(static_cast<size_t>(hit));
}

void FileView::OnWheel(int delta) {
    scroll_ -= delta / 120.f * 60.f;
    ClampScroll();
    hover_ = -1;
    requestRedraw_();
}

// ---------------- Akcje ----------------

void FileView::OpenItem(size_t index) {
    FileItem& f = items_[index];
    if (f.folder) {
        Navigate(f.item);
        return;
    }
    if (pickFile) {
        pickFile(f.item);
        return;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetIDListFromObject(f.item.Get(), &pidl))) return;
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.hwnd = owner_;
    sei.lpIDList = pidl;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
    CoTaskMemFree(pidl);
}

void FileView::OpenSelected() {
    const auto sel = Selected();
    if (sel.size() == 1) return OpenItem(sel[0]);
    for (size_t i : sel)
        if (!items_[i].folder) OpenItem(i);  // wiele zaznaczonych: otwieramy tylko pliki
}

void FileView::DeleteSelected(bool permanent) {
    ComPtr<IFileOperation> op;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))) return;
    op->SetOwnerWindow(owner_);
    op->SetOperationFlags(permanent ? FOF_NOCONFIRMMKDIR : (FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE));
    bool any = false;
    for (size_t i : Selected()) any |= SUCCEEDED(op->DeleteItem(items_[i].item.Get(), nullptr));
    if (any) op->PerformOperations();
    Reload();
}

void FileView::CopySelected(bool cut) {
    const auto sel = Selected();
    if (sel.empty() || !folder_) return;
    std::vector<PIDLIST_ABSOLUTE> pidls;
    for (size_t i : sel) {
        PIDLIST_ABSOLUTE p = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(items_[i].item.Get(), &p))) pidls.push_back(p);
    }
    ComPtr<IShellItemArray> array;
    if (!pidls.empty() &&
        SUCCEEDED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(pidls.size()), const_cast<PCIDLIST_ABSOLUTE_ARRAY>(pidls.data()), &array))) {
        ComPtr<IDataObject> data;
        if (SUCCEEDED(array->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data)))) {
            if (cut) {  // "wytnij" = dane z flagą przeniesienia
                FORMATETC fmt{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)), nullptr, DVASPECT_CONTENT,
                              -1, TYMED_HGLOBAL};
                STGMEDIUM medium{};
                medium.tymed = TYMED_HGLOBAL;
                medium.hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
                if (medium.hGlobal) {
                    *static_cast<DWORD*>(GlobalLock(medium.hGlobal)) = DROPEFFECT_MOVE;
                    GlobalUnlock(medium.hGlobal);
                    if (FAILED(data->SetData(&fmt, &medium, TRUE))) GlobalFree(medium.hGlobal);
                }
            }
            OleSetClipboard(data.Get());
        }
    }
    for (PIDLIST_ABSOLUTE p : pidls) CoTaskMemFree(p);
}

void FileView::Paste() {
    ComPtr<IDataObject> data;
    if (!folder_ || FAILED(OleGetClipboard(&data))) return;
    DWORD effect = DROPEFFECT_COPY;
    FORMATETC fmt{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)), nullptr, DVASPECT_CONTENT, -1,
                  TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (SUCCEEDED(data->GetData(&fmt, &medium))) {
        if (const auto* p = static_cast<const DWORD*>(GlobalLock(medium.hGlobal))) {
            effect = *p;
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    }
    ComPtr<IFileOperation> op;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))) return;
    op->SetOwnerWindow(owner_);
    op->SetOperationFlags(FOF_ALLOWUNDO | FOF_RENAMEONCOLLISION | FOF_NOCONFIRMMKDIR);
    const HRESULT hr = (effect & DROPEFFECT_MOVE) ? op->MoveItems(data.Get(), folder_.Get()) : op->CopyItems(data.Get(), folder_.Get());
    if (SUCCEEDED(hr)) op->PerformOperations();
    if (effect & DROPEFFECT_MOVE) OleSetClipboard(nullptr);  // wycięte pliki można wkleić tylko raz
    Reload();
}

void FileView::NewFolder() {
    PWSTR path = nullptr;
    if (!folder_ || FAILED(folder_->GetDisplayName(SIGDN_FILESYSPATH, &path))) return;
    const std::wstring dir = path;
    CoTaskMemFree(path);
    std::wstring name = L"Nowy folder";
    for (int n = 2; GetFileAttributesW((dir + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES && n < 1000; ++n)
        name = L"Nowy folder (" + std::to_wstring(n) + L")";
    const std::wstring full = dir + L"\\" + name;
    if (!CreateDirectoryW(full.c_str(), nullptr)) return;
    Load();
    for (size_t i = 0; i < items_.size(); ++i)
        if (_wcsicmp(items_[i].parsing.c_str(), full.c_str()) == 0) {
            SelectOnly(static_cast<int>(i));
            EnsureVisible(i);
            StartRename(i);  // od razu wpisz nazwę
            break;
        }
    requestRedraw_();
}

void FileView::StartRename(size_t index) {
    if (index >= items_.size()) return;
    renaming_ = static_cast<int>(index);
    editText_ = Name(items_[index].item.Get(), SIGDN_PARENTRELATIVEEDITING);
    caret_ = editText_.size();
    requestRedraw_();
}

void FileView::CommitRename(bool accept) {
    if (renaming_ < 0) return;
    const size_t index = static_cast<size_t>(renaming_);
    renaming_ = -1;
    if (accept && index < items_.size() && !editText_.empty() &&
        editText_ != Name(items_[index].item.Get(), SIGDN_PARENTRELATIVEEDITING)) {
        // SetNameOf zachowuje ukryte rozszerzenie (tak zmienia nazwy explorer)
        PIDLIST_ABSOLUTE abs = nullptr;
        ComPtr<IShellFolder> parent;
        PCUITEMID_CHILD child = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(items_[index].item.Get(), &abs))) {
            if (SUCCEEDED(SHBindToParent(abs, IID_PPV_ARGS(&parent), &child))) {
                PITEMID_CHILD renamed = nullptr;
                if (SUCCEEDED(parent->SetNameOf(owner_, child, editText_.c_str(), SHGDN_INFOLDER | SHGDN_FOREDITING, &renamed)))
                    CoTaskMemFree(renamed);
            }
            CoTaskMemFree(abs);
        }
        Load();
    }
    requestRedraw_();
}

bool FileView::OnChar(wchar_t ch) {
    if (renaming_ < 0) return false;
    if (ch == L'\r') CommitRename(true);
    else if (ch == 0x1B) CommitRename(false);
    else if (ch == L'\b') {
        if (caret_ > 0) editText_.erase(--caret_, 1);
    } else if (ch >= L' ' && !wcschr(L"\\/:*?\"<>|", ch)) {  // znaki niedozwolone w nazwach plików
        editText_.insert(caret_++, 1, ch);
    }
    requestRedraw_();
    return true;
}

bool FileView::OnKey(UINT vk, bool ctrl, bool shift, bool alt) {
    if (renaming_ >= 0) {
        if (vk == VK_LEFT && caret_ > 0) --caret_;
        else if (vk == VK_RIGHT && caret_ < editText_.size()) ++caret_;
        else if (vk == VK_HOME) caret_ = 0;
        else if (vk == VK_END) caret_ = editText_.size();
        else if (vk == VK_DELETE && caret_ < editText_.size()) editText_.erase(caret_, 1);
        else return true;
        requestRedraw_();
        return true;
    }
    if (alt && vk == VK_LEFT) return Back(), true;
    if (alt && vk == VK_RIGHT) return Forward(), true;
    if (alt && vk == VK_UP) return Up(), true;
    switch (vk) {
    case VK_RETURN: OpenSelected(); return true;
    case VK_BACK: Back(); return true;
    case VK_DELETE: DeleteSelected(shift); return true;
    case VK_F5: Reload(); return true;
    case VK_F2:
        if (const auto sel = Selected(); !sel.empty()) StartRename(sel[0]);
        return true;
    }
    if (ctrl) {
        switch (vk) {
        case 'A':
            for (FileItem& f : items_) f.selected = true;
            requestRedraw_();
            return true;
        case 'C': CopySelected(false); return true;
        case 'X': CopySelected(true); return true;
        case 'V': Paste(); return true;
        case 'N':
            if (shift) NewFolder();
            return true;
        }
    }
    return false;
}

// ---------------- Menu kontekstowe ----------------

void FileView::OnRightClick(D2D1_POINT_2F p, POINT screen) {
    CommitRename(true);
    const int hit = ItemAt(p);
    if (hit >= 0) {
        if (!items_[static_cast<size_t>(hit)].selected) SelectOnly(hit);
        requestRedraw_();
        ShowItemMenu(static_cast<size_t>(hit), screen);
    } else {
        SelectOnly(-1);
        requestRedraw_();
        ShowBackgroundMenu(screen);
    }
}

bool FileView::HandleMenuMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    if (!activeMenu_ || (msg != WM_INITMENUPOPUP && msg != WM_DRAWITEM && msg != WM_MEASUREITEM)) return false;
    activeMenu_->HandleMenuMsg(msg, wp, lp);
    result = msg == WM_INITMENUPOPUP ? 0 : TRUE;
    return true;
}

void FileView::ShowItemMenu(size_t index, POINT pt) {
    // Menu Windows dla zaznaczonych elementów (wszystkie leżą w bieżącym folderze)
    PIDLIST_ABSOLUTE folderPidl = nullptr;
    ComPtr<IShellFolder> shellFolder;
    if (FAILED(SHGetIDListFromObject(folder_.Get(), &folderPidl))) return;
    if (FAILED(SHBindToObject(nullptr, folderPidl, nullptr, IID_PPV_ARGS(&shellFolder)))) {
        CoTaskMemFree(folderPidl);
        return;
    }
    std::vector<PIDLIST_ABSOLUTE> owned;
    std::vector<PCUITEMID_CHILD> children;
    std::vector<size_t> sel = Selected();
    if (std::find(sel.begin(), sel.end(), index) == sel.end()) sel = {index};
    for (size_t i : sel) {
        PIDLIST_ABSOLUTE abs = nullptr;
        if (FAILED(SHGetIDListFromObject(items_[i].item.Get(), &abs))) continue;
        owned.push_back(abs);
        children.push_back(ILFindLastID(abs));
    }
    ComPtr<IContextMenu> menu;
    if (!children.empty() &&
        SUCCEEDED(shellFolder->GetUIObjectOf(owner_, static_cast<UINT>(children.size()), children.data(), IID_IContextMenu, nullptr,
                                             reinterpret_cast<void**>(menu.GetAddressOf())))) {
        HMENU popup = CreatePopupMenu();
        constexpr UINT kFirst = 1, kLast = 0x7FFF;
        const UINT flags = CMF_NORMAL | CMF_CANRENAME | (GetKeyState(VK_SHIFT) < 0 ? CMF_EXTENDEDVERBS : 0);
        if (SUCCEEDED(menu->QueryContextMenu(popup, 0, kFirst, kLast, flags))) {
            ComPtr<IContextMenu2> menu2;
            menu.As(&menu2);
            activeMenu_ = menu2.Get();
            const UINT cmd = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, owner_, nullptr);
            activeMenu_ = nullptr;
            if (cmd >= kFirst) {
                wchar_t verb[64]{};
                const bool hasVerb = SUCCEEDED(menu->GetCommandString(cmd - kFirst, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb),
                                                                      ARRAYSIZE(verb)));
                if (hasVerb && _wcsicmp(verb, L"rename") == 0) {
                    StartRename(index);  // "Zmień nazwę" wymaga widoku explorera — robimy ją sami
                } else if (hasVerb && _wcsicmp(verb, L"open") == 0 && items_[index].folder) {
                    Navigate(items_[index].item);  // folder otwieramy u siebie, nie w explorerze
                } else {
                    CMINVOKECOMMANDINFOEX ici{sizeof(ici)};
                    ici.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                    ici.hwnd = owner_;
                    ici.lpVerb = MAKEINTRESOURCEA(cmd - kFirst);
                    ici.lpVerbW = MAKEINTRESOURCEW(cmd - kFirst);
                    ici.nShow = SW_SHOWNORMAL;
                    ici.ptInvoke = pt;
                    menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&ici));
                    Reload();
                }
            }
        }
        DestroyMenu(popup);
    }
    for (PIDLIST_ABSOLUTE p : owned) CoTaskMemFree(p);
    CoTaskMemFree(folderPidl);
}

void FileView::ShowBackgroundMenu(POINT pt) {
    const bool fileSystem = [&] {
        PWSTR p = nullptr;
        const bool ok = folder_ && SUCCEEDED(folder_->GetDisplayName(SIGDN_FILESYSPATH, &p));
        CoTaskMemFree(p);
        return ok;
    }();
    const bool recycleBin = _wcsicmp(FolderParsing().c_str(), kRecycleBin) == 0;
    const bool canPaste = IsClipboardFormatAvailable(CF_HDROP) != FALSE;

    HMENU menu = CreatePopupMenu();
    if (fileSystem) {
        AppendMenuW(menu, MF_STRING, CmdNewFolder, L"Nowy folder\tCtrl+Shift+N");
        AppendMenuW(menu, MF_STRING | (canPaste ? 0 : MF_GRAYED), CmdPaste, L"Wklej\tCtrl+V");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    if (recycleBin) {
        AppendMenuW(menu, MF_STRING | (items_.empty() ? MF_GRAYED : 0), CmdEmptyBin, L"Opróżnij kosz");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, CmdRefresh, L"Odśwież\tF5");
    if (openInNewWindow) AppendMenuW(menu, MF_STRING, CmdNewWindow, L"Otwórz w nowym oknie");
    AppendMenuW(menu, MF_STRING, CmdProperties, L"Właściwości");
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner_, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case CmdNewFolder: NewFolder(); break;
    case CmdPaste: Paste(); break;
    case CmdRefresh:
        for (FileItem& f : items_) f.iconLoaded = false;  // też nowe miniatury
        Reload();
        break;
    case CmdNewWindow:
        if (openInNewWindow) openInNewWindow(folder_);
        break;
    case CmdEmptyBin:
        SHEmptyRecycleBinW(owner_, nullptr, 0);
        Reload();
        break;
    case CmdProperties: {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(folder_.Get(), &pidl))) {
            SHELLEXECUTEINFOW sei{sizeof(sei)};
            sei.fMask = SEE_MASK_INVOKEIDLIST;
            sei.lpVerb = L"properties";
            sei.hwnd = owner_;
            sei.lpIDList = pidl;
            sei.nShow = SW_SHOWNORMAL;
            ShellExecuteExW(&sei);
            CoTaskMemFree(pidl);
        }
        break;
    }
    }
}
