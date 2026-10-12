#include "Accent.h"

namespace {
// Nieudokumentowane API user32 (używane m.in. przez TranslucentTB, Open-Shell).
struct AccentPolicy {
    int state;
    int flags;
    DWORD gradientColor;
    int animationId;
};
struct CompositionData {
    int attribute;
    void* data;
    SIZE_T size;
};
using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, CompositionData*);
constexpr int kWcaAccentPolicy = 19;
}  // namespace

bool SetBackdrop(HWND hwnd, Backdrop kind, DWORD tintAbgr) {
    static const auto fn = reinterpret_cast<SetWindowCompositionAttributeFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (!fn) return false;
    AccentPolicy policy{};
    policy.state = kind == Backdrop::Acrylic ? 4 : kind == Backdrop::Blur ? 3 : 0;
    policy.flags = 2;  // użyj gradientColor jako tintu
    policy.gradientColor = tintAbgr;
    CompositionData data{kWcaAccentPolicy, &policy, sizeof(policy)};
    return fn(hwnd, &data) != FALSE;
}
