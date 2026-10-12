#include "AppIcons.h"
#include "../shell/TrayService.h"
#include <shobjidl.h>
#include <algorithm>
#include <cwctype>
#include <initializer_list>
#include <vector>

namespace {
std::wstring ProcessPath(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return {};
    wchar_t path[MAX_PATH * 2]{};
    DWORD len = ARRAYSIZE(path);
    const BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &len);
    CloseHandle(proc);
    return ok ? std::wstring(path, len) : std::wstring();
}

// "C:\...\Brave.exe" -> "brave"
std::wstring AppName(const std::wstring& path) {
    std::wstring name = path.substr(path.find_last_of(L'\\') + 1);
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) name.resize(name.size() - 4);
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return name;
}

ComPtr<IWICBitmapSource> LoadCustomIcon(IWICImagingFactory* wic, const std::wstring& app) {
    if (app.empty()) return nullptr;
    const std::wstring file = CustomIconsDir() + L"\\" + app + L".png";
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)))
        return nullptr;
    return frame;
}

// Duża ikona z pliku exe (WM_GETICON daje max 32 px — za mało dla docka).
ComPtr<IWICBitmapSource> LoadExeIcon(IWICImagingFactory* wic, const std::wstring& path, int px) {
    // Okna UWP należą do ApplicationFrameHost — jego ikona nic nie mówi, zostaje ikona okna.
    if (path.empty() || AppName(path) == L"applicationframehost") return nullptr;

    // Najpierw zasób ikony w dokładnie potrzebnym rozmiarze: Windows wybiera największy obraz z pliku
    // i skaluje go w dół. (Obraz z IShellItemImageFactory bywał za mały i przycięty — np. koło Discorda
    // miało ścięte boki.)
    HICON hicon = nullptr;
    if (PrivateExtractIconsW(path.c_str(), 0, px, px, &hicon, nullptr, 1, LR_DEFAULTCOLOR) == 1 && hicon) {
        ComPtr<IWICBitmap> bmp;
        const HRESULT hr = wic->CreateBitmapFromHICON(hicon, &bmp);
        DestroyIcon(hicon);
        if (SUCCEEDED(hr)) return bmp;
    }

    ComPtr<IShellItemImageFactory> factory;
    HBITMAP hbm = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->GetImage({px, px}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbm)))
        return nullptr;
    ComPtr<IWICBitmap> bmp;
    const HRESULT hr = wic->CreateBitmapFromHBITMAP(hbm, nullptr, WICBitmapUsePremultipliedAlpha, &bmp);
    DeleteObject(hbm);
    return SUCCEEDED(hr) ? bmp : nullptr;
}

ComPtr<IWICBitmapSource> LoadWindowIcon(IWICImagingFactory* wic, HWND h) {
    HICON icon = nullptr;
    DWORD_PTR result = 0;
    for (WPARAM type : {WPARAM(ICON_BIG), WPARAM(ICON_SMALL2), WPARAM(ICON_SMALL)})
        if (SendMessageTimeoutW(h, WM_GETICON, type, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result) && result) {
            icon = reinterpret_cast<HICON>(result);
            break;
        }
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(h, GCLP_HICON));
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(h, GCLP_HICONSM));
    if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
    ComPtr<IWICBitmap> bmp;
    return SUCCEEDED(wic->CreateBitmapFromHICON(icon, &bmp)) ? bmp : nullptr;
}

// Nazwa programu z zasobów exe ("Discord", "Brave Browser")
std::wstring FileDescription(const std::wstring& path) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    struct LangCodePage {
        WORD lang, codePage;
    }* translation = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation), &len) ||
        len < sizeof(LangCodePage))
        return {};
    wchar_t key[64];
    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", translation->lang, translation->codePage);
    wchar_t* value = nullptr;
    if (!VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &len) || len <= 1) return {};
    return value;
}

BuiltinIcon BuiltinFor(HWND h, const std::wstring& app) {
    wchar_t cls[64]{};
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    const bool console = wcscmp(cls, L"ConsoleWindowClass") == 0 || app == L"cmd" || app == L"conhost";
    if (!console) return BuiltinIcon::None;
    return app == L"powershell" || app == L"pwsh" ? BuiltinIcon::PowerShell : BuiltinIcon::Console;
}

ComPtr<ID2D1LinearGradientBrush> Gradient(ID2D1DeviceContext* dc, float y0, float y1, UINT32 top, UINT32 bottom) {
    const D2D1_GRADIENT_STOP stops[] = {{0.f, D2D1::ColorF(top)}, {1.f, D2D1::ColorF(bottom)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> brush;
    if (SUCCEEDED(dc->CreateGradientStopCollection(stops, 2, &collection)))
        dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, y0}, {0, y1}), collection.Get(), &brush);
    return brush;
}
}  // namespace

std::wstring AppNameForWindow(HWND hwnd) {
    static std::vector<std::pair<std::wstring, std::wstring>> cache;  // ścieżka exe → nazwa
    const std::wstring path = ProcessPath(hwnd);
    if (path.empty()) return {};
    for (const auto& [p, name] : cache)
        if (p == path) return name;
    const std::wstring app = AppName(path);
    std::wstring name = app == L"cmd" || app == L"conhost" ? L"Wiersz polecenia" : FileDescription(path);
    if (name.empty()) name = app;
    if (cache.size() > 200) cache.clear();
    cache.emplace_back(path, name);
    return name;
}

std::wstring CustomIconsDir() {
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    return std::wstring(base, n) + L"\\Exploder\\icons";
}

AppIcon ResolveAppIcon(HWND hwnd, int sizePx, IWICImagingFactory* wic, ID2D1DeviceContext* dc) {
    AppIcon icon;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) {  // okna Explodera: ikona folderu z klasy okna
        icon.appName = L"Exploder";
        ComPtr<IWICFormatConverter> conv;
        const HICON own = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
        ComPtr<IWICBitmap> bmp;
        if (own && SUCCEEDED(wic->CreateBitmapFromHICON(own, &bmp)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(bmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                       WICBitmapPaletteTypeMedianCut)))
            dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &icon.bitmap);
        return icon;
    }
    const std::wstring path = ProcessPath(hwnd);
    const std::wstring app = path.empty() ? std::wstring() : AppName(path);
    if (app == L"cmd" || app == L"conhost") icon.appName = L"Wiersz polecenia";
    else if (app == L"powershell" || app == L"pwsh") icon.appName = L"PowerShell";
    else if (!path.empty()) icon.appName = FileDescription(path);
    if (icon.appName.empty()) icon.appName = app;

    ComPtr<IWICBitmapSource> source = LoadCustomIcon(wic, app);
    if (!source) {
        icon.builtin = BuiltinFor(hwnd, app);
        if (icon.builtin != BuiltinIcon::None) return icon;
        source = LoadExeIcon(wic, path, sizePx);
    }
    if (!source) source = LoadWindowIcon(wic, hwnd);

    ComPtr<IWICFormatConverter> conv;
    if (source && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                   WICBitmapPaletteTypeMedianCut)))
        dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &icon.bitmap);
    return icon;
}

ID2D1Bitmap* TrayIconBitmap(TrayIcon& icon, IWICImagingFactory* wic, ID2D1DeviceContext* dc) {
    if (icon.bitmap || !icon.icon) return icon.bitmap.Get();
    ComPtr<IWICBitmap> bmp;
    ComPtr<IWICFormatConverter> conv;
    if (SUCCEEDED(wic->CreateBitmapFromHICON(icon.icon, &bmp)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(bmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                   WICBitmapPaletteTypeMedianCut)))
        dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &icon.bitmap);
    return icon.bitmap.Get();
}

// Okno terminala jak w Windows 11: zaokrąglone okienko z paskiem tytułu i znakiem zachęty ">_".
void DrawBuiltinIcon(ID2D1DeviceContext* dc, BuiltinIcon kind, const D2D1_RECT_F& r, float opacity) {
    const float s = r.right - r.left;
    const D2D1_RECT_F body{r.left + s * 0.05f, r.top + s * 0.12f, r.right - s * 0.05f, r.bottom - s * 0.12f};
    const float h = body.bottom - body.top;
    const float radius = s * 0.11f;
    const float barH = h * 0.2f;

    const bool ps = kind == BuiltinIcon::PowerShell;
    const UINT32 bodyTop = ps ? 0x2F80E0 : 0x3A3A3A, bodyBottom = ps ? 0x1A4FA8 : 0x161616;
    const UINT32 barTop = ps ? 0x8EC2FF : 0x8A8A8A, barBottom = ps ? 0x5C9BEB : 0x5E5E5E;

    auto fill = Gradient(dc, body.top, body.bottom, bodyTop, bodyBottom);
    auto bar = Gradient(dc, body.top, body.top + barH, barTop, barBottom);
    ComPtr<ID2D1SolidColorBrush> solid;
    dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, opacity), &solid);
    if (!fill || !bar || !solid) return;
    fill->SetOpacity(opacity);
    bar->SetOpacity(opacity);

    // Korpus, potem pasek tytułu przycięty do zaokrąglonego kształtu korpusu
    const auto shape = D2D1::RoundedRect(body, radius, radius);
    dc->FillRoundedRectangle(shape, fill.Get());
    ComPtr<ID2D1Factory> factory;
    dc->GetFactory(&factory);
    ComPtr<ID2D1RoundedRectangleGeometry> clip;
    if (SUCCEEDED(factory->CreateRoundedRectangleGeometry(shape, &clip))) {
        dc->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
        dc->FillRectangle({body.left, body.top, body.right, body.top + barH}, bar.Get());
        dc->PopLayer();
    }
    solid->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.16f * opacity));
    dc->DrawRoundedRectangle(shape, solid.Get(), s * 0.015f);

    // ">_"
    const float stroke = s * 0.065f;
    const float x0 = body.left + s * 0.17f, cy = body.top + barH + (h - barH) * 0.5f;
    const float cw = s * 0.15f, ch = s * 0.15f;
    ComPtr<ID2D1PathGeometry> chevron;
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1StrokeStyle> round;
    const auto strokeProps = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                         D2D1_LINE_JOIN_ROUND);
    if (FAILED(factory->CreateStrokeStyle(strokeProps, nullptr, 0, &round)) ||
        FAILED(factory->CreatePathGeometry(&chevron)) || FAILED(chevron->Open(&sink)))
        return;
    sink->BeginFigure({x0, cy - ch}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine({x0 + cw, cy});
    sink->AddLine({x0, cy + ch});
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    solid->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, opacity));
    dc->DrawGeometry(chevron.Get(), solid.Get(), stroke, round.Get());
    const float ux = x0 + cw + s * 0.08f;
    dc->DrawLine({ux, cy + ch}, {ux + s * 0.2f, cy + ch}, solid.Get(), stroke, round.Get());
}
