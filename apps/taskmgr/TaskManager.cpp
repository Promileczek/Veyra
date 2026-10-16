// Menedżer zadań Veyry — wygląd spójny z resztą (grafit #1C1C21, akcent #61B3FF, płasko, Segoe UI). Bez przezroczystości.
// Zakładki: Procesy (lista z CPU% i RAM, zakończ zadanie), Wydajność (wykresy CPU/RAM/dysk/sieć), Uruchamianie
// (programy autostartu z HKCU/HKLM Run, włącz/wyłącz). Odświeżanie co 1,5 s.
// Pod systemowy Menedżer zadań podstawia się przez IFEO taskmgr.exe (jak Kalkulator) — robi to instalator/Ustawienia.
#include "../../src/core/AppWindow.h"
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
const D2D1_COLOR_F kBody = D2D1::ColorF(0x1C1C21), kAccent = D2D1::ColorF(0.38f, 0.70f, 1.f), kOnAccent = D2D1::ColorF(0x0B1A26);
D2D1_COLOR_F White(float a) { return D2D1::ColorF(1.f, 1.f, 1.f, a); }
constexpr float kPad = 16.f, kTabH = 40.f, kRowH = 30.f;
constexpr size_t kHistory = 60;  // próbek w wykresach

std::wstring FormatMB(double bytes) {
    wchar_t buf[32];
    if (bytes >= 1024.0 * 1024 * 1024) swprintf_s(buf, L"%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else swprintf_s(buf, L"%.0f MB", bytes / (1024.0 * 1024));
    return buf;
}
std::wstring FormatRate(double bps) {
    wchar_t buf[32];
    if (bps >= 1024.0 * 1024) swprintf_s(buf, L"%.1f MB/s", bps / (1024.0 * 1024));
    else swprintf_s(buf, L"%.0f KB/s", bps / 1024.0);
    return buf;
}

struct Proc {
    DWORD pid = 0;
    std::wstring name;
    double cpu = 0.0;      // 0–1 (udział w całym procesorze)
    double ramBytes = 0.0;
    ULONGLONG lastCpu = 0;  // suma kernel+user z poprzedniej próbki
    bool own = false;       // proces Veyry
};

struct StartupEntry {
    std::wstring name, command;
    HKEY root;           // HKCU / HKLM
    bool approved = true;  // włączony (StartupApproved)
};

enum class Tab { Processes, Performance, Startup };
}  // namespace

class TaskManager : public AppWindow {
public:
    bool Init(HINSTANCE inst, Graphics& g) {
        Options o;
        o.className = L"ExploderTaskManager";
        o.title = L"Menedżer zadań";
        o.width = 760.f;
        o.height = 560.f;
        o.minWidth = 560.f;
        o.minHeight = 400.f;
        if (!Create(inst, g, o)) return false;
        tabFormat_ = MakeFormat(L"Segoe UI", 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        rowFormat_ = MakeFormat(L"Segoe UI", 12.5f);
        numFormat_ = MakeFormat(L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
        headFormat_ = MakeFormat(L"Segoe UI", 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        bigFormat_ = MakeFormat(L"Segoe UI", 26.f, DWRITE_FONT_WEIGHT_LIGHT);
        glyphFormat_ = MakeFormat(L"Segoe MDL2 Assets", 14.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        numCpuLast_ = 0;
        cpuHist_.assign(kHistory, 0.f);
        ramHist_.assign(kHistory, 0.f);
        netHist_.assign(kHistory, 0.f);
        LoadStartup();
        SampleSystem();
        Sample();
        SetTimer(hwnd_, kRefreshTimer, 1500, nullptr);
        Redraw();
        return true;
    }

protected:
    static constexpr UINT_PTR kRefreshTimer = 10;

    void OnTimer(UINT_PTR id) override {
        if (id != kRefreshTimer) return;
        SampleSystem();
        Sample();
        PushHistory();
        Redraw();
    }

    void Paint(ID2D1DeviceContext* dc) override {
        dc->Clear(kBody);
        PaintTabs(dc);
        switch (tab_) {
        case Tab::Processes: PaintProcesses(dc); break;
        case Tab::Performance: PaintPerformance(dc); break;
        case Tab::Startup: PaintStartup(dc); break;
        }
    }

    // ---------------- Zakładki ----------------
    void PaintTabs(ID2D1DeviceContext* dc) {
        const wchar_t* names[] = {L"Procesy", L"Wydajność", L"Uruchamianie"};
        float x = kPad;
        for (int i = 0; i < 3; ++i) {
            const float w = 130.f;
            const D2D1_RECT_F r{x, titleH_ + 4.f, x + w, titleH_ + kTabH};
            const bool sel = static_cast<int>(tab_) == i;
            if (sel) {
                Fill(dc, r, White(0.08f), 8.f);
                Fill(dc, {r.left + 10.f, r.bottom - 3.f, r.right - 10.f, r.bottom - 1.f}, kAccent, 1.f);
            } else if (hoverTab_ == i) {
                Fill(dc, r, White(0.04f), 8.f);
            }
            Text(dc, names[i], tabFormat_.Get(), r, sel ? White(0.95f) : White(0.6f));
            x += w + 6.f;
        }
        Fill(dc, {kPad, titleH_ + kTabH + 2.f, widthDip_ - kPad, titleH_ + kTabH + 3.f}, White(0.08f));
    }

    float ContentTop() const { return titleH_ + kTabH + 12.f; }

    // ---------------- Procesy ----------------
    void PaintProcesses(ID2D1DeviceContext* dc) {
        const float L = kPad, R = widthDip_ - kPad, top = ContentTop();
        const float cpuX = R - 230.f, ramX = R - 120.f;
        Text(dc, L"Nazwa", headFormat_.Get(), {L + 6.f, top, cpuX, top + 18.f}, White(0.5f));
        Text(dc, L"CPU", numFormat_.Get(), {cpuX, top, cpuX + 90.f, top + 18.f}, White(0.5f));
        Text(dc, L"Pamięć", numFormat_.Get(), {ramX, top, R - 6.f, top + 18.f}, White(0.5f));

        const float listTop = top + 24.f, listBottom = heightDip_ - 56.f;
        dc->PushAxisAlignedClip({L, listTop, R, listBottom}, D2D1_ANTIALIAS_MODE_ALIASED);
        float y = listTop - scroll_;
        for (size_t i = 0; i < procs_.size(); ++i, y += kRowH) {
            if (y + kRowH < listTop || y > listBottom) continue;
            const Proc& p = procs_[i];
            const D2D1_RECT_F row{L, y, R, y + kRowH};
            if (static_cast<int>(i) == selected_) Fill(dc, row, kAccent, 6.f);
            else if (static_cast<int>(i) == hoverRow_) Fill(dc, row, White(0.05f), 6.f);
            const bool sel = static_cast<int>(i) == selected_;
            const D2D1_COLOR_F col = sel ? kOnAccent : White(0.9f);
            Text(dc, p.name, rowFormat_.Get(), {L + 10.f, y, cpuX - 6.f, y + kRowH}, col);
            wchar_t cpu[16];
            swprintf_s(cpu, L"%.1f%%", p.cpu * 100.0);
            Text(dc, cpu, numFormat_.Get(), {cpuX, y, cpuX + 90.f, y + kRowH}, sel ? kOnAccent : (p.cpu > 0.1 ? kAccent : White(0.8f)));
            Text(dc, FormatMB(p.ramBytes), numFormat_.Get(), {ramX, y, R - 10.f, y + kRowH}, col);
        }
        dc->PopAxisAlignedClip();

        // Dół: przycisk „Zakończ zadanie”
        const D2D1_RECT_F btn{R - 160.f, heightDip_ - 46.f, R, heightDip_ - 14.f};
        endRect_ = btn;
        const bool can = selected_ >= 0 && selected_ < static_cast<int>(procs_.size()) && !procs_[selected_].own;
        Fill(dc, btn, can ? (hoverEnd_ ? D2D1::ColorF(1.f, 0.5f, 0.46f) : D2D1::ColorF(0.86f, 0.3f, 0.28f)) : White(0.08f), 8.f);
        Text(dc, L"Zakończ zadanie", tabFormat_.Get(), btn, can ? D2D1::ColorF(1.f, 1.f, 1.f) : White(0.3f));
    }

    // ---------------- Wydajność ----------------
    void PaintPerformance(ID2D1DeviceContext* dc) {
        const float L = kPad, R = widthDip_ - kPad, top = ContentTop();
        const float gap = 12.f, gh = (heightDip_ - top - kPad - 2 * gap) / 3.f;
        struct Graph {
            const wchar_t* name;
            std::wstring value;
            const std::vector<float>* hist;
            D2D1_COLOR_F color;
        };
        wchar_t cpu[32];
        swprintf_s(cpu, L"%.0f%%", sysCpu_ * 100.0);
        const Graph graphs[] = {
            {L"Procesor", cpu, &cpuHist_, kAccent},
            {L"Pamięć", FormatMB(ramUsed_) + L" / " + FormatMB(ramTotal_), &ramHist_, D2D1::ColorF(0.55f, 0.78f, 0.45f)},
            {L"Sieć", L"↓ " + FormatRate(netDown_) + L"   ↑ " + FormatRate(netUp_), &netHist_, D2D1::ColorF(0.7f, 0.55f, 0.95f)},
        };
        for (int i = 0; i < 3; ++i) {
            const float y = top + i * (gh + gap);
            const D2D1_RECT_F r{L, y, R, y + gh};
            Fill(dc, r, White(0.04f), 12.f);
            brush_->SetColor(White(0.08f));
            dc->DrawRoundedRectangle(D2D1::RoundedRect({r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f}, 12.f, 12.f), brush_.Get(), 1.f);
            Text(dc, graphs[i].name, tabFormat_.Get(), {r.left + 14.f, r.top + 10.f, r.left + 200.f, r.top + 32.f}, White(0.9f));
            Text(dc, graphs[i].value, numFormat_.Get(), {r.right - 260.f, r.top + 10.f, r.right - 14.f, r.top + 32.f}, graphs[i].color);
            DrawGraph(dc, {r.left + 14.f, r.top + 40.f, r.right - 14.f, r.bottom - 12.f}, *graphs[i].hist, graphs[i].color);
        }
    }

    void DrawGraph(ID2D1DeviceContext* dc, const D2D1_RECT_F& r, const std::vector<float>& hist, const D2D1_COLOR_F& color) {
        if (hist.size() < 2) return;
        const float w = r.right - r.left, h = r.bottom - r.top;
        ComPtr<ID2D1Factory> factory;
        dc->GetFactory(&factory);
        ComPtr<ID2D1PathGeometry> line, area;
        if (FAILED(factory->CreatePathGeometry(&line)) || FAILED(factory->CreatePathGeometry(&area))) return;
        auto px = [&](size_t i) { return r.left + w * i / (hist.size() - 1); };
        auto py = [&](float v) { return r.bottom - h * std::clamp(v, 0.f, 1.f); };
        ComPtr<ID2D1GeometrySink> ls, as;
        if (SUCCEEDED(area->Open(&as))) {
            as->BeginFigure({px(0), r.bottom}, D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 0; i < hist.size(); ++i) as->AddLine({px(i), py(hist[i])});
            as->AddLine({px(hist.size() - 1), r.bottom});
            as->EndFigure(D2D1_FIGURE_END_CLOSED);
            as->Close();
            brush_->SetColor(D2D1::ColorF(color.r, color.g, color.b, 0.15f));
            dc->FillGeometry(area.Get(), brush_.Get());
        }
        if (SUCCEEDED(line->Open(&ls))) {
            ls->BeginFigure({px(0), py(hist[0])}, D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < hist.size(); ++i) ls->AddLine({px(i), py(hist[i])});
            ls->EndFigure(D2D1_FIGURE_END_OPEN);
            ls->Close();
            brush_->SetColor(color);
            dc->DrawGeometry(line.Get(), brush_.Get(), 1.5f);
        }
    }

    // ---------------- Uruchamianie ----------------
    void PaintStartup(ID2D1DeviceContext* dc) {
        const float L = kPad, R = widthDip_ - kPad, top = ContentTop();
        Text(dc, L"Programy uruchamiane przy starcie Windows", headFormat_.Get(), {L + 6.f, top, R, top + 18.f}, White(0.5f));
        const float listTop = top + 26.f, listBottom = heightDip_ - 16.f;
        dc->PushAxisAlignedClip({L, listTop, R, listBottom}, D2D1_ANTIALIAS_MODE_ALIASED);
        float y = listTop - scroll_;
        for (size_t i = 0; i < startup_.size(); ++i, y += kRowH + 4.f) {
            if (y + kRowH < listTop || y > listBottom) continue;
            const StartupEntry& e = startup_[i];
            const D2D1_RECT_F row{L, y, R, y + kRowH};
            if (static_cast<int>(i) == hoverStartup_) Fill(dc, row, White(0.05f), 6.f);
            Text(dc, e.name, rowFormat_.Get(), {L + 12.f, y, R - 110.f, y + kRowH}, White(0.9f));
            // Przełącznik włącz/wyłącz
            const D2D1_RECT_F sw{R - 86.f, y + 5.f, R - 42.f, y + kRowH - 5.f};
            Fill(dc, sw, e.approved ? kAccent : White(0.12f), (kRowH - 10.f) / 2.f);
            const float kr = (kRowH - 10.f) / 2.f - 3.f;
            const float cx = e.approved ? sw.right - kr - 3.f : sw.left + kr + 3.f;
            brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f));
            dc->FillEllipse(D2D1::Ellipse({cx, (sw.top + sw.bottom) / 2.f}, kr, kr), brush_.Get());
            Text(dc, e.approved ? L"wł." : L"wył.", numFormat_.Get(), {R - 38.f, y, R - 6.f, y + kRowH}, White(0.6f));
        }
        dc->PopAxisAlignedClip();
    }

    // ---------------- Dane ----------------
    void Sample() {
        // Czas całego procesora między próbkami (do udziału każdego procesu)
        FILETIME idle{}, kern{}, user{};
        ULONGLONG sysNow = 0;
        if (GetSystemTimes(&idle, &kern, &user))
            sysNow = (ToU64(kern) + ToU64(user));
        const ULONGLONG sysDelta = numCpuLast_ && sysNow > numCpuLast_ ? sysNow - numCpuLast_ : 0;
        numCpuLast_ = sysNow;

        std::map<DWORD, ULONGLONG> prevCpu;
        for (const Proc& p : procs_) prevCpu[p.pid] = p.lastCpu;
        const std::wstring keep = selected_ >= 0 && selected_ < static_cast<int>(procs_.size()) ? procs_[selected_].name : L"";

        std::vector<Proc> next;
        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{sizeof(pe)};
            for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
                if (pe.th32ProcessID == 0) continue;
                Proc p;
                p.pid = pe.th32ProcessID;
                p.name = FriendlyName(pe.szExeFile);
                p.own = _wcsnicmp(pe.szExeFile, L"exploder", 8) == 0;
                if (const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid)) {
                    FILETIME c{}, e{}, k{}, u{};
                    if (GetProcessTimes(h, &c, &e, &k, &u)) {
                        p.lastCpu = ToU64(k) + ToU64(u);
                        if (auto it = prevCpu.find(p.pid); it != prevCpu.end() && sysDelta && p.lastCpu >= it->second)
                            p.cpu = static_cast<double>(p.lastCpu - it->second) / sysDelta;
                    }
                    PROCESS_MEMORY_COUNTERS mem{sizeof(mem)};
                    if (GetProcessMemoryInfo(h, &mem, sizeof(mem))) p.ramBytes = static_cast<double>(mem.WorkingSetSize);
                    CloseHandle(h);
                }
                next.push_back(std::move(p));
            }
            CloseHandle(snap);
        }
        std::sort(next.begin(), next.end(), [](const Proc& a, const Proc& b) {
            if (a.cpu != b.cpu) return a.cpu > b.cpu;
            return a.ramBytes > b.ramBytes;
        });
        procs_ = std::move(next);
        // Utrzymaj zaznaczenie na tym samym procesie
        selected_ = -1;
        if (!keep.empty())
            for (size_t i = 0; i < procs_.size(); ++i)
                if (procs_[i].name == keep) {
                    selected_ = static_cast<int>(i);
                    break;
                }
    }

    // Metryki całego systemu: CPU (udział niebezczynny), RAM (GlobalMemoryStatusEx), sieć (suma kart, B/s)
    void SampleSystem() {
        FILETIME idle{}, kern{}, user{};
        if (GetSystemTimes(&idle, &kern, &user)) {
            const ULONGLONG i = ToU64(idle), total = ToU64(kern) + ToU64(user);
            if (lastSysIdle_ && total > lastSysTotal_)
                sysCpu_ = std::clamp(1.0 - static_cast<double>(i - lastSysIdle_) / (total - lastSysTotal_), 0.0, 1.0);
            lastSysIdle_ = i;
            lastSysTotal_ = total;
        }
        MEMORYSTATUSEX mem{sizeof(mem)};
        if (GlobalMemoryStatusEx(&mem)) {
            ramTotal_ = static_cast<double>(mem.ullTotalPhys);
            ramUsed_ = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys);
        }
        MIB_IF_TABLE2* table = nullptr;
        if (GetIfTable2(&table) == NO_ERROR && table) {
            ULONG64 in = 0, out = 0;
            for (ULONG k = 0; k < table->NumEntries; ++k) {
                const MIB_IF_ROW2& row = table->Table[k];
                if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) continue;
                in += row.InOctets;
                out += row.OutOctets;
            }
            FreeMibTable(table);
            const ULONGLONG now = GetTickCount64();
            if (lastNetTick_ && now > lastNetTick_) {
                const double sec = (now - lastNetTick_) / 1000.0;
                netDown_ = in >= lastIn_ ? (in - lastIn_) / sec : 0.0;
                netUp_ = out >= lastOut_ ? (out - lastOut_) / sec : 0.0;
            }
            lastIn_ = in;
            lastOut_ = out;
            lastNetTick_ = now;
        }
    }

    void PushHistory() {
        auto push = [](std::vector<float>& h, float v) {
            h.push_back(v);
            if (h.size() > kHistory) h.erase(h.begin());
        };
        push(cpuHist_, static_cast<float>(sysCpu_));
        push(ramHist_, ramTotal_ > 0 ? static_cast<float>(ramUsed_ / ramTotal_) : 0.f);
        push(netHist_, std::clamp(static_cast<float>((netDown_ + netUp_) / 5e6), 0.f, 1.f));  // skala do ~5 MB/s
    }

    void LoadStartup() {
        startup_.clear();
        ReadRunKey(HKEY_CURRENT_USER);
        ReadRunKey(HKEY_LOCAL_MACHINE);
    }
    void ReadRunKey(HKEY root) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &key) != ERROR_SUCCESS) return;
        for (DWORD i = 0;; ++i) {
            wchar_t name[256];
            DWORD nameLen = ARRAYSIZE(name), type = 0, size = 0;
            if (RegEnumValueW(key, i, name, &nameLen, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) break;
            if (type != REG_SZ && type != REG_EXPAND_SZ) continue;
            std::wstring cmd(size / sizeof(wchar_t) + 1, L'\0');
            DWORD cb = static_cast<DWORD>(cmd.size() * sizeof(wchar_t));
            nameLen = ARRAYSIZE(name);
            if (RegEnumValueW(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(cmd.data()), &cb) != ERROR_SUCCESS) continue;
            cmd.resize(wcslen(cmd.c_str()));
            startup_.push_back({name, cmd, root, StartupApproved(root, name)});
        }
        RegCloseKey(key);
    }
    // StartupApproved: Menedżer zadań Windows zapisuje tu wyłączenie (pierwszy bajt 2/6 = wyłączony)
    bool StartupApproved(HKEY root, const std::wstring& name) {
        BYTE data[16]{};
        DWORD cb = sizeof(data), type = 0;
        if (RegGetValueW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", name.c_str(),
                         RRF_RT_REG_BINARY, &type, data, &cb) != ERROR_SUCCESS || cb == 0)
            return true;
        return (data[0] & 1) == 0;  // bit 0 ustawiony = wyłączony
    }
    void ToggleStartup(size_t i) {
        if (i >= startup_.size()) return;
        StartupEntry& e = startup_[i];
        const bool enable = !e.approved;
        HKEY key = nullptr;
        if (RegCreateKeyExW(e.root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", 0, nullptr, 0,
                            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return;
        BYTE data[12]{};
        data[0] = enable ? 2 : 3;  // 2 = włączony, 3 = wyłączony (bit 0)
        RegSetValueExW(key, e.name.c_str(), 0, REG_BINARY, data, sizeof(data));
        RegCloseKey(key);
        e.approved = enable;
        Redraw();
    }

    static ULONGLONG ToU64(const FILETIME& ft) { return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }

    // Nazwa przyjazna: bez ".exe", pierwsza litera wielka
    static std::wstring FriendlyName(const std::wstring& exe) {
        std::wstring n = exe;
        if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".exe") == 0) n.resize(n.size() - 4);
        if (!n.empty()) n[0] = static_cast<wchar_t>(std::towupper(n[0]));
        return n;
    }

    // ---------------- Mysz ----------------
    void OnMouseMove(D2D1_POINT_2F p) override {
        int tab = -1;
        float x = kPad;
        for (int i = 0; i < 3; ++i, x += 136.f)
            if (p.x >= x && p.x < x + 130.f && p.y >= titleH_ + 4.f && p.y < titleH_ + kTabH) tab = i;
        const int row = tab_ == Tab::Processes ? RowAt(p, ContentTop() + 24.f, kRowH, procs_.size()) : -1;
        const int su = tab_ == Tab::Startup ? RowAt(p, ContentTop() + 26.f, kRowH + 4.f, startup_.size()) : -1;
        const bool end = Inside(endRect_, p);
        if (tab != hoverTab_ || row != hoverRow_ || su != hoverStartup_ || end != hoverEnd_) {
            hoverTab_ = tab;
            hoverRow_ = row;
            hoverStartup_ = su;
            hoverEnd_ = end;
            Redraw();
        }
    }
    int RowAt(D2D1_POINT_2F p, float listTop, float rowH, size_t count) const {
        if (p.x < kPad || p.x > widthDip_ - kPad || p.y < listTop || p.y > heightDip_ - 14.f) return -1;
        const int i = static_cast<int>((p.y - listTop + scroll_) / rowH);
        return i >= 0 && i < static_cast<int>(count) ? i : -1;
    }

    void OnMouseDown(D2D1_POINT_2F p, bool) override {
        float x = kPad;
        for (int i = 0; i < 3; ++i, x += 136.f)
            if (p.x >= x && p.x < x + 130.f && p.y >= titleH_ + 4.f && p.y < titleH_ + kTabH) {
                if (tab_ != static_cast<Tab>(i)) {
                    tab_ = static_cast<Tab>(i);
                    scroll_ = 0.f;
                    selected_ = -1;
                    Redraw();
                }
                return;
            }
        if (tab_ == Tab::Processes) {
            if (Inside(endRect_, p)) return EndSelected();
            const int row = RowAt(p, ContentTop() + 24.f, kRowH, procs_.size());
            if (row != selected_) {
                selected_ = row;
                Redraw();
            }
        } else if (tab_ == Tab::Startup) {
            const int row = RowAt(p, ContentTop() + 26.f, kRowH + 4.f, startup_.size());
            if (row >= 0) ToggleStartup(static_cast<size_t>(row));
        }
    }
    void OnDoubleClick(D2D1_POINT_2F p) override {
        if (tab_ != Tab::Processes) return;
        const int row = RowAt(p, ContentTop() + 24.f, kRowH, procs_.size());
        if (row >= 0) {  // dwuklik: pokaż plik procesu? Nie — po prostu zaznacz
            selected_ = row;
            Redraw();
        }
    }
    void OnWheel(int delta, D2D1_POINT_2F) override {
        const size_t count = tab_ == Tab::Processes ? procs_.size() : tab_ == Tab::Startup ? startup_.size() : 0;
        const float rowH = tab_ == Tab::Startup ? kRowH + 4.f : kRowH;
        const float max = std::max(0.f, count * rowH - (heightDip_ - ContentTop() - 70.f));
        scroll_ = std::clamp(scroll_ - delta / 120.f * rowH * 2.f, 0.f, max);
        Redraw();
    }
    bool OnKeyDown(UINT vk) override {
        if (vk == VK_DELETE && tab_ == Tab::Processes) return EndSelected(), true;
        return false;
    }

    void EndSelected() {
        if (selected_ < 0 || selected_ >= static_cast<int>(procs_.size())) return;
        const Proc& p = procs_[selected_];
        if (p.own) return;  // własnych procesów Veyry stąd nie zabijamy
        if (const HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p.pid)) {
            TerminateProcess(h, 1);
            CloseHandle(h);
        }
        Sample();
        Redraw();
    }

private:
    Tab tab_ = Tab::Processes;
    int hoverTab_ = -1, hoverRow_ = -1, hoverStartup_ = -1, selected_ = -1;
    bool hoverEnd_ = false;
    float scroll_ = 0.f;
    D2D1_RECT_F endRect_{};
    std::vector<Proc> procs_;
    std::vector<StartupEntry> startup_;
    std::vector<float> cpuHist_, ramHist_, netHist_;
    ULONGLONG numCpuLast_ = 0;
    double sysCpu_ = 0.0, ramUsed_ = 0.0, ramTotal_ = 0.0, netDown_ = 0.0, netUp_ = 0.0;
    ULONGLONG lastSysIdle_ = 0, lastSysTotal_ = 0, lastNetTick_ = 0;
    ULONG64 lastIn_ = 0, lastOut_ = 0;
    ComPtr<IDWriteTextFormat> tabFormat_, rowFormat_, numFormat_, headFormat_, bigFormat_, glyphFormat_;
};

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Graphics graphics;
    if (!graphics.Init()) return 1;
    auto tm = std::make_unique<TaskManager>();
    if (!tm->Init(inst, graphics)) return 1;
    const int code = AppWindow::RunMessageLoop();
    tm.reset();
    return code;
}
