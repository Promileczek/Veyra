#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "Settings.h"

// Skala Veyry: pulpit, dock, górny pasek, menu i Alt+Tab (aplikacje mają skalowanie Windows). Ustawienie "uiScale":
// "auto" (domyślnie) albo procent 75–150. Auto dopasowuje układ projektowany pod 1600×900 punktów do ekranu:
// 1920×1080 przy 100% → 100%, laptop 1920×1080 przy 150% (1280×720 punktów) → 80%. Nigdy nie powiększa (max 100%).
namespace UiScale {
inline float AutoFactor() {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const float dpi = static_cast<float>(GetDpiForSystem());
    const float w = (mi.rcMonitor.right - mi.rcMonitor.left) * 96.f / dpi, h = (mi.rcMonitor.bottom - mi.rcMonitor.top) * 96.f / dpi;
    return std::clamp(std::min(w / 1600.f, h / 900.f), 0.6f, 1.f);
}
inline float FromSetting() {
    const std::wstring v = Settings::Get(L"uiScale", L"auto");
    const int percent = _wtoi(v.c_str());
    return percent >= 50 && percent <= 200 ? percent / 100.f : AutoFactor();
}
inline float& Cached() {
    static float factor = FromSetting();
    return factor;
}
inline float Factor() { return Cached(); }
// true = skala się zmieniła (ustawienie albo rozdzielczość/skalowanie Windows przy "auto")
inline bool Reload() {
    const float f = FromSetting();
    if (std::fabs(f - Cached()) < 0.001f) return false;
    Cached() = f;
    return true;
}
// Rozsyłany przez dock do pulpitu, górnego paska i siebie po zmianie skali (WM_DPICHANGED wysłany ręcznie
// system pomija) — odbiorcy przeliczają się jak po WM_DISPLAYCHANGE
inline UINT ChangedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"VeyraUiScaleChanged");
    return msg;
}
// DPI Windows → DPI, w którym rysuje Veyra
inline UINT Dpi(UINT systemDpi) { return std::max(48u, static_cast<UINT>(std::lround(systemDpi * Factor()))); }
}  // namespace UiScale
