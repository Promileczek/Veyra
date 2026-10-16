#pragma once
// Informacje o komputerze dla Ustawienia → Informacje: system, procesor, pamięć, karta graficzna, płyta główna, dyski.
#include <windows.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#pragma comment(lib, "dxgi.lib")

namespace SystemInfo {
struct Row {
    std::wstring key, value;
};

namespace detail {
inline std::wstring Reg(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t buf[512]{};
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    std::wstring s = buf;
    while (!s.empty() && s.back() == L' ') s.pop_back();
    while (!s.empty() && s.front() == L' ') s.erase(s.begin());
    return s;
}
inline DWORD RegDword(HKEY root, const wchar_t* key, const wchar_t* name) {
    DWORD v = 0, size = sizeof(v);
    RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v;
}
inline std::wstring Gb(double bytes) {
    wchar_t s[32];
    swprintf_s(s, bytes >= 1024.0 * 1024 * 1024 * 1024 ? L"%.1f TB" : L"%.0f GB",
               bytes >= 1024.0 * 1024 * 1024 * 1024 ? bytes / (1024.0 * 1024 * 1024 * 1024) : bytes / (1024.0 * 1024 * 1024));
    return s;
}
}  // namespace detail

inline std::vector<Row> Collect() {
    using namespace detail;
    std::vector<Row> rows;
    // System
    const wchar_t* nt = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const std::wstring build = Reg(HKEY_LOCAL_MACHINE, nt, L"CurrentBuild");
    std::wstring product = Reg(HKEY_LOCAL_MACHINE, nt, L"ProductName");
    if (_wtoi(build.c_str()) >= 22000) {  // Windows 11 nadal zapisuje się w rejestrze jako „Windows 10”
        const size_t at = product.find(L"Windows 10");
        if (at != std::wstring::npos) product.replace(at, 10, L"Windows 11");
    }
    rows.push_back({L"System", product + L" " + Reg(HKEY_LOCAL_MACHINE, nt, L"DisplayVersion") + L" (kompilacja " + build + L"." +
                                   std::to_wstring(RegDword(HKEY_LOCAL_MACHINE, nt, L"UBR")) + L")"});
    wchar_t name[256]{};
    DWORD n = ARRAYSIZE(name);
    if (GetComputerNameExW(ComputerNameDnsHostname, name, &n)) rows.push_back({L"Nazwa komputera", name});
    // Procesor
    std::wstring cpu = Reg(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    rows.push_back({L"Procesor", cpu});  // bez liczby rdzeni/wątków (mniej szczegółów)
    // Pamięć
    MEMORYSTATUSEX ms{sizeof(ms)};
    GlobalMemoryStatusEx(&ms);
    rows.push_back({L"Pamięć RAM", Gb(static_cast<double>(ms.ullTotalPhys))});
    // Karty graficzne (bez programowej)
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i, adapter.Reset()) {
            DXGI_ADAPTER_DESC1 d{};
            adapter->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            std::wstring v = d.Description;
            if (d.DedicatedVideoMemory > 256ull * 1024 * 1024) v += L"  ·  " + Gb(static_cast<double>(d.DedicatedVideoMemory));
            rows.push_back({L"Karta graficzna", v});
        }
    }
    // Płyta główna i BIOS
    const wchar_t* bios = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    const std::wstring board = Reg(HKEY_LOCAL_MACHINE, bios, L"BaseBoardManufacturer") + L" " + Reg(HKEY_LOCAL_MACHINE, bios, L"BaseBoardProduct");
    if (board.size() > 1) rows.push_back({L"Płyta główna", board});
    const std::wstring biosVer = Reg(HKEY_LOCAL_MACHINE, bios, L"BIOSVendor") + L" " + Reg(HKEY_LOCAL_MACHINE, bios, L"BIOSVersion");
    if (biosVer.size() > 1) rows.push_back({L"BIOS", biosVer});
    // (Dyski pominięte — mniej szczegółów)
    return rows;
}
}  // namespace SystemInfo
