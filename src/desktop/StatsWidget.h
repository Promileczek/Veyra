#pragma once
#include "../core/Graphics.h"
#include "../shell/SystemStats.h"

// Panel statystyk na pulpicie (po prawej od panelu aplikacji): procesor, RAM, dysk, sieć,
// czas działania komputera i dzisiejszy czas w aplikacjach. Rysowany przez Desktop.
class StatsWidget {
public:
    static constexpr float kWidth = 290.f;
    void Init(Graphics& g);
    void Sample() { stats_.Sample(); }
    void SaveUsage() { stats_.SaveUsage(); }
    float Height() const;
    void SetBounds(const D2D1_RECT_F& r) { bounds_ = r; }
    const D2D1_RECT_F& Bounds() const { return bounds_; }
    void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush);

private:
    Graphics* g_ = nullptr;
    SystemStats stats_;
    D2D1_RECT_F bounds_{};
    ComPtr<IDWriteTextFormat> titleFormat_, labelFormat_, valueFormat_, smallFormat_;
};
