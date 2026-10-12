#include "WorkArea.h"
#include <shellapi.h>

namespace {
int g_top = 0;
int g_bottom = 0;

RECT PrimaryMonitor() {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi.rcMonitor;
}
}  // namespace

namespace WorkArea {
void Reserve(UINT edge, int pixels) {
    if (edge == ABE_TOP) g_top = pixels;
    else if (edge == ABE_BOTTOM) g_bottom = pixels;
    RECT work = PrimaryMonitor();
    work.top += g_top;
    work.bottom -= g_bottom;
    RECT current{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &current, 0);
    if (!EqualRect(&current, &work)) SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, SPIF_SENDCHANGE);
}

bool IsFullscreenWindow(HWND fg) {
    if (!fg || fg == GetShellWindow() || !IsWindowVisible(fg)) return false;
    if (GetWindowLongPtrW(fg, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    RECT wr{};
    MONITORINFO mi{sizeof(mi)};
    GetWindowRect(fg, &wr);
    GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi);
    // Zakrywa cały monitor. Zwykłe zmaksymalizowane okno się nie łapie — kończy się na obszarze roboczym
    // (pod górnym paskiem, nad dockiem). Styl okna nie jest pewny: przeglądarki z filmem zostawiają WS_CAPTION.
    return wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top && wr.right >= mi.rcMonitor.right &&
           wr.bottom >= mi.rcMonitor.bottom;
}

void Restore() {
    g_top = g_bottom = 0;
    RECT full = PrimaryMonitor();
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &full, SPIF_SENDCHANGE);
}
}  // namespace WorkArea
