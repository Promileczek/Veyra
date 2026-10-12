#include "RunBox.h"
#include "../core/Settings.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>

namespace {
constexpr D2D1_RECT_F kField{16.f, 70.f, 454.f, 106.f};
constexpr D2D1_RECT_F kOk{262.f, 120.f, 354.f, 152.f};
constexpr D2D1_RECT_F kCancel{362.f, 120.f, 454.f, 152.f};

std::wstring Trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1);
}

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

// Aplikacja Veyry obok exploder.exe
bool LaunchSibling(const wchar_t* exe) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    p = p.substr(0, p.find_last_of(L'\\') + 1) + exe;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
}  // namespace

void RunBox::Open(UINT dpi) {
    // Kotwica nad środkiem ekranu → okienko na środku, trochę powyżej połowy
    MONITORINFO mi{sizeof(mi)};
    POINT cursor{};
    GetCursorPos(&cursor);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
    const LONG cx = (mi.rcWork.left + mi.rcWork.right) / 2, cy = (mi.rcWork.top + mi.rcWork.bottom) / 2;
    Toggle({cx, cy - 160, cx + 1, cy - 159}, dpi);
}

void RunBox::OnShow() {
    text_ = Settings::Get(L"runLast");
    selectAll_ = !text_.empty();
    error_.clear();
    hover_ = -1;
}

void RunBox::Paint(ID2D1DeviceContext* dc) {
    Glyph(dc, L'\xE7C5', {14.f, 12.f, 44.f, 42.f}, kAccent);
    Text(dc, L"Uruchom", titleFormat_.Get(), {50.f, 12.f, 450.f, 32.f}, kText);
    Text(dc, L"Wpisz program, folder, dokument albo adres internetowy.", smallFormat_.Get(), {50.f, 34.f, 456.f, 54.f}, kDim);

    // Pole tekstowe (zaznaczona ostatnia komenda — pierwszy znak ją zastępuje)
    Fill(dc, kField, D2D1::ColorF(1.f, 1.f, 1.f, 0.08f), 8.f);
    brush_->SetColor(kAccent);
    dc->DrawRoundedRectangle(D2D1::RoundedRect({kField.left + 0.5f, kField.top + 0.5f, kField.right - 0.5f, kField.bottom - 0.5f}, 8.f, 8.f),
                             brush_.Get(), 1.f);
    const D2D1_RECT_F textRect{kField.left + 12.f, kField.top + 9.f, kField.right - 12.f, kField.bottom};
    if (selectAll_ && !text_.empty()) {
        ComPtr<IDWriteTextLayout> layout;
        g_->DWrite()->CreateTextLayout(text_.c_str(), static_cast<UINT32>(text_.size()), textFormat_.Get(), 1000.f, 30.f, &layout);
        DWRITE_TEXT_METRICS m{};
        if (layout) layout->GetMetrics(&m);
        Fill(dc, {textRect.left - 2.f, kField.top + 8.f, textRect.left + std::min(m.widthIncludingTrailingWhitespace, textRect.right - textRect.left) + 2.f,
                  kField.bottom - 8.f},
             D2D1::ColorF(kAccent.r, kAccent.g, kAccent.b, 0.35f), 3.f);
    }
    Text(dc, text_ + (selectAll_ ? L"" : L"|"), textFormat_.Get(), textRect, kText);

    if (!error_.empty()) Text(dc, error_, smallFormat_.Get(), {16.f, 128.f, 256.f, 148.f}, D2D1::ColorF(1.f, 0.55f, 0.5f));
    else Text(dc, L"cmd → Terminal · calc → Kalkulator", smallFormat_.Get(), {16.f, 128.f, 256.f, 148.f}, kDim);
    Fill(dc, kOk, hover_ == 0 ? D2D1::ColorF(0.48f, 0.77f, 1.f) : kAccent, 8.f);
    Text(dc, L"OK", textFormat_.Get(), {kOk.left + 36.f, kOk.top + 7.f, kOk.right, kOk.bottom}, D2D1::ColorF(0x0B1A26));
    Fill(dc, kCancel, D2D1::ColorF(1.f, 1.f, 1.f, hover_ == 1 ? 0.16f : 0.09f), 8.f);
    Text(dc, L"Anuluj", textFormat_.Get(), {kCancel.left + 26.f, kCancel.top + 7.f, kCancel.right, kCancel.bottom}, kText);
}

void RunBox::OnMouseMove(D2D1_POINT_2F p) {
    const int h = Inside(kOk, p) ? 0 : Inside(kCancel, p) ? 1 : -1;
    if (h != hover_) {
        hover_ = h;
        Redraw();
    }
}

void RunBox::OnMouseDown(D2D1_POINT_2F p) {
    if (Inside(kOk, p)) {
        if (Run()) Hide();
        else Redraw();
    } else if (Inside(kCancel, p)) {
        Hide();
    } else if (Inside(kField, p) && selectAll_) {
        selectAll_ = false;  // klik w pole: kursor na końcu, bez zaznaczenia
        Redraw();
    }
}

void RunBox::OnChar(wchar_t ch) {
    if (ch == L'\r') {
        if (Run()) Hide();
        else Redraw();
        return;
    }
    if (ch == 0x16) {  // Ctrl+V
        if (OpenClipboard(hwnd_)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
                    std::wstring s = p;
                    s.erase(std::remove_if(s.begin(), s.end(), [](wchar_t c) { return c == L'\r' || c == L'\n'; }), s.end());
                    if (selectAll_) text_.clear();
                    text_ += s;
                    selectAll_ = false;
                }
                GlobalUnlock(h);
            }
            CloseClipboard();
        }
    } else if (ch == L'\b') {
        if (selectAll_) text_.clear();
        else if (!text_.empty()) text_.pop_back();
        selectAll_ = false;
    } else if (ch == 0x01) {  // Ctrl+A
        selectAll_ = !text_.empty();
    } else if (ch >= L' ') {
        if (selectAll_) text_.clear();
        selectAll_ = false;
        text_ += ch;
    } else {
        return;
    }
    error_.clear();
    Redraw();
}

bool RunBox::Run() {
    const std::wstring cmd = Trim(text_);
    if (cmd.empty()) return false;
    const std::wstring low = Lower(cmd);
    bool ok = false;
    // Same nazwy (bez argumentów) → aplikacje Veyry; "cmd /c …" itp. zostaje prawdziwym cmd.exe
    if (low == L"cmd" || low == L"cmd.exe" || low == L"terminal") {
        ok = LaunchSibling(L"exploder-terminal.exe");
    } else if (low == L"calc" || low == L"calc.exe" || low == L"kalkulator") {
        ok = LaunchSibling(L"exploder-calc.exe");
    } else {
        // Jak w Windows: najpierw całość (ścieżka ze spacjami, adres, shell:…), potem program + argumenty
        SHELLEXECUTEINFOW sei{sizeof(sei)};
        sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        sei.nShow = SW_SHOWNORMAL;
        sei.lpFile = cmd.c_str();
        ok = ShellExecuteExW(&sei) != FALSE;
        if (!ok) {
            std::wstring file = cmd;
            const std::wstring args = PathGetArgsW(cmd.c_str());
            PathRemoveArgsW(file.data());
            file.resize(wcslen(file.c_str()));
            PathUnquoteSpacesW(file.data());
            file.resize(wcslen(file.c_str()));
            SHELLEXECUTEINFOW sei2{sizeof(sei2)};
            sei2.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
            sei2.nShow = SW_SHOWNORMAL;
            sei2.lpFile = file.c_str();
            sei2.lpParameters = args.empty() ? nullptr : args.c_str();
            ok = ShellExecuteExW(&sei2) != FALSE;
        }
    }
    if (ok) Settings::Set(L"runLast", cmd);
    else error_ = L"Nie znaleziono: " + cmd;
    return ok;
}
