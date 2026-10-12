#pragma once
#include "../core/Popup.h"
#include "../shell/SystemStatus.h"
#include <vector>

// Suwak głośności (PPM na głośności w górnym pasku).
class VolumePopup : public Popup {
public:
    void Init(VolumeControl& volume) { volume_ = &volume; }

protected:
    D2D1_SIZE_F ContentSize() override { return {300.f, 96.f}; }
    void Paint(ID2D1DeviceContext* dc) override;
    void OnMouseMove(D2D1_POINT_2F p) override;
    void OnMouseDown(D2D1_POINT_2F p) override;
    void OnMouseUp(D2D1_POINT_2F p) override;
    void OnWheel(int delta) override;

private:
    void SetFromX(float x);
    VolumeControl* volume_ = nullptr;
    bool dragging_ = false;
    bool hoverMute_ = false;
};

// Menu sieci (klik w sieć w górnym pasku): bieżące połączenie + sieci Wi-Fi (jeśli jest karta).
class NetworkPopup : public Popup {
public:
    void Init(NetworkStatus& network) { network_ = &network; }

protected:
    D2D1_SIZE_F ContentSize() override;
    void Paint(ID2D1DeviceContext* dc) override;
    void OnShow() override;
    void OnMouseMove(D2D1_POINT_2F p) override;
    void OnMouseDown(D2D1_POINT_2F p) override;
    void OnChar(wchar_t ch) override;
    void OnTimer(UINT_PTR id) override;

private:
    enum { HoverNone = -1, HoverSettings = -2 };
    void Refresh();
    float WifiTop() const;
    int RowAt(D2D1_POINT_2F p) const;

    NetworkStatus* network_ = nullptr;
    NetworkInfo info_;
    NetworkDetails details_;
    bool hasWifi_ = false;
    std::vector<WifiNetwork> wifi_;
    int hover_ = HoverNone;
    int passwordFor_ = -1;  // wiersz, pod którym wpisujemy hasło
    std::wstring password_;
    std::wstring status_;   // np. "Łączenie…"
};
