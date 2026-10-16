#pragma once
#include <d2d1_1.h>

// Ikona folderu w stylu Veyry (płasko, niebiesko): zakładka + zaokrąglony korpus. Rysowana bezpośrednio w D2D,
// żeby nie trzeba było dołączać pliku. Używana w panelu „Pliki” i na pulpicie zamiast systemowej ikony folderu.
inline void DrawVeyraFolder(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, const D2D1_RECT_F& box) {
    const float w = box.right - box.left, h = box.bottom - box.top, s = (w < h ? w : h);
    const float x = box.left + (w - s) / 2, y = box.top + (h - s) / 2;  // kwadrat wyśrodkowany
    auto px = [&](float t) { return x + t * s; };
    auto py = [&](float t) { return y + t * s; };
    const D2D1_COLOR_F body = D2D1::ColorF(0x4CA8FF), tab = D2D1::ColorF(0x6FBDFF);
    // Zakładka (za korpusem)
    brush->SetColor(tab);
    dc->FillRoundedRectangle(D2D1::RoundedRect({px(0.19f), py(0.30f), px(0.60f), py(0.46f)}, s * 0.05f, s * 0.05f), brush);
    // Korpus
    brush->SetColor(body);
    dc->FillRoundedRectangle(D2D1::RoundedRect({px(0.14f), py(0.37f), px(0.86f), py(0.78f)}, s * 0.08f, s * 0.08f), brush);
}
