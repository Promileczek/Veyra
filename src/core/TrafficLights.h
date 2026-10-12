#pragma once
#include <d2d1_1.h>

// Kropki macOS (zamknij / minimalizuj / maksymalizuj) — okno Explodera, panel "Pliki", nakładki na cudzych oknach,
// aplikacje Explodera (Terminal, Kalkulator, Sklep).
namespace TrafficLights {
// origin: środek pierwszej kropki; hover: czy mysz jest nad grupą (wtedy symbole × − +); scale: 1 = kropki 12 DIP co 20 DIP
void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, D2D1_POINT_2F origin, bool hover, bool active = true, float scale = 1.f);
int HitTest(D2D1_POINT_2F origin, D2D1_POINT_2F p, float scale = 1.f);  // 0 czerwona, 1 żółta, 2 zielona, -1 nic
}  // namespace TrafficLights
