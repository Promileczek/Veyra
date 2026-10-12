#include "TrafficLights.h"

namespace TrafficLights {
void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, D2D1_POINT_2F o, bool hover, bool active, float k) {
    const D2D1_COLOR_F colors[] = {D2D1::ColorF(0xFF5F57), D2D1::ColorF(0xFEBC2E), D2D1::ColorF(0x28C840)};
    for (int i = 0; i < 3; ++i) {
        const D2D1_POINT_2F c{o.x + i * 20.f * k, o.y};
        brush->SetColor(active || hover ? colors[i] : D2D1::ColorF(1.f, 1.f, 1.f, 0.22f));
        dc->FillEllipse(D2D1::Ellipse(c, 6.f * k, 6.f * k), brush);
        if (!hover) continue;
        brush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, 0.55f));  // symbole jak w macOS
        const float a = 2.6f * k, b = 3.f * k, w = 1.4f * k;
        if (i == 0) {
            dc->DrawLine({c.x - a, c.y - a}, {c.x + a, c.y + a}, brush, 1.3f * k);
            dc->DrawLine({c.x - a, c.y + a}, {c.x + a, c.y - a}, brush, 1.3f * k);
        } else if (i == 1) {
            dc->DrawLine({c.x - b, c.y}, {c.x + b, c.y}, brush, w);
        } else {
            dc->DrawLine({c.x - b, c.y}, {c.x + b, c.y}, brush, w);
            dc->DrawLine({c.x, c.y - b}, {c.x, c.y + b}, brush, w);
        }
    }
}

int HitTest(D2D1_POINT_2F o, D2D1_POINT_2F p, float k) {
    for (int i = 0; i < 3; ++i) {
        const float dx = p.x - (o.x + i * 20.f * k), dy = p.y - o.y;
        if (dx * dx + dy * dy <= 8.f * 8.f * k * k) return i;
    }
    return -1;
}
}  // namespace TrafficLights
