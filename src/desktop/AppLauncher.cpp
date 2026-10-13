#include "AppLauncher.h"
#include "DesktopItems.h"
#include "../core/Edition.h"
#include "../files/FileWindow.h"
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float kPad = 18.f, kSearchH = 38.f, kGridGap = 14.f, kCellW = 92.f, kCellH = 96.f, kIcon = 40.f, kRadius = 22.f;
constexpr int kAllAppsBase = -100;

// Domyślnie przypięte: Kosz, Ten komputer
const wchar_t* const kDefaultPinned[] = {L"::{645FF040-5081-101B-9F08-00AA002F954E}", L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"};

enum MenuCmd { CmdOpen = 1, CmdPin, CmdUnpin, CmdAdd };

std::wstring Lower(const std::wstring& s) {
    std::wstring out(s.size(), L'\0');
    LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE, s.c_str(), static_cast<int>(s.size()), out.data(),
                  static_cast<int>(out.size()), nullptr, nullptr, 0);
    return out;
}

std::wstring PinnedPath() {
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    std::wstring dir = std::wstring(base, n) + L"\\Exploder";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\launcher.txt";
}

ComPtr<IShellItem> ItemFromKey(const std::wstring& key) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(key.c_str(), nullptr, IID_PPV_ARGS(&item)))) return nullptr;
    return item;
}
}  // namespace

void AppLauncher::Init(Graphics& g, std::function<void()> requestRedraw, std::function<void()> sizeChanged) {
    g_ = &g;
    requestRedraw_ = std::move(requestRedraw);
    sizeChanged_ = std::move(sizeChanged);
    IDWriteFactory* dw = g_->DWrite();
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         11.5f, L"pl-PL", &labelFormat_);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         14.f, L"pl-PL", &searchFormat_);
    dw->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 15.f, L"pl-PL", &glyphFormat_);
    if (!labelFormat_ || !searchFormat_ || !glyphFormat_) return;
    labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    labelFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
    dw->CreateEllipsisTrimmingSign(labelFormat_.Get(), &ellipsis_);
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    labelFormat_->SetTrimming(&trimming, ellipsis_.Get());
    searchFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    searchFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    glyphFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    glyphFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

bool AppLauncher::MakeEntry(ComPtr<IShellItem> item, Entry& out) {
    PWSTR name = nullptr, key = nullptr;
    if (!item || FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name))) return false;
    if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &key))) {
        CoTaskMemFree(name);
        return false;
    }
    out.name = name;
    out.lower = Lower(out.name);
    out.key = key;
    out.item = std::move(item);
    CoTaskMemFree(name);
    CoTaskMemFree(key);
    return true;
}

void AppLauncher::Load() {
    pinned_.clear();
    std::vector<std::wstring> keys;
    FILE* f = nullptr;
    if (!_wfopen_s(&f, PinnedPath().c_str(), L"r, ccs=UTF-8") && f) {
        wchar_t line[2048];
        while (fgetws(line, ARRAYSIZE(line), f)) {
            std::wstring key = line;
            while (!key.empty() && (key.back() == L'\n' || key.back() == L'\r')) key.pop_back();
            if (!key.empty()) keys.push_back(key);
        }
        fclose(f);
    } else {
        keys.assign(std::begin(kDefaultPinned), std::end(kDefaultPinned));
    }
    for (const std::wstring& key : keys) {
        Entry e;
        if (MakeEntry(ItemFromKey(key), e)) pinned_.push_back(std::move(e));
    }
    RebuildView();
}

void AppLauncher::SavePinned() const {
    FILE* f = nullptr;
    if (_wfopen_s(&f, PinnedPath().c_str(), L"w, ccs=UTF-8") || !f) return;
    for (const Entry& e : pinned_) fwprintf(f, L"%s\n", e.key.c_str());
    fclose(f);
}

void AppLauncher::LoadAllApps() {
    if (allAppsLoaded_) return;
    allAppsLoaded_ = true;
    ComPtr<IShellItem> folder;
    ComPtr<IEnumShellItems> items;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder))) ||
        FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))))
        return;
    ComPtr<IShellItem> child;
    while (items->Next(1, &child, nullptr) == S_OK) {
        Entry e;
        // Wiersz polecenia i Kalkulator Windows zastępują aplikacje Veyry (niżej; w Minimal ich nie ma)
        const bool replaced = MakeEntry(child, e) && !Edition::Minimal() && ((e.key.size() >= 8 && _wcsicmp(e.key.c_str() + e.key.size() - 8, L"\\cmd.exe") == 0) ||
                                                      e.key.rfind(L"Microsoft.WindowsCalculator_", 0) == 0);
        if (!e.key.empty() && !replaced) allApps_.push_back(std::move(e));
        child.Reset();
    }
    // Aplikacje Veyry (obok exploder.exe) — z dodatkowymi słowami do wyszukiwania ("cmd" znajduje Terminal)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L'\\') + 1);
    const struct {
        const wchar_t* file;
        const wchar_t* name;
        const wchar_t* keywords;
    } veyra[] = {{L"exploder-terminal.exe", L"Terminal", L"cmd wiersz polecenia konsola powershell"},
                 {L"exploder-calc.exe", L"Kalkulator", L"calc calculator liczydło"},
                 {L"exploder-store.exe", L"Sklep", L"store winget programy instaluj"},
                 {L"exploder-settings.exe", L"Ustawienia", L"settings tapeta opcje"}};
    for (const auto& app : veyra) {
        if (Edition::Minimal()) break;
        ComPtr<IShellItem> item;
        Entry e;
        if (FAILED(SHCreateItemFromParsingName((dir + app.file).c_str(), nullptr, IID_PPV_ARGS(&item))) || !MakeEntry(item, e)) continue;
        e.name = app.name;
        e.lower = Lower(std::wstring(app.name) + L" " + app.keywords);
        allApps_.push_back(std::move(e));
    }
    std::sort(allApps_.begin(), allApps_.end(), [](const Entry& a, const Entry& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.name.c_str(), -1, b.name.c_str(), -1, nullptr,
                               nullptr, 0) == CSTR_LESS_THAN;
    });
}

bool AppLauncher::IsPinned(const std::wstring& key) const {
    return std::any_of(pinned_.begin(), pinned_.end(), [&](const Entry& e) { return _wcsicmp(e.key.c_str(), key.c_str()) == 0; });
}

void AppLauncher::Pin(ComPtr<IShellItem> item) {
    Entry e;
    if (!MakeEntry(std::move(item), e) || IsPinned(e.key)) return;
    pinned_.push_back(std::move(e));
    SavePinned();
    RebuildView();
}

void AppLauncher::Unpin(const std::wstring& key) {
    std::erase_if(pinned_, [&](const Entry& e) { return _wcsicmp(e.key.c_str(), key.c_str()) == 0; });
    SavePinned();
    RebuildView();
}

// Okienko "Dodaj do panelu" w stylu Explodera (zamiast systemowego okna wyboru pliku)
void AppLauncher::OpenPicker(HWND owner, const RECT& anchor) {
    if (!picker_.Visible() && !pickerCreated_) {
        pickerCreated_ = picker_.Create(GetModuleHandleW(nullptr), *g_, L"ExploderAppPicker");
        picker_.onPick = [this](ComPtr<IShellItem> item) { Pin(std::move(item)); };
        picker_.onBrowse = [this] {
            if (Edition::Minimal()) {  // bez okien "Pliki" — systemowe okno wyboru pliku
                ComPtr<IFileOpenDialog> dialog;
                ComPtr<IShellItem> item;
                if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) &&
                    SUCCEEDED(dialog->Show(nullptr)) && SUCCEEDED(dialog->GetResult(&item)))
                    Pin(std::move(item));
                return;
            }
            ComPtr<IShellItem> start;
            SHGetKnownFolderItem(FOLDERID_ComputerFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&start));
            OpenFilePicker(GetModuleHandleW(nullptr), *g_, start, L"Kliknij dwukrotnie plik lub program, aby dodać go do panelu",
                           [this](ComPtr<IShellItem> item) { Pin(std::move(item)); });
        };
    }
    if (!pickerCreated_) return;
    LoadAllApps();
    std::vector<AppPicker::App> apps;
    apps.reserve(allApps_.size());
    for (const Entry& e : allApps_) {
        AppPicker::App a;
        a.name = e.name;
        a.lower = e.lower;
        a.key = e.key;
        a.item = e.item;
        a.pinned = IsPinned(e.key);
        apps.push_back(std::move(a));
    }
    picker_.Open(anchor, GetDpiForWindow(owner), std::move(apps));
}

void AppLauncher::RebuildView() {
    const bool wasSearching = !view_.empty() && std::find(view_.begin(), view_.end(), kAddTile) == view_.end();
    (void)kFilesTile;
    view_.clear();
    if (query_.empty()) {
        view_.push_back(kFilesTile);
        for (size_t i = 0; i < pinned_.size(); ++i) view_.push_back(static_cast<int>(i));
        view_.push_back(kAddTile);
    } else {
        LoadAllApps();
        const std::wstring q = Lower(query_);
        std::vector<int> starts, contains;
        auto consider = [&](const Entry& e, int code) {
            const size_t pos = e.lower.find(q);
            if (pos == 0) starts.push_back(code);
            else if (pos != std::wstring::npos) contains.push_back(code);
        };
        for (size_t i = 0; i < pinned_.size(); ++i) consider(pinned_[i], static_cast<int>(i));
        for (size_t i = 0; i < allApps_.size(); ++i)
            if (!IsPinned(allApps_[i].key)) consider(allApps_[i], kAllAppsBase - static_cast<int>(i));
        view_ = std::move(starts);
        view_.insert(view_.end(), contains.begin(), contains.end());
    }
    scrollRow_ = 0;
    hover_ = -1;
    const bool searching = !query_.empty();
    if (sizeChanged_ && (searching != wasSearching || !searching)) sizeChanged_();
}

AppLauncher::Entry* AppLauncher::EntryAt(int viewIndex) {
    if (viewIndex < 0 || viewIndex >= static_cast<int>(view_.size())) return nullptr;
    const int code = view_[static_cast<size_t>(viewIndex)];
    if (code >= 0) return &pinned_[static_cast<size_t>(code)];
    if (code <= kAllAppsBase) return &allApps_[static_cast<size_t>(kAllAppsBase - code)];
    return nullptr;
}

float AppLauncher::PreferredHeight(float width, float maxHeight) const {
    const int cols = std::max(1, static_cast<int>((width - 2 * kPad) / kCellW));
    const float chrome = kPad + kSearchH + kGridGap + kPad;
    if (!query_.empty()) return maxHeight;  // przy szukaniu pełna wysokość
    const int rows = std::max(1, (static_cast<int>(view_.size()) + cols - 1) / cols);
    return std::min(maxHeight, chrome + rows * kCellH);
}

bool AppLauncher::Contains(D2D1_POINT_2F p) const {
    return p.x >= bounds_.left && p.x < bounds_.right && p.y >= bounds_.top && p.y < bounds_.bottom;
}

D2D1_RECT_F AppLauncher::SearchRect() const {
    return {bounds_.left + kPad, bounds_.top + kPad, bounds_.right - kPad, bounds_.top + kPad + kSearchH};
}

D2D1_RECT_F AppLauncher::GridRect() const {
    return {bounds_.left + kPad, bounds_.top + kPad + kSearchH + kGridGap, bounds_.right - kPad, bounds_.bottom - kPad};
}

int AppLauncher::Columns() const {
    const D2D1_RECT_F g = GridRect();
    return std::max(1, static_cast<int>((g.right - g.left) / kCellW));
}

int AppLauncher::VisibleRows() const {
    const D2D1_RECT_F g = GridRect();
    return std::max(1, static_cast<int>((g.bottom - g.top + 1.f) / kCellH));
}

D2D1_RECT_F AppLauncher::CellRect(int visibleIndex) const {
    const D2D1_RECT_F g = GridRect();
    const int cols = Columns();
    const int count = std::min(static_cast<int>(view_.size()), cols);
    // Mało elementów → wyśrodkowane; pełne rzędy → od lewej
    const float x0 = g.left + ((g.right - g.left) - count * kCellW) / 2;
    const float x = x0 + (visibleIndex % cols) * kCellW, y = g.top + (visibleIndex / cols) * kCellH;
    return {x, y, x + kCellW, y + kCellH};
}

int AppLauncher::ViewIndexAt(D2D1_POINT_2F p) const {
    const int cols = Columns(), first = scrollRow_ * cols;
    const int count = std::min(static_cast<int>(view_.size()) - first, cols * VisibleRows());
    for (int v = 0; v < count; ++v) {
        const D2D1_RECT_F r = CellRect(v);
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return first + v;
    }
    return -1;
}

void AppLauncher::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float dpi) {
    if (!labelFormat_) return;
    // Szkło: rozmyta tapeta pod panelem jest w warstwie tapety (Desktop::DrawWallpaper).
    const auto panel = D2D1::RoundedRect(bounds_, kRadius, kRadius);
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.22f));
    dc->DrawRoundedRectangle(panel, brush, 1.f);
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));  // połysk górnej krawędzi
    dc->DrawLine({bounds_.left + kRadius, bounds_.top + 1.5f}, {bounds_.right - kRadius, bounds_.top + 1.5f}, brush, 1.f);

    // Wyszukiwarka
    const D2D1_RECT_F s = SearchRect();
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, focused_ ? 0.14f : 0.08f));
    dc->FillRoundedRectangle(D2D1::RoundedRect(s, 12.f, 12.f), brush);
    brush->SetColor(focused_ ? D2D1::ColorF(0.45f, 0.72f, 1.f, 0.8f) : D2D1::ColorF(1.f, 1.f, 1.f, 0.15f));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(s, 12.f, 12.f), brush, 1.f);
    const wchar_t search = L'\xE721';
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.8f));
    dc->DrawText(&search, 1, glyphFormat_.Get(), {s.left + 6.f, s.top, s.left + 36.f, s.bottom}, brush);
    const D2D1_RECT_F textRect{s.left + 40.f, s.top, s.right - 12.f, s.bottom};
    if (query_.empty()) {
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.5f));
        const std::wstring hint = L"Szukaj aplikacji…";
        dc->DrawText(hint.c_str(), static_cast<UINT32>(hint.size()), searchFormat_.Get(), textRect, brush);
    } else {
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 1.f));
        dc->DrawText(query_.c_str(), static_cast<UINT32>(query_.size()), searchFormat_.Get(), textRect, brush,
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (focused_) {  // kursor tekstowy
        float caretX = textRect.left;
        ComPtr<IDWriteTextLayout> layout;
        if (!query_.empty() &&
            SUCCEEDED(g_->DWrite()->CreateTextLayout(query_.c_str(), static_cast<UINT32>(query_.size()), searchFormat_.Get(),
                                                     10000.f, kSearchH, &layout))) {
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            caretX += m.widthIncludingTrailingWhitespace;
        }
        caretX = std::min(caretX + 1.f, textRect.right);
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.9f));
        dc->DrawLine({caretX, s.top + 10.f}, {caretX, s.bottom - 10.f}, brush, 1.f);
    }

    // Kafelki
    const int cols = Columns(), rows = VisibleRows(), first = scrollRow_ * cols;
    const int count = std::min(static_cast<int>(view_.size()) - first, cols * rows);
    const int iconPx = static_cast<int>(std::ceil(kIcon * dpi / 96.f));
    for (int v = 0; v < count; ++v) {
        const D2D1_RECT_F r = CellRect(v);
        const float cx = (r.left + r.right) / 2;
        if (hover_ == first + v) {
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.12f));
            dc->FillRoundedRectangle(D2D1::RoundedRect({r.left + 3.f, r.top + 2.f, r.right - 3.f, r.bottom - 2.f}, 10.f, 10.f),
                                     brush);
        }
        Entry* e = EntryAt(first + v);
        if (view_[static_cast<size_t>(first + v)] == kFilesTile) {  // "Pliki": ikona folderu z systemu
            if (!filesIcon_) {
                HICON icon = nullptr;
                ComPtr<IWICBitmap> bmp;
                ComPtr<IWICFormatConverter> conv;
                if (PrivateExtractIconsW(L"imageres.dll", 3, iconPx * 2, iconPx * 2, &icon, nullptr, 1, 0) == 1 && icon) {
                    if (SUCCEEDED(g_->WIC()->CreateBitmapFromHICON(icon, &bmp)) && SUCCEEDED(g_->WIC()->CreateFormatConverter(&conv)) &&
                        SUCCEEDED(conv->Initialize(bmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                                   WICBitmapPaletteTypeMedianCut)))
                        dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &filesIcon_);
                    DestroyIcon(icon);
                }
            }
            if (filesIcon_) {
                const D2D1_RECT_F dst{cx - kIcon / 2, r.top + 10.f, cx + kIcon / 2, r.top + 10.f + kIcon};
                dc->DrawBitmap(filesIcon_.Get(), &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            }
            const std::wstring files = L"Pliki";
            ComPtr<IDWriteTextLayout> layout;
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.95f));
            if (SUCCEEDED(g_->DWrite()->CreateTextLayout(files.c_str(), static_cast<UINT32>(files.size()), labelFormat_.Get(),
                                                         kCellW - 8.f, 32.f, &layout)))
                dc->DrawTextLayout({r.left + 4.f, r.top + 56.f}, layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            continue;
        }
        if (!e) {  // "Dodaj": przerywany kwadrat z plusem
            const D2D1_RECT_F box{cx - kIcon / 2, r.top + 10.f, cx + kIcon / 2, r.top + 10.f + kIcon};
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.35f));
            dc->DrawRoundedRectangle(D2D1::RoundedRect(box, 10.f, 10.f), brush, 1.5f);
            const float my = (box.top + box.bottom) / 2;
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.8f));
            dc->DrawLine({cx - 8.f, my}, {cx + 8.f, my}, brush, 2.f);
            dc->DrawLine({cx, my - 8.f}, {cx, my + 8.f}, brush, 2.f);
            const std::wstring add = L"Dodaj";
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(g_->DWrite()->CreateTextLayout(add.c_str(), static_cast<UINT32>(add.size()), labelFormat_.Get(),
                                                         kCellW - 8.f, 32.f, &layout)))
                dc->DrawTextLayout({r.left + 4.f, r.top + 56.f}, layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            continue;
        }
        if (e->pendingIcon) {
            dc->CreateBitmapFromWicBitmap(e->pendingIcon.Get(), nullptr, &e->icon);
            e->pendingIcon.Reset();
        }
        if (!e->iconLoaded) requestRedraw_();  // dociągnie PrepareIcons w następnej klatce
        if (e->icon) {
            const D2D1_RECT_F dst{cx - kIcon / 2, r.top + 10.f, cx + kIcon / 2, r.top + 10.f + kIcon};
            dc->DrawBitmap(e->icon.Get(), &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        }
        if (!e->label)
            g_->DWrite()->CreateTextLayout(e->name.c_str(), static_cast<UINT32>(e->name.size()), labelFormat_.Get(), kCellW - 8.f,
                                           32.f, &e->label);
        if (e->label) {
            brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.95f));
            dc->DrawTextLayout({r.left + 4.f, r.top + 56.f}, e->label.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    if (view_.empty()) {
        const std::wstring none = L"Brak wyników";
        const D2D1_RECT_F g = GridRect();
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.5f));
        dc->DrawText(none.c_str(), static_cast<UINT32>(none.size()), searchFormat_.Get(), {g.left + 8.f, g.top, g.right, g.top + 30.f},
                     brush);
    }

    // Pasek przewijania
    const int totalRows = (static_cast<int>(view_.size()) + cols - 1) / cols;
    if (totalRows > rows) {
        const D2D1_RECT_F g = GridRect();
        const float trackH = g.bottom - g.top, thumbH = std::max(24.f, trackH * rows / totalRows);
        const float y = g.top + (trackH - thumbH) * scrollRow_ / std::max(1, totalRows - rows);
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.25f));
        dc->FillRoundedRectangle(D2D1::RoundedRect({bounds_.right - 9.f, y, bounds_.right - 6.f, y + thumbH}, 1.5f, 1.5f), brush);
    }
}

void AppLauncher::PrepareIcons(float dpi) {
    const int cols = Columns(), first = scrollRow_ * cols;
    const int count = std::min(static_cast<int>(view_.size()) - first, cols * VisibleRows());
    const int iconPx = static_cast<int>(std::ceil(kIcon * dpi / 96.f));
    for (int v = 0, loaded = 0; v < count && loaded < 12; ++v) {
        Entry* e = EntryAt(first + v);
        if (!e || e->iconLoaded) continue;
        e->iconLoaded = true;
        ++loaded;
        const std::wstring key = e->key;
        const ComPtr<IShellItem> item = e->item;
        ComPtr<IWICBitmapSource> src = LoadItemImage(item.Get(), iconPx, g_->WIC());
        if (Entry* again = EntryAt(first + v); again && again->key == key) again->pendingIcon = std::move(src);
    }
}

void AppLauncher::OnMouseMove(D2D1_POINT_2F p) {
    const int hit = ViewIndexAt(p);
    if (hit != hover_) {
        hover_ = hit;
        requestRedraw_();
    }
}

void AppLauncher::OnMouseLeave() {
    if (hover_ != -1) {
        hover_ = -1;
        requestRedraw_();
    }
}

void AppLauncher::OnClick(D2D1_POINT_2F p, HWND owner) {
    SetFocused(true);
    const int hit = ViewIndexAt(p);
    if (hit < 0) return;
    if (view_[static_cast<size_t>(hit)] == kFilesTile) {
        if (openFiles) openFiles();
    } else if (Entry* e = EntryAt(hit)) {
        Launch(*e);
    } else {  // kafelek "Dodaj": okienko pod kafelkiem
        const int first = scrollRow_ * Columns();
        const D2D1_RECT_F c = CellRect(hit - first);
        const float k = GetDpiForWindow(owner) / 96.f;
        POINT a{static_cast<LONG>(c.left * k), static_cast<LONG>(c.top * k)}, b{static_cast<LONG>(c.right * k), static_cast<LONG>(c.bottom * k)};
        ClientToScreen(owner, &a);
        ClientToScreen(owner, &b);
        OpenPicker(owner, {a.x, a.y, b.x, b.y});
    }
}

void AppLauncher::OnRightClick(D2D1_POINT_2F p, POINT screen, HWND owner) {
    const int hit = ViewIndexAt(p);
    if (hit >= 0 && view_[static_cast<size_t>(hit)] == kFilesTile) return;
    Entry* e = EntryAt(hit);
    HMENU menu = CreatePopupMenu();
    if (e) {
        AppendMenuW(menu, MF_STRING, CmdOpen, L"Otwórz");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (IsPinned(e->key)) AppendMenuW(menu, MF_STRING, CmdUnpin, L"Usuń z panelu");
        else AppendMenuW(menu, MF_STRING, CmdPin, L"Przypnij do panelu");
    } else {
        AppendMenuW(menu, MF_STRING, CmdAdd, L"Dodaj do panelu…");
    }
    const std::wstring key = e ? e->key : std::wstring();
    const ComPtr<IShellItem> item = e ? e->item : nullptr;
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, owner, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case CmdOpen:
        if (Entry* again = EntryAt(hit)) Launch(*again);
        break;
    case CmdPin:
        Pin(item);
        break;
    case CmdUnpin:
        Unpin(key);
        break;
    case CmdAdd:
        OpenPicker(owner, {screen.x, screen.y, screen.x + 1, screen.y + 1});
        break;
    }
    requestRedraw_();
}

void AppLauncher::OnWheel(int delta) {
    const int cols = Columns();
    const int totalRows = (static_cast<int>(view_.size()) + cols - 1) / cols;
    const int maxRow = std::max(0, totalRows - VisibleRows());
    const int next = std::clamp(scrollRow_ + (delta > 0 ? -1 : 1), 0, maxRow);
    if (next != scrollRow_) {
        scrollRow_ = next;
        hover_ = -1;
        requestRedraw_();
    }
}

void AppLauncher::OnChar(wchar_t ch, HWND owner) {
    (void)owner;
    SetFocused(true);
    if (ch == L'\b') {
        if (query_.empty()) return;
        query_.pop_back();
    } else if (ch == L'\r') {
        if (Entry* e = EntryAt(0)) Launch(*e);
        return;
    } else if (ch == 0x1B) {  // Esc
        if (query_.empty()) return;
        query_.clear();
    } else if (ch >= L' ') {
        query_ += ch;
    } else {
        return;
    }
    RebuildView();
    requestRedraw_();
}

void AppLauncher::SetFocused(bool focused) {
    if (focused_ == focused) return;
    focused_ = focused;
    requestRedraw_();
}

void AppLauncher::Launch(Entry& entry) {
    SFGAOF attrs = 0;
    entry.item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attrs);
    if ((attrs & SFGAO_FOLDER) && !(attrs & SFGAO_STREAM) && openFolder) {  // folder → Exploder (nie explorer.exe)
        openFolder(entry.item);
        if (!query_.empty()) {
            query_.clear();
            RebuildView();
        }
        SetFocused(false);
        requestRedraw_();
        return;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetIDListFromObject(entry.item.Get(), &pidl))) return;
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.lpIDList = pidl;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
    CoTaskMemFree(pidl);
    if (!query_.empty()) {
        query_.clear();
        RebuildView();
    }
    SetFocused(false);
    requestRedraw_();
}
