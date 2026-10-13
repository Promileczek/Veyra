#pragma once
// Monitory dla Ustawienia → Ekran: lista (nazwa, rozdzielczość, odświeżanie, przekątna z EDID, skalowanie Windows)
// i zmiana układu (który po lewej / prawej / nad / pod) — jak w Ustawieniach Windows, które w Veyrze Full nie działają.
#include <windows.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <vector>
#pragma comment(lib, "shcore.lib")

namespace Displays {
struct Monitor {
    std::wstring device;  // \\.\DISPLAY1
    std::wstring name;    // "DELL P2419H"
    int x = 0, y = 0, width = 0, height = 0;  // piksele, pulpit wirtualny
    int hz = 0;
    int scale = 100;          // skalowanie Windows w %
    double diagonalIn = 0.0;  // 0 = nieznana
    bool primary = false;
};

namespace detail {
// Przekątna z EDID monitora (rejestr: Enum\DISPLAY\<model>\<instancja>\Device Parameters\EDID)
inline double DiagonalFromDevicePath(const std::wstring& path) {
    // \\?\DISPLAY#DELA0B1#5&2e2c8b8&0&UID4352#{e6f07b5f-…}
    std::vector<std::wstring> parts;
    size_t start = path.rfind(L"\\") != std::wstring::npos ? path.rfind(L"\\") + 1 : 0;
    for (size_t i = start; i <= path.size(); ++i)
        if (i == path.size() || path[i] == L'#') {
            parts.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    if (parts.size() < 3) return 0.0;
    const std::wstring key = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" + parts[1] + L"\\" + parts[2] + L"\\Device Parameters";
    BYTE edid[256]{};
    DWORD size = sizeof(edid);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"EDID", RRF_RT_REG_BINARY, nullptr, edid, &size) != ERROR_SUCCESS || size < 128)
        return 0.0;
    // Dokładniej: pierwszy opis trybu (bajty 54+12..14, milimetry); zapasowo bajty 21–22 (centymetry)
    double wmm = edid[66] | ((edid[68] & 0xF0) << 4), hmm = edid[67] | ((edid[68] & 0x0F) << 8);
    if (wmm < 50 || hmm < 50) wmm = edid[21] * 10.0, hmm = edid[22] * 10.0;
    if (wmm < 50 || hmm < 50) return 0.0;
    return std::sqrt(wmm * wmm + hmm * hmm) / 25.4;
}

// Nazwy i ścieżki monitorów z QueryDisplayConfig, po nazwie GDI źródła (\\.\DISPLAY1)
struct Target {
    std::wstring gdi, name, path;
};
inline std::vector<Target> Targets() {
    std::vector<Target> out;
    UINT32 nPaths = 0, nModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), nullptr) != ERROR_SUCCESS) return out;
    for (UINT32 i = 0; i < nPaths; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id};
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), paths[i].targetInfo.adapterId, paths[i].targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
        out.push_back({source.viewGdiDeviceName, target.monitorFriendlyDeviceName, target.monitorDevicePath});
    }
    return out;
}
}  // namespace detail

inline std::vector<Monitor> List() {
    std::vector<Monitor> list;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM p) -> BOOL {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(mon, &mi)) return TRUE;
            Monitor m;
            m.device = mi.szDevice;
            m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                m.x = dm.dmPosition.x, m.y = dm.dmPosition.y;
                m.width = static_cast<int>(dm.dmPelsWidth), m.height = static_cast<int>(dm.dmPelsHeight);
                m.hz = static_cast<int>(dm.dmDisplayFrequency);
            } else {
                m.x = mi.rcMonitor.left, m.y = mi.rcMonitor.top;
                m.width = mi.rcMonitor.right - mi.rcMonitor.left, m.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
            }
            UINT dx = 96, dy = 96;
            if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) m.scale = static_cast<int>(std::lround(dx * 100.0 / 96.0));
            reinterpret_cast<std::vector<Monitor>*>(p)->push_back(std::move(m));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&list));
    for (const auto& t : detail::Targets())
        for (Monitor& m : list)
            if (_wcsicmp(m.device.c_str(), t.gdi.c_str()) == 0) {
                m.name = t.name;
                m.diagonalIn = detail::DiagonalFromDevicePath(t.path);
            }
    for (Monitor& m : list)
        if (m.name.empty()) m.name = L"Monitor";
    // Kolejność jak w Windows: główny pierwszy, potem od lewej
    std::stable_sort(list.begin(), list.end(), [](const Monitor& a, const Monitor& b) {
        return a.primary != b.primary ? a.primary : (a.x != b.x ? a.x < b.x : a.y < b.y);
    });
    return list;
}

// Nowe położenia (piksele). Główny monitor musi zostać w (0,0) — przesuwamy wszystko względem niego.
// true = Windows przyjął układ
inline bool ApplyLayout(std::vector<Monitor> list) {
    int ox = 0, oy = 0;
    for (const Monitor& m : list)
        if (m.primary) ox = m.x, oy = m.y;
    bool ok = true;
    for (Monitor& m : list) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(m.device.c_str(), ENUM_CURRENT_SETTINGS, &dm)) continue;
        dm.dmPosition.x = m.x - ox;
        dm.dmPosition.y = m.y - oy;
        dm.dmFields = DM_POSITION;
        ok &= ChangeDisplaySettingsExW(m.device.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY | CDS_NORESET, nullptr) == DISP_CHANGE_SUCCESSFUL;
    }
    ok &= ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr) == DISP_CHANGE_SUCCESSFUL;
    return ok;
}

// Tryby monitora (rozdzielczość + odświeżanie), 32-bit, od największych
struct Mode {
    int width = 0, height = 0, hz = 0;
};
inline std::vector<Mode> Modes(const std::wstring& device) {
    std::vector<Mode> out;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(device.c_str(), i, &dm); ++i) {
        if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth < 800) continue;
        const Mode m{static_cast<int>(dm.dmPelsWidth), static_cast<int>(dm.dmPelsHeight), static_cast<int>(dm.dmDisplayFrequency)};
        if (std::none_of(out.begin(), out.end(), [&](const Mode& o) { return o.width == m.width && o.height == m.height && o.hz == m.hz; }))
            out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const Mode& a, const Mode& b) {
        return a.width != b.width ? a.width > b.width : (a.height != b.height ? a.height > b.height : a.hz > b.hz);
    });
    return out;
}
// Nowa rozdzielczość / częstotliwość; true = Windows przyjął
inline bool SetMode(const std::wstring& device, int width, int height, int hz) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(device.c_str(), ENUM_CURRENT_SETTINGS, &dm)) return false;
    dm.dmPelsWidth = static_cast<DWORD>(width);
    dm.dmPelsHeight = static_cast<DWORD>(height);
    dm.dmDisplayFrequency = static_cast<DWORD>(hz);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    return ChangeDisplaySettingsExW(device.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY, nullptr) == DISP_CHANGE_SUCCESSFUL;
}

// Przyklejenie przeciąganego monitora do najbliższej krawędzi innego (bez nakładania i bez przerw)
inline void Snap(std::vector<Monitor>& list, size_t moved) {
    if (list.size() < 2) return;
    Monitor& m = list[moved];
    long best = LONG_MAX;
    int bx = m.x, by = m.y;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i == moved) continue;
        const Monitor& o = list[i];
        // Kandydaci: po lewej, po prawej, nad, pod — z zachowaniem przesunięcia wzdłuż krawędzi (przyciętego)
        const int cy = std::clamp(m.y, o.y - m.height + 1, o.y + o.height - 1);
        const int cx = std::clamp(m.x, o.x - m.width + 1, o.x + o.width - 1);
        const int cand[4][2] = {{o.x - m.width, cy}, {o.x + o.width, cy}, {cx, o.y - m.height}, {cx, o.y + o.height}};
        for (const auto& c : cand) {
            const long d = std::labs(c[0] - m.x) + std::labs(c[1] - m.y);
            bool overlaps = false;
            for (size_t k = 0; k < list.size(); ++k)
                if (k != moved && c[0] < list[k].x + list[k].width && c[0] + m.width > list[k].x && c[1] < list[k].y + list[k].height &&
                    c[1] + m.height > list[k].y)
                    overlaps = true;
            if (!overlaps && d < best) best = d, bx = c[0], by = c[1];
        }
    }
    m.x = bx, m.y = by;
}
}  // namespace Displays
