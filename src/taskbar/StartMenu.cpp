#include "StartMenu.h"
#include "../core/Edition.h"
#include <powrprof.h>
#include <shellapi.h>
#include <string>

namespace {
constexpr float kW = 280.f, kHeader = 54.f, kRow = 36.f, kSep = 9.f, kPower = 62.f, kPad = 8.f;

struct Row {
    int action;  // -1 = separator
    wchar_t glyph;
    const wchar_t* label;
};
// Kolejność wierszy; akcje odpowiadają StartMenu::Action
constexpr Row kRows[] = {
    {0, L'\xE756', L"Terminal"},     {1, L'\xE8EF', L"Kalkulator"},       {2, L'\xE719', L"Sklep"},
    {3, L'\xE8B7', L"Pliki"},        {-1, 0, nullptr},                    {4, L'\xE713', L"Ustawienia"},
    {5, L'\xE9D9', L"Menedżer zadań"}, {6, L'\xE7F4', L"Pokaż pulpit"}, {7, L'\xE7C5', L"Uruchom…"},
};
struct Power {
    int action;
    wchar_t glyph;
    const wchar_t* label;
};
constexpr Power kPowers[] = {{8, L'\xE72E', L"Zablokuj"}, {9, L'\xE708', L"Uśpij"}, {10, L'\xE777', L"Restart"},
                             {11, L'\xE7E8', L"Wyłącz"}, {12, L'\xF3B1', L"Wyloguj"}};

// Aplikacja Explodera obok exploder.exe; w Minimal jej systemowy odpowiednik
void LaunchSibling(const wchar_t* exe, const wchar_t* system) {
    if (Edition::Minimal() && system) {  // system == nullptr: aplikacja Veyry także w Minimal (Ustawienia → Ekran)
        ShellExecuteW(nullptr, L"open", system, nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    p = p.substr(0, p.find_last_of(L'\\') + 1) + exe;
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Zamknięcie systemu wymaga uprawnienia SE_SHUTDOWN_NAME w tokenie procesu
void EnableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return;
    TOKEN_PRIVILEGES tp{1};
    LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
    CloseHandle(token);
}
}  // namespace

D2D1_SIZE_F StartMenu::ContentSize() {
    float h = kHeader;
    for (const Row& r : kRows) h += r.action < 0 ? kSep : kRow;
    return {kW, h + kSep + kPower + kPad};
}

void StartMenu::OnShow() {
    wchar_t name[256]{};
    DWORD n = ARRAYSIZE(name);
    GetUserNameW(name, &n);
    user_ = name;
    hover_ = hoverPower_ = -1;
}

// Zdjęcie konta: shell32 #261 (SHGetUserPicturePath — nieudokumentowane, z niego korzysta menu Start Windows) zapisuje
// je jako .bmp i podaje ścieżkę; bez zdjęcia — domyślny obrazek konta Windows z ProgramData
ComPtr<ID2D1Bitmap> StartMenu::LoadUserPicture(ID2D1DeviceContext* dc) {
    std::vector<std::wstring> paths;
    using GetPicturePath = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPWSTR, UINT);
    if (HMODULE shell = GetModuleHandleW(L"shell32.dll"))
        if (auto fn = reinterpret_cast<GetPicturePath>(GetProcAddress(shell, MAKEINTRESOURCEA(261)))) {
            wchar_t path[MAX_PATH]{};
            if (SUCCEEDED(fn(nullptr, 0x80000000 /* utwórz, gdy brak */, path, MAX_PATH)) && *path) paths.push_back(path);
        }
    wchar_t data[MAX_PATH]{};
    GetEnvironmentVariableW(L"ProgramData", data, MAX_PATH);
    paths.push_back(std::wstring(data) + L"\\Microsoft\\User Account Pictures\\user-192.png");
    paths.push_back(std::wstring(data) + L"\\Microsoft\\User Account Pictures\\user.png");
    IWICImagingFactory* wic = g_->WIC();
    for (const std::wstring& p : paths) {
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> conv;
        ComPtr<ID2D1Bitmap> bmp;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES &&
            SUCCEEDED(wic->CreateDecoderFromFilename(p.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f,
                                       WICBitmapPaletteTypeMedianCut)) &&
            SUCCEEDED(dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bmp)))
            return bmp;
    }
    return nullptr;
}

D2D1_RECT_F StartMenu::RowRect(int i) const {
    float y = kHeader;
    for (int k = 0; k < i; ++k) y += kRows[k].action < 0 ? kSep : kRow;
    return {kPad, y, kW - kPad, y + (kRows[i].action < 0 ? kSep : kRow)};
}

D2D1_RECT_F StartMenu::PowerRect(int i) const {
    const D2D1_RECT_F last = RowRect(static_cast<int>(std::size(kRows)) - 1);
    const float top = last.bottom + kSep, w = (kW - 2 * kPad) / std::size(kPowers);
    return {kPad + i * w, top, kPad + (i + 1) * w, top + kPower};
}

int StartMenu::ItemAt(D2D1_POINT_2F p) const {
    for (int i = 0; i < static_cast<int>(std::size(kRows)); ++i)
        if (kRows[i].action >= 0 && Inside(RowRect(i), p)) return i;
    return -1;
}

int StartMenu::PowerAt(D2D1_POINT_2F p) const {
    for (int i = 0; i < static_cast<int>(std::size(kPowers)); ++i)
        if (Inside(PowerRect(i), p)) return i;
    return -1;
}

void StartMenu::Paint(ID2D1DeviceContext* dc) {
    if (!centered_) {  // podpisy przycisków zasilania — wyśrodkowane
        g_->DWrite()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                       10.5f, L"pl-PL", &centered_);
        if (!centered_) return;
        centered_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        centered_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    // Nagłówek: zdjęcie konta Windows (w kółku) + użytkownik; bez zdjęcia — logo Windows
    if (!pictureTried_) {
        pictureTried_ = true;
        picture_ = LoadUserPicture(dc);
    }
    const D2D1_ELLIPSE avatar = D2D1::Ellipse({30.f, kHeader / 2.f}, 16.f, 16.f);
    ComPtr<ID2D1Factory> factory;
    dc->GetFactory(&factory);
    ComPtr<ID2D1EllipseGeometry> circle;
    if (picture_ && factory && SUCCEEDED(factory->CreateEllipseGeometry(avatar, &circle))) {
        dc->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), circle.Get()), nullptr);
        dc->DrawBitmap(picture_.Get(), {14.f, avatar.point.y - 16.f, 46.f, avatar.point.y + 16.f}, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        dc->PopLayer();
    } else {
        const float lx = 20.f, ly = 17.f, sq = 9.f, gap = 2.f;
        for (int i = 0; i < 4; ++i)
            Fill(dc, {lx + (i % 2) * (sq + gap), ly + (i / 2) * (sq + gap), lx + (i % 2) * (sq + gap) + sq, ly + (i / 2) * (sq + gap) + sq}, kAccent, 1.5f);
    }
    Text(dc, user_.empty() ? L"Windows" : user_, titleFormat_.Get(), {56.f, 10.f, kW - 16.f, kHeader - 10.f}, kText);

    for (int i = 0; i < static_cast<int>(std::size(kRows)); ++i) {
        const D2D1_RECT_F r = RowRect(i);
        if (kRows[i].action < 0) {
            Fill(dc, {r.left + 8.f, (r.top + r.bottom) / 2, r.right - 8.f, (r.top + r.bottom) / 2 + 1.f}, D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
            continue;
        }
        if (hover_ == i) Fill(dc, r, kHover, 8.f);
        Glyph(dc, kRows[i].glyph, {r.left + 6.f, r.top, r.left + 38.f, r.bottom}, kText);
        const wchar_t* label = kRows[i].label;
        if (Edition::Minimal() && kRows[i].action == 0) label = L"Wiersz polecenia";
        else if (Edition::Minimal() && kRows[i].action == 2) label = L"Microsoft Store";
        Text(dc, label, textFormat_.Get(), {r.left + 44.f, r.top + 9.f, r.right - 8.f, r.bottom}, kText);
    }
    const D2D1_RECT_F first = PowerRect(0);
    Fill(dc, {kPad + 8.f, first.top - kSep / 2, kW - kPad - 8.f, first.top - kSep / 2 + 1.f}, D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
    for (int i = 0; i < static_cast<int>(std::size(kPowers)); ++i) {
        const D2D1_RECT_F r = PowerRect(i);
        if (hoverPower_ == i) Fill(dc, {r.left + 2.f, r.top + 2.f, r.right - 2.f, r.bottom - 2.f}, kHover, 8.f);
        Glyph(dc, kPowers[i].glyph, {r.left, r.top + 8.f, r.right, r.top + 32.f}, i == 3 ? D2D1::ColorF(1.f, 0.45f, 0.42f) : kText);
        Text(dc, kPowers[i].label, centered_.Get(), {r.left + 2.f, r.top + 34.f, r.right - 2.f, r.bottom - 6.f}, kDim);
    }
}

void StartMenu::OnMouseMove(D2D1_POINT_2F p) {
    const int h = ItemAt(p), hp = PowerAt(p);
    if (h != hover_ || hp != hoverPower_) {
        hover_ = h;
        hoverPower_ = hp;
        Redraw();
    }
}

void StartMenu::OnMouseDown(D2D1_POINT_2F p) {
    if (const int i = ItemAt(p); i >= 0) {
        Hide();
        Run(static_cast<Action>(kRows[i].action));
    } else if (const int k = PowerAt(p); k >= 0) {
        Hide();
        Run(static_cast<Action>(kPowers[k].action));
    }
}

void StartMenu::Run(Action a) {
    switch (a) {
    case Action::Terminal: LaunchSibling(L"exploder-terminal.exe", L"cmd.exe"); break;
    case Action::Calculator: LaunchSibling(L"exploder-calc.exe", L"calc.exe"); break;
    case Action::Store: LaunchSibling(L"exploder-store.exe", L"ms-windows-store:"); break;
    case Action::Files:
        if (openFiles) openFiles();
        break;
    case Action::Settings: LaunchSibling(L"exploder-settings.exe", nullptr); break;
    case Action::TaskManager: LaunchSibling(L"exploder-taskmgr.exe", L"taskmgr.exe"); break;  // Minimal: systemowy
    case Action::Desktop:
        if (showDesktop) showDesktop();
        break;
    case Action::Run:  // własne okienko Veyry (cmd → Terminal, calc → Kalkulator)
        if (openRun) openRun();
        break;
    case Action::Lock: LockWorkStation(); break;
    case Action::Sleep: SetSuspendState(FALSE, FALSE, FALSE); break;
    case Action::Restart:
        EnableShutdownPrivilege();
        ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
        break;
    case Action::Shutdown:
        EnableShutdownPrivilege();
        ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
        break;
    case Action::Logoff: ExitWindowsEx(EWX_LOGOFF, 0); break;
    }
}
