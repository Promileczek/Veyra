#include "DesktopItems.h"
#include <knownfolders.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {
constexpr wchar_t kRecycleBin[] = L"::{645FF040-5081-101B-9F08-00AA002F954E}";

std::wstring GetName(IShellItem* item, SIGDN type) {
    PWSTR s = nullptr;
    std::wstring out;
    if (SUCCEEDED(item->GetDisplayName(type, &s))) {
        out = s;
        CoTaskMemFree(s);
    }
    return out;
}

void Add(ComPtr<IShellItem> item, std::vector<DesktopItem>& out) {
    DesktopItem d;
    d.key = ItemKey(item.Get());
    d.name = GetName(item.Get(), SIGDN_NORMALDISPLAY);
    d.item = std::move(item);
    out.push_back(std::move(d));
}

// Folder z dysku (np. C:\Users\X\Desktop). Uwaga: SHGetKnownFolderItem(FOLDERID_Desktop) daje wirtualny
// korzeń powłoki (Ten komputer, Panel sterowania, Biblioteki...), dlatego idziemy przez ścieżkę.
void AddFolder(REFKNOWNFOLDERID id, std::vector<DesktopItem>& out) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path))) return;
    ComPtr<IShellItem> folder;
    ComPtr<IEnumShellItems> items;
    const HRESULT hr = SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&folder));
    CoTaskMemFree(path);
    if (FAILED(hr) || FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) return;
    ComPtr<IShellItem> child;
    while (items->Next(1, &child, nullptr) == S_OK) {
        SFGAOF attrs = 0;
        child->GetAttributes(SFGAO_HIDDEN, &attrs);
        if (!(attrs & SFGAO_HIDDEN)) Add(child, out);  // pomija desktop.ini i inne ukryte
        child.Reset();
    }
}

std::wstring LayoutPath() {
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    std::wstring dir = std::wstring(base, n) + L"\\Exploder";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\desktop-layout.txt";
}

// GetImage zwraca premultiplied BGRA, ale miniatury bywają bez kanału alfa (same zera) — wtedy go ignorujemy.
bool HasAlpha(HBITMAP hbm) {
    DIBSECTION ds{};
    if (GetObjectW(hbm, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBitsPixel != 32 || !ds.dsBm.bmBits) return true;
    const auto* bits = static_cast<const BYTE*>(ds.dsBm.bmBits);
    const size_t count = static_cast<size_t>(ds.dsBm.bmWidthBytes) * std::abs(ds.dsBm.bmHeight) / 4;
    for (size_t i = 0; i < count; ++i)
        if (bits[i * 4 + 3]) return true;
    return false;
}

ComPtr<IWICBitmapSource> ToPBGRA(IWICImagingFactory* wic, IWICBitmapSource* src) {
    ComPtr<IWICFormatConverter> conv;
    if (!src || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                WICBitmapPaletteTypeMedianCut)))
        return nullptr;
    return conv;
}
}  // namespace

std::wstring ItemKey(IShellItem* item) { return GetName(item, SIGDN_DESKTOPABSOLUTEPARSING); }

bool IsRecycleBin(const DesktopItem& item) { return _wcsicmp(item.key.c_str(), kRecycleBin) == 0; }

std::vector<DesktopItem> EnumerateDesktopItems() {
    std::vector<DesktopItem> out;
    // Ikony systemowe wg "Ustawienia ikon pulpitu" (HideDesktopIcons: 0 = pokazana, 1 = ukryta).
    struct SystemIcon {
        const wchar_t* clsid;
        bool shownByDefault;
    };
    const SystemIcon systemIcons[] = {
        {L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", false},  // Ten komputer
        {L"{59031a47-3f72-44a7-89c5-5595fe6b30ee}", false},  // Pliki użytkownika
        {L"{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}", false},  // Sieć
        {L"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}", false},  // Panel sterowania
        {L"{645FF040-5081-101B-9F08-00AA002F954E}", true},   // Kosz
    };
    for (const SystemIcon& icon : systemIcons) {
        DWORD hidden = icon.shownByDefault ? 0 : 1, size = sizeof(hidden);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel",
                     icon.clsid, RRF_RT_REG_DWORD, nullptr, &hidden, &size);
        ComPtr<IShellItem> item;
        if (!hidden && SUCCEEDED(SHCreateItemFromParsingName((std::wstring(L"::") + icon.clsid).c_str(), nullptr,
                                                             IID_PPV_ARGS(&item))))
            Add(item, out);
    }
    AddFolder(FOLDERID_Desktop, out);
    AddFolder(FOLDERID_PublicDesktop, out);
    return out;
}

// Miniatury tylko dla obrazów: dla filmów/muzyki Windows ładuje Media Foundation (+20 MB RAM na stałe).
static bool WantsThumbnail(IShellItem* item) {
    const std::wstring name = GetName(item, SIGDN_PARENTRELATIVEPARSING);
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    const wchar_t* ext = name.c_str() + dot;
    for (const wchar_t* e : {L".png", L".jpg", L".jpeg", L".jfif", L".bmp", L".gif", L".webp", L".ico"})
        if (_wcsicmp(ext, e) == 0) return true;
    return false;
}

ComPtr<IWICBitmapSource> LoadItemImage(IShellItem* item, int px, IWICImagingFactory* wic) {
    ComPtr<IShellItemImageFactory> factory;
    HBITMAP hbm = nullptr;
    if (FAILED(item->QueryInterface(IID_PPV_ARGS(&factory)))) return nullptr;
    // Prosimy o 2× większy obraz i zmniejszamy przy rysowaniu — przy dokładnym rozmiarze Windows
    // potrafi przeskalować w górę mniejszą ikonę (rozpikselowane ikony w panelu).
    const SIIGBF mode = WantsThumbnail(item) ? SIIGBF_BIGGERSIZEOK : (SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK);
    px *= 2;
    if (FAILED(factory->GetImage({px, px}, mode, &hbm)) &&
        FAILED(factory->GetImage({px, px}, SIIGBF_ICONONLY, &hbm)))
        return nullptr;
    ComPtr<IWICBitmap> bmp;
    const HRESULT hr = wic->CreateBitmapFromHBITMAP(
        hbm, nullptr, HasAlpha(hbm) ? WICBitmapUsePremultipliedAlpha : WICBitmapIgnoreAlpha, &bmp);
    DeleteObject(hbm);
    return SUCCEEDED(hr) ? ToPBGRA(wic, bmp.Get()) : nullptr;
}

ComPtr<IWICBitmapSource> LoadWallpaper(IWICImagingFactory* wic, UINT width, UINT height) {
    wchar_t path[MAX_PATH]{};
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0);
    if (!*path || !width || !height) return nullptr;

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT w = 0, h = 0;
    if (FAILED(wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) || !w || !h)
        return nullptr;

    // "Wypełnij": skalujemy tak, żeby pokryć cały ekran, i przycinamy środek.
    const double s = std::max(static_cast<double>(width) / w, static_cast<double>(height) / h);
    const UINT sw = std::max(width, static_cast<UINT>(std::ceil(w * s)));
    const UINT sh = std::max(height, static_cast<UINT>(std::ceil(h * s)));
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICBitmapClipper> clipper;
    const WICRect rc{static_cast<INT>((sw - width) / 2), static_cast<INT>((sh - height) / 2), static_cast<INT>(width),
                     static_cast<INT>(height)};
    if (FAILED(wic->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame.Get(), sw, sh, WICBitmapInterpolationModeHighQualityCubic)) ||
        FAILED(wic->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(scaler.Get(), &rc)))
        return nullptr;
    return ToPBGRA(wic, clipper.Get());
}

IconLayout LoadIconLayout() {
    IconLayout layout;
    FILE* f = nullptr;
    if (_wfopen_s(&f, LayoutPath().c_str(), L"r, ccs=UTF-8") || !f) return layout;
    wchar_t line[2048];
    while (fgetws(line, ARRAYSIZE(line), f)) {
        wchar_t* p = nullptr;
        const long col = wcstol(line, &p, 10);
        if (*p != L'\t') continue;
        const long row = wcstol(p + 1, &p, 10);
        if (*p != L'\t') continue;
        std::wstring key = p + 1;
        while (!key.empty() && (key.back() == L'\n' || key.back() == L'\r')) key.pop_back();
        if (!key.empty()) layout[key] = {static_cast<int>(col), static_cast<int>(row)};
    }
    fclose(f);
    return layout;
}

void SaveIconLayout(const IconLayout& layout) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, LayoutPath().c_str(), L"w, ccs=UTF-8") || !f) return;
    for (const auto& [key, pos] : layout) fwprintf(f, L"%d\t%d\t%s\n", pos.first, pos.second, key.c_str());
    fclose(f);
}
