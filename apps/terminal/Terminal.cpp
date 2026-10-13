// Terminal Explodera — w stylu terminala Linuksa: ciemne tło, prompt user@host:ścieżka$, kolory Tango.
// Pod spodem: cmd.exe w pseudokonsoli Windows (ConPTY) + parser sekwencji VT (kursor, kasowanie, kolory 16/256/24-bit,
// ekran alternatywny, region przewijania). Aliasy jak w Linuksie (ls, cat, grep, clear…) przez doskey.
// Skróty: Ctrl+C (kopiuje, gdy coś zaznaczone; inaczej przerywa), Ctrl+Shift+C / Ctrl+Shift+V, Ctrl+V, PPM = kopiuj/wklej,
// Shift+PgUp/PgDn i kółko — historia, Ctrl+plus/minus/0 — rozmiar czcionki.
#include "../../src/core/AppWindow.h"
#include "../../src/core/Settings.h"
#include <objbase.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr UINT WM_PTY_OUTPUT = WM_APP + 10;
constexpr UINT WM_PTY_EXIT = WM_APP + 11;
constexpr UINT_PTR kBlinkTimer = 1;
constexpr size_t kScrollback = 5000;
constexpr float kPad = 10.f;
constexpr uint32_t kDefault = 0xFF000000;  // kolor domyślny (nie RGB)

// Paleta Tango (terminal GNOME/Ubuntu)
constexpr uint32_t kPalette[16] = {0x2E3436, 0xCC0000, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
                                   0x555753, 0xEF2929, 0x8AE234, 0xFCE94F, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC};
constexpr uint32_t kFg = 0xE6E6E6, kBg = 0x121318;  // neutralne ciemne tło (jak Kalkulator)

uint32_t Color256(int n) {
    if (n < 16) return kPalette[n];
    if (n < 232) {
        static const int lv[] = {0, 95, 135, 175, 215, 255};
        n -= 16;
        return (lv[n / 36] << 16) | (lv[(n / 6) % 6] << 8) | lv[n % 6];
    }
    const int g = 8 + 10 * (n - 232);
    return (g << 16) | (g << 8) | g;
}

D2D1_COLOR_F ToColor(uint32_t rgb, float a = 1.f) {
    return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}

enum Attr : uint8_t { Bold = 1, Underline = 2, Inverse = 4, Italic = 8, Faint = 16 };

struct Cell {
    char32_t ch = U' ';
    uint32_t fg = kDefault, bg = kDefault;
    uint8_t attr = 0;
};
using Line = std::vector<Cell>;

std::wstring ToUtf16(char32_t c) {
    if (c < 0x10000) return std::wstring(1, static_cast<wchar_t>(c));
    c -= 0x10000;
    return {static_cast<wchar_t>(0xD800 + (c >> 10)), static_cast<wchar_t>(0xDC00 + (c & 0x3FF))};
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring LocalAppDataDir() {
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) dir = std::wstring(p) + L"\\Exploder";
    CoTaskMemFree(p);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// Aliasy jak w Linuksie (doskey). $* = argumenty.
constexpr char kMacros[] =
    "ls=dir /b $*\r\nll=dir $*\r\nla=dir /a $*\r\nclear=cls\r\npwd=cd\r\ncat=type $*\r\nrm=del $*\r\ncp=copy $*\r\nmv=move $*\r\n"
    "touch=type nul >> $*\r\ngrep=findstr $*\r\nwhich=where $*\r\nifconfig=ipconfig $*\r\nps=tasklist $*\r\nkill=taskkill /PID $*\r\n"
    "nano=notepad $*\r\nman=help $*\r\nhistory=doskey /history\r\nuname=ver\r\nhome=cd /d %USERPROFILE%\r\n";

class Terminal : public AppWindow {
public:
    bool Init(HINSTANCE inst, Graphics& g) {
        Options o;
        o.className = L"ExploderTerminal";
        o.title = L"Terminal";
        o.width = 860.f;
        o.height = 540.f;
        o.minWidth = 360.f;
        o.minHeight = 200.f;
        titleH_ = 38.f;
        fontSize_ = static_cast<float>(_wtof(Settings::Get(L"terminalFontSize", L"14").c_str()));
        if (fontSize_ < 8.f || fontSize_ > 32.f) fontSize_ = 14.f;
        // Rozmiar komórek musi być znany przed pierwszym OnResize — fabryka DirectWrite jest już w Graphics
        g_ = &g;
        MakeFonts();
        if (!Create(inst, g, o)) return false;
        if (!StartShell()) return false;
        SetTimer(hwnd_, kBlinkTimer, 530, nullptr);
        return true;
    }

    ~Terminal() override { StopShell(); }

protected:
    // ---------------- Rysowanie ----------------
    void Paint(ID2D1DeviceContext* dc) override {
        dc->Clear(ToColor(kBg));
        Fill(dc, {0.f, titleH_ - 1.f, widthDip_, titleH_}, D2D1::ColorF(1.f, 1.f, 1.f, 0.06f));
        if (!regular_) return;
        const float left = kPad, top = titleH_ + 6.f;
        const int visibleRows = rows_;
        const size_t sb = scrollback_.size();
        const int offset = std::min<int>(viewOffset_, static_cast<int>(sb));
        for (int r = 0; r < visibleRows; ++r) {
            // Wiersz widoku: najpierw (przewinięta) historia, potem ekran
            const int idx = static_cast<int>(sb) - offset + r;  // indeks w [historia..., ekran...]
            const Line* line = nullptr;
            if (idx < static_cast<int>(sb)) line = &scrollback_[static_cast<size_t>(idx)];
            else if (idx - static_cast<int>(sb) < rows_) line = &Screen()[static_cast<size_t>(idx - static_cast<int>(sb))];
            if (!line) continue;
            const float y = top + r * cellH_;
            DrawLine(dc, *line, left, y, idx);
        }
        // Kursor (tylko gdy nie przewijamy historii)
        if (offset == 0 && cursorVisible_ && cy_ < rows_) {
            const float x = left + std::min(cx_, cols_ - 1) * cellW_, y = top + cy_ * cellH_;
            if (active_) {
                if (blinkOn_) Fill(dc, {x, y, x + cellW_, y + cellH_}, D2D1::ColorF(1.f, 1.f, 1.f, 0.85f));
            } else {
                brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.7f));
                dc->DrawRectangle({x + 0.5f, y + 0.5f, x + cellW_ - 0.5f, y + cellH_ - 0.5f}, brush_.Get(), 1.f);
            }
        }
        // Pasek przewijania historii
        if (sb > 0 && offset > 0) {
            const float h = rows_ * cellH_, total = static_cast<float>(sb + rows_);
            const float thumb = std::max(20.f, h * rows_ / total);
            const float pos = (static_cast<float>(sb) - offset) / static_cast<float>(sb);
            const float y = top + (h - thumb) * pos;
            Fill(dc, {widthDip_ - 6.f, y, widthDip_ - 3.f, y + thumb}, D2D1::ColorF(1.f, 1.f, 1.f, 0.35f), 1.5f);
        }
    }

    void DrawLine(ID2D1DeviceContext* dc, const Line& line, float left, float y, int absRow) {
        const int n = std::min<int>(static_cast<int>(line.size()), cols_);
        int c = 0;
        while (c < n) {
            const Cell& first = line[static_cast<size_t>(c)];
            const bool sel0 = Selected(absRow, c);
            int e = c + 1;
            while (e < n) {
                const Cell& x = line[static_cast<size_t>(e)];
                if (x.fg != first.fg || x.bg != first.bg || x.attr != first.attr || Selected(absRow, e) != sel0) break;
                ++e;
            }
            uint32_t fg = first.fg == kDefault ? kFg : first.fg, bg = first.bg;
            if ((first.attr & Bold) && first.fg != kDefault)  // pogrubiony kolor podstawowy → jasna odmiana
                for (int i = 0; i < 8; ++i)
                    if (fg == kPalette[i]) fg = kPalette[i + 8];
            if (first.attr & Inverse) {
                const uint32_t f = fg;
                fg = bg == kDefault ? kBg : bg;
                bg = f;
            }
            const float x0 = left + c * cellW_, x1 = left + e * cellW_;
            if (sel0) Fill(dc, {x0, y, x1, y + cellH_}, D2D1::ColorF(0.40f, 0.62f, 1.f, 0.45f));
            else if (bg != kDefault) Fill(dc, {x0, y, x1, y + cellH_}, ToColor(bg));
            std::wstring text;
            for (int i = c; i < e; ++i) text += ToUtf16(line[static_cast<size_t>(i)].ch);
            const size_t last = text.find_last_not_of(L' ');
            if (last != std::wstring::npos) {
                text.resize(last + 1);
                brush_->SetColor(ToColor(fg, (first.attr & Faint) ? 0.6f : 1.f));
                IDWriteTextFormat* f = (first.attr & Bold) ? bold_.Get() : regular_.Get();
                dc->DrawText(text.c_str(), static_cast<UINT32>(text.size()), f, {x0, y, x0 + (e - c + 2) * cellW_, y + cellH_}, brush_.Get(),
                             D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
            if (first.attr & Underline) Fill(dc, {x0, y + cellH_ - 2.f, x1, y + cellH_ - 1.f}, ToColor(fg));
            c = e;
        }
    }

    void OnResize() override {
        if (cellW_ <= 0) return;
        const int cols = std::max(10, static_cast<int>((widthDip_ - 2 * kPad) / cellW_));
        const int rows = std::max(3, static_cast<int>((heightDip_ - titleH_ - 6.f - kPad) / cellH_));
        if (cols == cols_ && rows == rows_) return;
        ResizeGrid(cols, rows);
        if (pty_) ResizePseudoConsole(pty_, {static_cast<SHORT>(cols_), static_cast<SHORT>(rows_)});
    }

    void OnTimer(UINT_PTR id) override {
        if (id != kBlinkTimer) return;
        blinkOn_ = !blinkOn_;
        Redraw();
    }

    void OnActivate(bool) override {
        blinkOn_ = true;
        Redraw();
    }

    bool OnMessage(UINT msg, WPARAM, LPARAM, LRESULT& result) override {
        if (msg == Settings::ChangedMessage()) {  // aplikacja Ustawienia zmieniła rozmiar czcionki
            Settings::Reload();
            const float size = static_cast<float>(_wtof(Settings::Get(L"terminalFontSize", L"14").c_str()));
            if (size >= 8.f && size <= 32.f && size != fontSize_) {
                fontSize_ = size;
                MakeFonts();
                cols_ = rows_ = 0;
                ResizeGrid(std::max(10, static_cast<int>((widthDip_ - 2 * kPad) / cellW_)),
                           std::max(3, static_cast<int>((heightDip_ - titleH_ - 6.f - kPad) / cellH_)));
                if (pty_) ResizePseudoConsole(pty_, {static_cast<SHORT>(cols_), static_cast<SHORT>(rows_)});
                Redraw();
            }
            result = 0;
            return true;
        }
        if (msg == WM_PTY_OUTPUT) {
            std::string data;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                data.swap(pending_);
                outputPosted_ = false;
            }
            Feed(data);
            Redraw();
            result = 0;
            return true;
        }
        if (msg == WM_PTY_EXIT) {  // "exit" w powłoce zamyka okno, jak w Linuksie
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            result = 0;
            return true;
        }
        return false;
    }

    // ---------------- Klawiatura ----------------
    bool OnKeyDown(UINT vk) override {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
        if (ctrl && shift && vk == 'C') return CopySelection(), true;
        if (ctrl && (vk == 'V' || (shift && vk == 'V'))) return Paste(), true;
        if (ctrl && vk == 'C' && HasSelection()) return CopySelection(), suppressChar_ = true, true;
        if (ctrl && (vk == VK_OEM_PLUS || vk == VK_ADD)) return Zoom(1.f), true;
        if (ctrl && (vk == VK_OEM_MINUS || vk == VK_SUBTRACT)) return Zoom(-1.f), true;
        if (ctrl && vk == '0') return Zoom(0.f), true;
        if (shift && (vk == VK_PRIOR || vk == VK_NEXT)) {
            ScrollView(vk == VK_PRIOR ? rows_ - 1 : -(rows_ - 1));
            return true;
        }
        if (alt && vk == VK_F4) return false;  // zamknięcie okna — niech obsłuży system
        const int mod = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
        auto csi = [&](const char* final, int num = 0) {
            std::string s = "\x1b[";
            if (num) s += std::to_string(num);
            if (mod > 1) s += (num ? ";" : "1;") + std::to_string(mod);
            s += final;
            Send(s);
        };
        switch (vk) {
        case VK_UP:
        case VK_DOWN:
        case VK_RIGHT:
        case VK_LEFT: {
            const char k = "ABCD"[vk == VK_UP ? 0 : vk == VK_DOWN ? 1 : vk == VK_RIGHT ? 2 : 3];
            if (appCursor_ && mod == 1) Send(std::string("\x1bO") + k);
            else csi(std::string(1, k).c_str());
            return true;
        }
        case VK_HOME: csi("H"); return true;
        case VK_END: csi("F"); return true;
        case VK_INSERT: csi("~", 2); return true;
        case VK_DELETE: csi("~", 3); return true;
        case VK_PRIOR: csi("~", 5); return true;
        case VK_NEXT: csi("~", 6); return true;
        case VK_F1: case VK_F2: case VK_F3: case VK_F4:
            Send(std::string("\x1bO") + "PQRS"[vk - VK_F1]);
            return true;
        case VK_F5: case VK_F6: case VK_F7: case VK_F8: case VK_F9: case VK_F10: case VK_F11: case VK_F12: {
            static const int codes[] = {15, 17, 18, 19, 20, 21, 23, 24};
            csi("~", codes[vk - VK_F5]);
            return true;
        }
        }
        return false;
    }

    void OnChar(wchar_t ch) override {
        if (suppressChar_) {  // Ctrl+C, które skopiowało zaznaczenie
            suppressChar_ = false;
            return;
        }
        if (ch == 0x16 && GetKeyState(VK_CONTROL) < 0) return;  // Ctrl+V obsłużone jako wklejanie
        ClearSelection();
        viewOffset_ = 0;
        if (ch == L'\b') return Send("\x7f");
        if (IS_HIGH_SURROGATE(ch)) {
            highSurrogate_ = ch;
            return;
        }
        std::wstring s;
        if (IS_LOW_SURROGATE(ch) && highSurrogate_) s = {highSurrogate_, ch};
        else s = std::wstring(1, ch);
        highSurrogate_ = 0;
        Send(ToUtf8(s));
    }

    // ---------------- Mysz: zaznaczanie, kopiuj/wklej, historia ----------------
    void OnMouseDown(D2D1_POINT_2F p, bool right) override {
        if (right) {  // jak w Windows Terminal: PPM kopiuje zaznaczenie albo wkleja
            if (HasSelection()) CopySelection();
            else Paste();
            return;
        }
        selecting_ = true;
        selStart_ = selEnd_ = CellAt(p);
        SetCapture(hwnd_);
        Redraw();
    }
    void OnMouseMove(D2D1_POINT_2F p) override {
        if (!selecting_) return;
        selEnd_ = CellAt(p);
        Redraw();
    }
    void OnMouseUp(D2D1_POINT_2F) override {
        if (!selecting_) return;
        selecting_ = false;
        ReleaseCapture();
        if (selStart_ == selEnd_) ClearSelection();
        Redraw();
    }
    void OnDoubleClick(D2D1_POINT_2F p) override {  // zaznacz słowo
        const auto [row, col] = CellAt(p);
        const Line* line = LineAt(row);
        if (!line || col >= static_cast<int>(line->size())) return;
        auto word = [&](int c) { return c >= 0 && c < static_cast<int>(line->size()) && (*line)[static_cast<size_t>(c)].ch != U' '; };
        if (!word(col)) return;
        int a = col, b = col;
        while (word(a - 1)) --a;
        while (word(b + 1)) ++b;
        selStart_ = {row, a};
        selEnd_ = {row, b};
        Redraw();
    }
    void OnWheel(int delta, D2D1_POINT_2F) override {
        if (GetKeyState(VK_CONTROL) < 0) return Zoom(delta > 0 ? 1.f : -1.f);
        ScrollView(delta > 0 ? 3 : -3);
    }

private:
    // ---------------- Czcionka ----------------
    void MakeFonts() {
        ComPtr<IDWriteFontCollection> fonts;
        g_->DWrite()->GetSystemFontCollection(&fonts);
        const wchar_t* family = L"Consolas";
        for (const wchar_t* f : {L"Ubuntu Mono", L"Cascadia Mono", L"Consolas"}) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (fonts && SUCCEEDED(fonts->FindFamilyName(f, &index, &exists)) && exists) {
                family = f;
                break;
            }
        }
        auto make = [&](DWRITE_FONT_WEIGHT w, ComPtr<IDWriteTextFormat>& out) {
            out.Reset();
            g_->DWrite()->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize_, L"en-us", &out);
            if (!out) return;
            out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        };
        make(DWRITE_FONT_WEIGHT_NORMAL, regular_);
        make(DWRITE_FONT_WEIGHT_BOLD, bold_);
        ComPtr<IDWriteTextLayout> layout;
        if (regular_ && SUCCEEDED(g_->DWrite()->CreateTextLayout(L"MMMMMMMMMM", 10, regular_.Get(), 1000.f, 1000.f, &layout))) {
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            cellW_ = m.widthIncludingTrailingWhitespace / 10.f;
            cellH_ = std::ceil(m.height);
        }
    }

    void Zoom(float step) {
        fontSize_ = step == 0.f ? 14.f : std::clamp(fontSize_ + step, 8.f, 32.f);
        Settings::Set(L"terminalFontSize", std::to_wstring(static_cast<int>(fontSize_)));
        MakeFonts();
        cols_ = rows_ = 0;  // wymusza przeliczenie siatki
        ResizeGrid(std::max(10, static_cast<int>((widthDip_ - 2 * kPad) / cellW_)),
                   std::max(3, static_cast<int>((heightDip_ - titleH_ - 6.f - kPad) / cellH_)));
        if (pty_) ResizePseudoConsole(pty_, {static_cast<SHORT>(cols_), static_cast<SHORT>(rows_)});
        Redraw();
    }

    // ---------------- Powłoka w ConPTY ----------------
    bool StartShell() {
        HANDLE inRead = nullptr, outWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite_, nullptr, 0) || !CreatePipe(&outRead_, &outWrite, nullptr, 0)) return false;
        if (cols_ <= 0) ResizeGrid(80, 24);
        if (FAILED(CreatePseudoConsole({static_cast<SHORT>(cols_), static_cast<SHORT>(rows_)}, inRead, outWrite, 0, &pty_))) return false;
        CloseHandle(inRead);
        CloseHandle(outWrite);

        // Prompt jak w Linuksie: zielony user@host, niebieska ścieżka, $
        wchar_t user[256]{}, host[256]{};
        DWORD n = ARRAYSIZE(user);
        GetUserNameW(user, &n);
        n = ARRAYSIZE(host);
        GetComputerNameW(host, &n);
        std::wstring u = user, h = host;
        for (auto* s : {&u, &h}) std::transform(s->begin(), s->end(), s->begin(), ::towlower);
        const std::wstring prompt = L"$E[1;32m" + u + L"@" + h + L"$E[0m:$E[1;34m$P$E[0m$$ ";
        SetEnvironmentVariableW(L"PROMPT", prompt.c_str());
        SetEnvironmentVariableW(L"COLORTERM", L"truecolor");  // programy (Claude Code, git…) wiedzą, że są pełne kolory

        const std::wstring macros = LocalAppDataDir() + L"\\terminal-aliasy.txt";
        if (HANDLE f = CreateFileW(macros.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            f != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(f, kMacros, sizeof(kMacros) - 1, &written, nullptr);
            CloseHandle(f);
        }
        wchar_t comspec[MAX_PATH] = L"cmd.exe";
        GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
        std::wstring cmdline = L"\"" + std::wstring(comspec) + L"\" /k doskey /macrofile=\"" + macros + L"\"";

        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        std::vector<BYTE> attrBuf(attrSize);
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
        if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) ||
            !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pty_, sizeof(pty_), nullptr, nullptr))
            return false;
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        si.lpAttributeList = attrs;
        PROCESS_INFORMATION pi{};
        wchar_t home[MAX_PATH]{};
        GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        const BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                       nullptr, home[0] ? home : nullptr, &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(attrs);
        if (!ok) return false;
        process_ = pi.hProcess;
        CloseHandle(pi.hThread);

        const HWND hwnd = hwnd_;
        reader_ = std::thread([this, hwnd] {
            char buf[16384];
            DWORD got = 0;
            // Diagnostyka: VEYRA_VT_LOG=ścieżka → surowe wyjście konsoli (sekwencje VT) dopisywane do pliku
            wchar_t logPath[MAX_PATH]{};
            FILE* log = nullptr;
            if (GetEnvironmentVariableW(L"VEYRA_VT_LOG", logPath, MAX_PATH)) _wfopen_s(&log, logPath, L"ab");
            while (ReadFile(outRead_, buf, sizeof(buf), &got, nullptr) && got) {
                if (log) fwrite(buf, 1, got, log), fflush(log);
                bool post = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending_.append(buf, got);
                    if (!outputPosted_) post = outputPosted_ = true;
                }
                if (post) PostMessageW(hwnd, WM_PTY_OUTPUT, 0, 0);
            }
            if (log) fclose(log);
        });
        waiter_ = std::thread([this, hwnd] {
            WaitForSingleObject(process_, INFINITE);
            if (!stopping_) PostMessageW(hwnd, WM_PTY_EXIT, 0, 0);
        });
        return true;
    }

    void StopShell() {
        stopping_ = true;
        if (process_) TerminateProcess(process_, 0);
        if (pty_) ClosePseudoConsole(pty_);  // zamyka też potok wyjścia → wątek czytający kończy się
        pty_ = nullptr;
        if (waiter_.joinable()) waiter_.join();
        if (outRead_) CancelIoEx(outRead_, nullptr);
        if (reader_.joinable()) reader_.join();
        for (HANDLE* h : {&inWrite_, &outRead_, &process_})
            if (*h) {
                CloseHandle(*h);
                *h = nullptr;
            }
    }

    void Send(const std::string& s) {
        if (!inWrite_ || s.empty()) return;
        DWORD written = 0;
        WriteFile(inWrite_, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
        blinkOn_ = true;
    }

    // ---------------- Ekran ----------------
    std::vector<Line>& Screen() { return alt_ ? altScreen_ : main_; }
    Line BlankLine() const {
        Line l(static_cast<size_t>(cols_));
        for (Cell& c : l) c.bg = pen_.bg;
        return l;
    }

    void ResizeGrid(int cols, int rows) {
        for (auto* screen : {&main_, &altScreen_}) {
            // Za mało wierszy: górne idą do historii (tylko główny ekran), żeby kursor został widoczny
            while (static_cast<int>(screen->size()) > rows) {
                if (screen == &main_ && cy_ >= rows) {
                    PushScrollback(screen->front());
                    screen->erase(screen->begin());
                    --cy_;
                } else {
                    screen->pop_back();
                }
            }
            while (static_cast<int>(screen->size()) < rows) screen->push_back(Line(static_cast<size_t>(cols)));
            for (Line& l : *screen) l.resize(static_cast<size_t>(cols));
        }
        cols_ = cols;
        rows_ = rows;
        cx_ = std::clamp(cx_, 0, cols_ - 1);
        cy_ = std::clamp(cy_, 0, rows_ - 1);
        top_ = 0;
        bottom_ = rows_ - 1;
        wrapPending_ = false;
    }

    void PushScrollback(const Line& l) {
        if (alt_) return;
        scrollback_.push_back(l);
        if (scrollback_.size() > kScrollback) scrollback_.pop_front();
        else if (viewOffset_ > 0) ++viewOffset_;  // przeglądana historia zostaje na miejscu
    }

    void ScrollUp(int n) {  // w regionie przewijania
        auto& s = Screen();
        for (int i = 0; i < n; ++i) {
            if (top_ == 0 && !alt_) PushScrollback(s[static_cast<size_t>(top_)]);
            s.erase(s.begin() + top_);
            s.insert(s.begin() + bottom_, BlankLine());
        }
    }
    void ScrollDown(int n) {
        auto& s = Screen();
        for (int i = 0; i < n; ++i) {
            s.erase(s.begin() + bottom_);
            s.insert(s.begin() + top_, BlankLine());
        }
    }
    void LineFeed() {
        if (cy_ == bottom_) ScrollUp(1);
        else if (cy_ < rows_ - 1) ++cy_;
    }

    void Put(char32_t ch) {
        if (wrapPending_) {
            cx_ = 0;
            LineFeed();
            wrapPending_ = false;
        }
        Cell& c = Screen()[static_cast<size_t>(cy_)][static_cast<size_t>(cx_)];
        c = pen_;
        c.ch = ch;
        if (cx_ == cols_ - 1) wrapPending_ = true;
        else ++cx_;
    }

    void EraseCells(int row, int from, int to) {  // [from, to)
        Line& l = Screen()[static_cast<size_t>(row)];
        for (int i = std::max(0, from); i < std::min(to, cols_); ++i) {
            l[static_cast<size_t>(i)] = Cell{};
            l[static_cast<size_t>(i)].bg = pen_.bg;
        }
    }

    // ---------------- Parser VT ----------------
    void Feed(const std::string& bytes) {
        for (unsigned char b : bytes) {
            // UTF-8 → znaki
            if (utfNeed_) {
                if ((b & 0xC0) == 0x80) {
                    utfCode_ = (utfCode_ << 6) | (b & 0x3F);
                    if (--utfNeed_ == 0) Process(utfCode_);
                    continue;
                }
                utfNeed_ = 0;
            }
            if (b < 0x80) Process(b);
            else if ((b & 0xE0) == 0xC0) utfCode_ = b & 0x1F, utfNeed_ = 1;
            else if ((b & 0xF0) == 0xE0) utfCode_ = b & 0x0F, utfNeed_ = 2;
            else if ((b & 0xF8) == 0xF0) utfCode_ = b & 0x07, utfNeed_ = 3;
        }
    }

    enum class State { Ground, Escape, EscapeSkip, Csi, Osc, OscEsc };

    void Process(char32_t ch) {
        switch (state_) {
        case State::Ground:
            if (ch == 0x1B) state_ = State::Escape;
            else if (ch < 0x20 || ch == 0x7F) Control(ch);
            else Put(ch);
            return;
        case State::Escape:
            state_ = State::Ground;
            switch (ch) {
            case U'[': state_ = State::Csi, params_.clear(), privateMark_ = 0, intermediate_ = 0; return;
            case U']': state_ = State::Osc, osc_.clear(); return;
            case U'(': case U')': case U'*': case U'+': case U'#': state_ = State::EscapeSkip; return;  // zestaw znaków — ignorujemy
            case U'7': saved_ = {cx_, cy_}, savedPen_ = pen_; return;
            case U'8': cx_ = saved_.first, cy_ = saved_.second, pen_ = savedPen_, wrapPending_ = false; return;
            case U'D': LineFeed(); return;
            case U'E': cx_ = 0, LineFeed(); return;
            case U'M':
                if (cy_ == top_) ScrollDown(1);
                else if (cy_ > 0) --cy_;
                return;
            case U'c': Reset(); return;
            default: return;
            }
        case State::EscapeSkip:
            state_ = State::Ground;
            return;
        case State::Csi:
            if (ch >= U'0' && ch <= U'9') {
                if (params_.empty()) params_.push_back(0);
                params_.back() = params_.back() * 10 + static_cast<int>(ch - U'0');
            } else if (ch == U';' || ch == U':') {
                if (params_.empty()) params_.push_back(0);
                params_.push_back(0);
            } else if (ch == U'?' || ch == U'>' || ch == U'<' || ch == U'=') {
                privateMark_ = static_cast<char>(ch);
            } else if (ch >= 0x20 && ch <= 0x2F) {
                intermediate_ = static_cast<char>(ch);
            } else if (ch >= 0x40 && ch <= 0x7E) {
                state_ = State::Ground;
                Csi(static_cast<char>(ch));
            } else if (ch == 0x1B) {
                state_ = State::Escape;
            }
            return;
        case State::Osc:
            if (ch == 0x07) Osc(), state_ = State::Ground;
            else if (ch == 0x1B) state_ = State::OscEsc;
            else if (osc_.size() < 512) osc_ += ToUtf16(ch);
            return;
        case State::OscEsc:  // ESC \ kończy OSC
            Osc();
            state_ = ch == U'\\' ? State::Ground : State::Escape;
            return;
        }
    }

    void Control(char32_t ch) {
        switch (ch) {
        case U'\r': cx_ = 0, wrapPending_ = false; break;
        case U'\n': case 0x0B: case 0x0C: LineFeed(), wrapPending_ = false; break;
        case U'\b': if (cx_ > 0) --cx_; wrapPending_ = false; break;
        case U'\t': cx_ = std::min(cols_ - 1, (cx_ / 8 + 1) * 8); break;
        default: break;  // BEL i inne — bez efektu
        }
    }

    void Osc() {  // 0;tytuł / 2;tytuł → tytuł okna
        const size_t semi = osc_.find(L';');
        if (semi == std::wstring::npos) return;
        const std::wstring kind = osc_.substr(0, semi);
        if (kind == L"0" || kind == L"2") {
            std::wstring t = osc_.substr(semi + 1);
            // cmd ustawia tytuł na własną ścieżkę ("Administrator: C:\\...\\cmd.exe") — wtedy zostaje "Terminal"
            const bool shellPath = t.size() >= 7 && _wcsicmp(t.c_str() + t.size() - 7, L"cmd.exe") == 0;
            SetTitle(t.empty() || shellPath ? L"Terminal" : t);
        }
    }

    int P(size_t i, int def) const { return i < params_.size() && params_[i] > 0 ? params_[i] : def; }

    void Csi(char final) {
        auto& s = Screen();
        wrapPending_ = wrapPending_ && (final == 'm');
        switch (final) {
        case 'A': cy_ = std::max(cy_ < top_ ? 0 : top_, cy_ - P(0, 1)); break;
        case 'B': cy_ = std::min(cy_ > bottom_ ? rows_ - 1 : bottom_, cy_ + P(0, 1)); break;
        case 'C': cx_ = std::min(cols_ - 1, cx_ + P(0, 1)); break;
        case 'D': cx_ = std::max(0, cx_ - P(0, 1)); break;
        case 'E': cx_ = 0, cy_ = std::min(rows_ - 1, cy_ + P(0, 1)); break;
        case 'F': cx_ = 0, cy_ = std::max(0, cy_ - P(0, 1)); break;
        case 'G': case '`': cx_ = std::clamp(P(0, 1) - 1, 0, cols_ - 1); break;
        case 'd': cy_ = std::clamp(P(0, 1) - 1, 0, rows_ - 1); break;
        case 'H': case 'f':
            cy_ = std::clamp(P(0, 1) - 1, 0, rows_ - 1);
            cx_ = std::clamp(P(1, 1) - 1, 0, cols_ - 1);
            break;
        case 'J': {
            const int mode = params_.empty() ? 0 : params_[0];
            if (mode == 0) {
                EraseCells(cy_, cx_, cols_);
                for (int r = cy_ + 1; r < rows_; ++r) EraseCells(r, 0, cols_);
            } else if (mode == 1) {
                for (int r = 0; r < cy_; ++r) EraseCells(r, 0, cols_);
                EraseCells(cy_, 0, cx_ + 1);
            } else if (mode == 2) {
                for (int r = 0; r < rows_; ++r) EraseCells(r, 0, cols_);
            } else if (mode == 3) {
                scrollback_.clear();
                viewOffset_ = 0;
            }
            break;
        }
        case 'K': {
            const int mode = params_.empty() ? 0 : params_[0];
            if (mode == 0) EraseCells(cy_, cx_, cols_);
            else if (mode == 1) EraseCells(cy_, 0, cx_ + 1);
            else EraseCells(cy_, 0, cols_);
            break;
        }
        case 'X': EraseCells(cy_, cx_, cx_ + P(0, 1)); break;
        case '@': {
            Line& l = s[static_cast<size_t>(cy_)];
            const int n = std::min(P(0, 1), cols_ - cx_);
            l.insert(l.begin() + cx_, static_cast<size_t>(n), Cell{});
            l.resize(static_cast<size_t>(cols_));
            break;
        }
        case 'P': {
            Line& l = s[static_cast<size_t>(cy_)];
            const int n = std::min(P(0, 1), cols_ - cx_);
            l.erase(l.begin() + cx_, l.begin() + cx_ + n);
            l.resize(static_cast<size_t>(cols_));
            break;
        }
        case 'L':
            if (cy_ >= top_ && cy_ <= bottom_)
                for (int i = 0; i < P(0, 1); ++i) {
                    s.erase(s.begin() + bottom_);
                    s.insert(s.begin() + cy_, BlankLine());
                }
            break;
        case 'M':
            if (cy_ >= top_ && cy_ <= bottom_)
                for (int i = 0; i < P(0, 1); ++i) {
                    s.erase(s.begin() + cy_);
                    s.insert(s.begin() + bottom_, BlankLine());
                }
            break;
        case 'S': ScrollUp(P(0, 1)); break;
        case 'T': ScrollDown(P(0, 1)); break;
        case 'm': Sgr(); break;
        case 'r':
            top_ = std::clamp(P(0, 1) - 1, 0, rows_ - 1);
            bottom_ = std::clamp(P(1, rows_) - 1, top_, rows_ - 1);
            cx_ = 0;
            cy_ = 0;
            break;
        case 's': saved_ = {cx_, cy_}; break;
        case 'u': cx_ = saved_.first, cy_ = saved_.second; break;
        case 'n':  // zapytanie o pozycję kursora — ConPTY czeka na odpowiedź przy starcie
            if (P(0, 0) == 6) Send("\x1b[" + std::to_string(cy_ + 1) + ";" + std::to_string(cx_ + 1) + "R");
            else if (P(0, 0) == 5) Send("\x1b[0n");
            break;
        case 'c':
            if (privateMark_ == 0) Send("\x1b[?1;0c");
            break;
        case 'h': case 'l': {
            const bool on = final == 'h';
            if (privateMark_ != '?') break;
            for (int p : params_) {
                if (p == 25) cursorVisible_ = on;
                else if (p == 1) appCursor_ = on;
                else if (p == 1049 || p == 1047 || p == 47) SetAltScreen(on);
            }
            break;
        }
        default: break;
        }
    }

    void Sgr() {
        if (params_.empty()) params_.push_back(0);
        for (size_t i = 0; i < params_.size(); ++i) {
            const int p = params_[i];
            if (p == 0) pen_ = Cell{};
            else if (p == 1) pen_.attr |= Bold;
            else if (p == 2) pen_.attr |= Faint;
            else if (p == 3) pen_.attr |= Italic;
            else if (p == 4) pen_.attr |= Underline;
            else if (p == 7) pen_.attr |= Inverse;
            else if (p == 22) pen_.attr &= ~(Bold | Faint);
            else if (p == 23) pen_.attr &= ~Italic;
            else if (p == 24) pen_.attr &= ~Underline;
            else if (p == 27) pen_.attr &= ~Inverse;
            else if (p >= 30 && p <= 37) pen_.fg = kPalette[p - 30];
            else if (p == 39) pen_.fg = kDefault;
            else if (p >= 40 && p <= 47) pen_.bg = kPalette[p - 40];
            else if (p == 49) pen_.bg = kDefault;
            else if (p >= 90 && p <= 97) pen_.fg = kPalette[p - 90 + 8];
            else if (p >= 100 && p <= 107) pen_.bg = kPalette[p - 100 + 8];
            else if ((p == 38 || p == 48) && i + 1 < params_.size()) {
                uint32_t color = kDefault;
                if (params_[i + 1] == 5 && i + 2 < params_.size()) {
                    color = Color256(std::clamp(params_[i + 2], 0, 255));
                    i += 2;
                } else if (params_[i + 1] == 2 && i + 4 < params_.size()) {
                    color = (std::clamp(params_[i + 2], 0, 255) << 16) | (std::clamp(params_[i + 3], 0, 255) << 8) | std::clamp(params_[i + 4], 0, 255);
                    i += 4;
                }
                (p == 38 ? pen_.fg : pen_.bg) = color;
            }
        }
    }

    void SetAltScreen(bool on) {
        if (on == alt_) return;
        if (on) {
            savedMain_ = {cx_, cy_};
            altScreen_.assign(static_cast<size_t>(rows_), Line(static_cast<size_t>(cols_)));
            alt_ = true;
        } else {
            alt_ = false;
            cx_ = savedMain_.first;
            cy_ = savedMain_.second;
        }
        viewOffset_ = 0;
    }

    void Reset() {
        pen_ = Cell{};
        cx_ = cy_ = 0;
        top_ = 0;
        bottom_ = rows_ - 1;
        alt_ = false;
        for (Line& l : main_) l.assign(static_cast<size_t>(cols_), Cell{});
    }

    // ---------------- Historia, zaznaczenie, schowek ----------------
    void ScrollView(int lines) {
        viewOffset_ = std::clamp(viewOffset_ + lines, 0, static_cast<int>(scrollback_.size()));
        Redraw();
    }

    // Wiersz bezwzględny: 0..historia-1 = historia, dalej ekran
    const Line* LineAt(int abs) const {
        const int sb = static_cast<int>(scrollback_.size());
        if (abs < 0) return nullptr;
        if (abs < sb) return &scrollback_[static_cast<size_t>(abs)];
        const auto& screen = alt_ ? altScreen_ : main_;
        return abs - sb < static_cast<int>(screen.size()) ? &screen[static_cast<size_t>(abs - sb)] : nullptr;
    }
    std::pair<int, int> CellAt(D2D1_POINT_2F p) const {
        const int r = std::clamp(static_cast<int>((p.y - titleH_ - 6.f) / cellH_), 0, rows_ - 1);
        const int c = std::clamp(static_cast<int>((p.x - kPad) / cellW_ + 0.5f), 0, cols_);
        return {static_cast<int>(scrollback_.size()) - std::min<int>(viewOffset_, static_cast<int>(scrollback_.size())) + r, c};
    }
    bool HasSelection() const { return hasSel(); }
    bool hasSel() const { return selStart_ != selEnd_ && selStart_.first >= 0; }
    void ClearSelection() { selStart_ = selEnd_ = {-1, -1}; }
    bool Selected(int row, int col) const {
        if (!hasSel()) return false;
        auto a = selStart_, b = selEnd_;
        if (b < a) std::swap(a, b);
        if (row < a.first || row > b.first) return false;
        if (row == a.first && col < a.second) return false;
        if (row == b.first && col >= b.second) return false;
        return true;
    }
    void CopySelection() {
        if (!hasSel()) return;
        auto a = selStart_, b = selEnd_;
        if (b < a) std::swap(a, b);
        std::wstring out;
        for (int r = a.first; r <= b.first; ++r) {
            const Line* l = LineAt(r);
            if (!l) continue;
            std::wstring row;
            const int from = r == a.first ? a.second : 0, to = r == b.first ? b.second : static_cast<int>(l->size());
            for (int c = from; c < std::min(to, static_cast<int>(l->size())); ++c) row += ToUtf16((*l)[static_cast<size_t>(c)].ch);
            row.erase(row.find_last_not_of(L' ') + 1);
            out += row;
            if (r != b.first) out += L"\r\n";
        }
        if (OpenClipboard(hwnd_)) {
            EmptyClipboard();
            if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (out.size() + 1) * sizeof(wchar_t))) {
                memcpy(GlobalLock(mem), out.c_str(), (out.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(mem);
                SetClipboardData(CF_UNICODETEXT, mem);
            }
            CloseClipboard();
        }
        ClearSelection();
        Redraw();
    }
    void Paste() {
        if (!OpenClipboard(hwnd_)) return;
        std::wstring s;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) s = p;
            GlobalUnlock(h);
        }
        CloseClipboard();
        std::wstring clean;  // nowe linie jako Enter
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') continue;
            clean += s[i] == L'\n' ? L'\r' : s[i];
        }
        viewOffset_ = 0;
        Send(ToUtf8(clean));
    }

    // Czcionka i siatka
    ComPtr<IDWriteTextFormat> regular_, bold_;
    float fontSize_ = 14.f, cellW_ = 8.f, cellH_ = 17.f;
    int cols_ = 0, rows_ = 0;

    // Stan ekranu
    std::vector<Line> main_, altScreen_;
    std::deque<Line> scrollback_;
    bool alt_ = false;
    int cx_ = 0, cy_ = 0, top_ = 0, bottom_ = 0;
    bool wrapPending_ = false, cursorVisible_ = true, appCursor_ = false, blinkOn_ = true;
    Cell pen_, savedPen_;
    std::pair<int, int> saved_{0, 0}, savedMain_{0, 0};
    int viewOffset_ = 0;

    // Parser
    State state_ = State::Ground;
    std::vector<int> params_;
    char privateMark_ = 0, intermediate_ = 0;
    std::wstring osc_;
    char32_t utfCode_ = 0;
    int utfNeed_ = 0;
    wchar_t highSurrogate_ = 0;

    // Zaznaczenie
    std::pair<int, int> selStart_{-1, -1}, selEnd_{-1, -1};
    bool selecting_ = false, suppressChar_ = false;

    // ConPTY
    HPCON pty_ = nullptr;
    HANDLE inWrite_ = nullptr, outRead_ = nullptr, process_ = nullptr;
    std::thread reader_, waiter_;
    std::mutex mutex_;
    std::string pending_;
    bool outputPosted_ = false;
    std::atomic<bool> stopping_{false};
};
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Graphics graphics;
    if (!graphics.Init()) return 1;
    auto terminal = std::make_unique<Terminal>();
    if (!terminal->Init(inst, graphics)) {
        MessageBoxW(nullptr, L"Nie udało się uruchomić powłoki (ConPTY wymaga Windows 10 1809+).", L"Terminal", MB_ICONERROR);
        return 1;
    }
    const int code = AppWindow::RunMessageLoop();
    terminal.reset();
    return code;
}
