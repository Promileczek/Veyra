#include "SystemStats.h"
#include "../taskbar/AppIcons.h"
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <winioctl.h>
#include <algorithm>
#include <cstdio>

namespace {
ULONGLONG ToU64(const FILETIME& ft) { return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }
constexpr DWORD kIdleLimitMs = 3 * 60 * 1000;  // dłuższa bezczynność nie liczy się do czasu w aplikacji
}  // namespace

SystemStats::SystemStats() {
    for (int n = 0; n < 16; ++n) {  // dostęp 0 wystarcza do odczytu liczników
        const std::wstring name = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
        const HANDLE h = CreateFileW(name.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) disks_.push_back({h});
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    day_ = st.wDay;
    LoadUsage();
    lastTick_ = lastSave_ = GetTickCount64();
    Sample();
}

SystemStats::~SystemStats() {
    SaveUsage();
    for (Disk& d : disks_) CloseHandle(d.handle);
}

void SystemStats::Sample() {
    const ULONGLONG now = GetTickCount64();
    const double elapsed = (now - lastTick_) / 1000.0;
    lastTick_ = now;
    uptimeMs = now;

    // Procesor
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const ULONGLONG i = ToU64(idle), k = ToU64(kernel), u = ToU64(user);
        const ULONGLONG total = (k - lastKernel_) + (u - lastUser_);  // czas jądra zawiera bezczynność
        if (lastKernel_ && total) cpu = std::clamp(1.0 - static_cast<double>(i - lastIdle_) / total, 0.0, 1.0);
        lastIdle_ = i;
        lastKernel_ = k;
        lastUser_ = u;
    }

    // Pamięć
    MEMORYSTATUSEX mem{sizeof(mem)};
    if (GlobalMemoryStatusEx(&mem)) {
        ramTotal = mem.ullTotalPhys / 1073741824.0;
        ramUsed = (mem.ullTotalPhys - mem.ullAvailPhys) / 1073741824.0;
    }

    // Dysk: aktywność (1 − udział bezczynności), odczyt/zapis, wolne miejsce na C:
    double busiest = 0.0, read = 0.0, written = 0.0;
    for (Disk& d : disks_) {
        DISK_PERFORMANCE perf{};
        DWORD bytes = 0;
        if (!DeviceIoControl(d.handle, IOCTL_DISK_PERFORMANCE, nullptr, 0, &perf, sizeof(perf), &bytes, nullptr)) continue;
        if (d.query && perf.QueryTime.QuadPart > d.query) {
            const double idleShare = static_cast<double>(perf.IdleTime.QuadPart - d.idle) / (perf.QueryTime.QuadPart - d.query);
            busiest = std::max(busiest, std::clamp(1.0 - idleShare, 0.0, 1.0));
            if (elapsed > 0) {
                read += (perf.BytesRead.QuadPart - d.read) / elapsed;
                written += (perf.BytesWritten.QuadPart - d.written) / elapsed;
            }
        }
        d.idle = perf.IdleTime.QuadPart;
        d.query = perf.QueryTime.QuadPart;
        d.read = perf.BytesRead.QuadPart;
        d.written = perf.BytesWritten.QuadPart;
    }
    disk = busiest;
    diskRead = std::max(0.0, read);
    diskWrite = std::max(0.0, written);
    ULARGE_INTEGER freeBytes{}, totalBytes{};
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, nullptr)) {
        diskFreeGB = freeBytes.QuadPart / 1073741824.0;
        diskTotalGB = totalBytes.QuadPart / 1073741824.0;
    }

    // Sieć: suma po fizycznych kartach
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) == NO_ERROR) {
        ULONG64 in = 0, out = 0;
        for (ULONG r = 0; r < table->NumEntries; ++r) {
            const MIB_IF_ROW2& row = table->Table[r];
            if (!row.InterfaceAndOperStatusFlags.HardwareInterface || row.OperStatus != IfOperStatusUp) continue;
            in += row.InOctets;
            out += row.OutOctets;
        }
        FreeMibTable(table);
        if (lastIn_ && elapsed > 0) {
            netDown = static_cast<double>(in - lastIn_) / elapsed;
            netUp = static_cast<double>(out - lastOut_) / elapsed;
        }
        lastIn_ = in;
        lastOut_ = out;
    }

    SampleUsage(elapsed);
    if (now - lastSave_ > 60000) {
        SaveUsage();
        lastSave_ = now;
    }
}

void SystemStats::SampleUsage(double elapsedSec) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (st.wDay != day_) {  // nowy dzień — od zera
        SaveUsage();
        day_ = st.wDay;
        usage_.clear();
    }
    LASTINPUTINFO lii{sizeof(lii)};
    const bool idle = GetLastInputInfo(&lii) && GetTickCount() - lii.dwTime > kIdleLimitMs;
    HWND fg = GetForegroundWindow();
    if (!idle && fg && fg != GetShellWindow() && elapsedSec > 0 && elapsedSec < 30) {
        if (const HWND root = GetAncestor(fg, GA_ROOTOWNER)) fg = root;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        const bool own = pid == GetCurrentProcessId();
        const bool toolWindow = (GetWindowLongPtrW(fg, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
        if (!toolWindow || !own) {
            const std::wstring name = own ? L"Exploder" : AppNameForWindow(fg);
            if (!name.empty()) {
                auto it = std::find_if(usage_.begin(), usage_.end(), [&](const auto& e) { return e.first == name; });
                if (it == usage_.end()) usage_.emplace_back(name, elapsedSec);
                else it->second += elapsedSec;
            }
        }
    }
    topApps = usage_;
    std::sort(topApps.begin(), topApps.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (topApps.size() > 5) topApps.resize(5);
}

std::wstring SystemStats::UsagePath() const {
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    std::wstring dir = std::wstring(base, n) + L"\\Exploder";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\usage";
    CreateDirectoryW(dir.c_str(), nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t file[32];
    swprintf_s(file, L"\\%04u-%02u-%02u.txt", st.wYear, st.wMonth, st.wDay);
    return dir + file;
}

void SystemStats::LoadUsage() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, UsagePath().c_str(), L"r, ccs=UTF-8") || !f) return;
    wchar_t line[512];
    while (fgetws(line, ARRAYSIZE(line), f)) {
        wchar_t* tab = wcsrchr(line, L'\t');
        if (!tab) continue;
        *tab = 0;
        usage_.emplace_back(line, _wtof(tab + 1));
    }
    fclose(f);
}

void SystemStats::SaveUsage() {
    FILE* f = nullptr;
    if (usage_.empty() || _wfopen_s(&f, UsagePath().c_str(), L"w, ccs=UTF-8") || !f) return;
    for (const auto& [name, sec] : usage_) fwprintf(f, L"%s\t%.0f\n", name.c_str(), sec);
    fclose(f);
}
