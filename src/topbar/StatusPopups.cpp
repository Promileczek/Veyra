#include "StatusPopups.h"
#include <shellapi.h>
#include <algorithm>
#include <cmath>

namespace {
// Głośność
constexpr D2D1_RECT_F kMute{14.f, 52.f, 46.f, 84.f};
constexpr float kTrackL = 58.f, kTrackR = 236.f, kTrackY = 68.f;

// Sieć
constexpr float kW = 330.f, kHeader = 40.f, kCurrent = 66.f, kRow = 36.f, kPasswordRow = 44.f, kFooter = 40.f, kMaxRows = 8;
constexpr wchar_t kWifi[] = {L'\xE872', L'\xE873', L'\xE874', L'\xE701'};
constexpr wchar_t kEthernet = L'\xE839', kOffline = L'\xF384', kLock = L'\xE72E';
constexpr UINT_PTR kRescanTimer = 1;
}  // namespace

// ---------------- Głośność ----------------

void VolumePopup::Paint(ID2D1DeviceContext* dc) {
    Text(dc, L"Głośność", titleFormat_.Get(), {16.f, 8.f, 284.f, 30.f}, kText);
    Text(dc, volume_ && !volume_->DeviceName().empty() ? volume_->DeviceName() : L"Brak urządzenia", smallFormat_.Get(),
         {16.f, 28.f, 284.f, 46.f}, kDim);
    if (!volume_ || !volume_->Available()) return;

    const float level = volume_->Level();
    const bool muted = volume_->Muted();
    if (hoverMute_) Fill(dc, kMute, kHover, 8.f);
    const wchar_t g = muted ? L'\xE74F' : (level < 0.34f ? L'\xE993' : level < 0.67f ? L'\xE994' : L'\xE995');
    Glyph(dc, g, kMute, kText);

    const float fillX = kTrackL + (kTrackR - kTrackL) * level;
    Fill(dc, {kTrackL, kTrackY - 2.f, kTrackR, kTrackY + 2.f}, D2D1::ColorF(1.f, 1.f, 1.f, 0.2f), 2.f);
    Fill(dc, {kTrackL, kTrackY - 2.f, fillX, kTrackY + 2.f}, muted ? kDim : kAccent, 2.f);
    brush_->SetColor(kText);
    dc->FillEllipse(D2D1::Ellipse({fillX, kTrackY}, dragging_ ? 9.f : 8.f, dragging_ ? 9.f : 8.f), brush_.Get());
    Text(dc, std::to_wstring(std::lround(level * 100.f)), textFormat_.Get(), {248.f, 52.f, 290.f, 84.f}, kText);
}

void VolumePopup::SetFromX(float x) {
    if (!volume_) return;
    volume_->SetLevel(std::clamp((x - kTrackL) / (kTrackR - kTrackL), 0.f, 1.f));
    Redraw();
}

void VolumePopup::OnMouseMove(D2D1_POINT_2F p) {
    if (dragging_) return SetFromX(p.x);
    const bool hover = Inside(kMute, p);
    if (hover != hoverMute_) {
        hoverMute_ = hover;
        Redraw();
    }
}

void VolumePopup::OnMouseDown(D2D1_POINT_2F p) {
    if (Inside(kMute, p)) {
        if (volume_) volume_->ToggleMute();
        Redraw();
        return;
    }
    if (p.x >= kTrackL - 10.f && p.x <= kTrackR + 10.f && p.y >= 50.f && p.y <= 88.f) {
        dragging_ = true;
        Capture(true);
        SetFromX(p.x);
    }
}

void VolumePopup::OnMouseUp(D2D1_POINT_2F) {
    dragging_ = false;
    Capture(false);
    Redraw();
}

void VolumePopup::OnWheel(int delta) {
    if (!volume_) return;
    volume_->SetLevel(volume_->Level() + (delta > 0 ? 0.02f : -0.02f));
    Redraw();
}

// ---------------- Sieć ----------------

void NetworkPopup::Refresh() {
    info_ = network_->Query();
    details_ = network_->Details();
    hasWifi_ = network_->HasWifi();
    wifi_ = hasWifi_ ? network_->WifiNetworks() : std::vector<WifiNetwork>{};
    if (wifi_.size() > kMaxRows) wifi_.resize(static_cast<size_t>(kMaxRows));
    if (passwordFor_ >= static_cast<int>(wifi_.size())) passwordFor_ = -1;
}

void NetworkPopup::OnShow() {
    hover_ = HoverNone;
    passwordFor_ = -1;
    password_.clear();
    status_.clear();
    Refresh();
    if (hasWifi_) {
        network_->ScanWifi();  // wyniki skanu przychodzą po chwili
        SetTimer(hwnd_, kRescanTimer, 2500, nullptr);
    }
}

void NetworkPopup::OnTimer(UINT_PTR id) {
    if (id != kRescanTimer) return;
    if (!Visible()) {
        KillTimer(hwnd_, kRescanTimer);
        return;
    }
    Refresh();
    Relayout();
}

float NetworkPopup::WifiTop() const { return kHeader + kCurrent + 30.f; }

D2D1_SIZE_F NetworkPopup::ContentSize() {
    float h = kHeader + kCurrent;
    if (hasWifi_) h += 30.f + std::max<size_t>(wifi_.size(), 1) * kRow + (passwordFor_ >= 0 ? kPasswordRow : 0.f);
    return {kW, h + kFooter};
}

int NetworkPopup::RowAt(D2D1_POINT_2F p) const {
    if (p.x < 6.f || p.x > kW - 6.f) return HoverNone;
    const float footerTop = size_.height - kFooter;
    if (p.y >= footerTop + 4.f && p.y < size_.height - 4.f) return HoverSettings;
    if (!hasWifi_) return HoverNone;
    float y = WifiTop();
    for (size_t i = 0; i < wifi_.size(); ++i) {
        if (p.y >= y && p.y < y + kRow) return static_cast<int>(i);
        y += kRow + (passwordFor_ == static_cast<int>(i) ? kPasswordRow : 0.f);
    }
    return HoverNone;
}

void NetworkPopup::Paint(ID2D1DeviceContext* dc) {
    const float W = size_.width;
    Text(dc, L"Sieć", titleFormat_.Get(), {16.f, 8.f, W - 16.f, kHeader - 6.f}, kText);

    // Bieżące połączenie
    float y = kHeader;
    wchar_t g = kOffline;
    std::wstring name = L"Brak połączenia", status = L"Nie połączono z siecią";
    if (info_.kind == NetworkInfo::Kind::Wifi) {
        g = kWifi[std::clamp(info_.signal / 25, 0, 3)];
        name = info_.name;
    } else if (info_.kind == NetworkInfo::Kind::Ethernet) {
        g = kEthernet;
        name = L"Ethernet";
    }
    if (info_.kind != NetworkInfo::Kind::None) status = info_.internet ? L"Połączono, internet dostępny" : L"Połączono, brak internetu";
    Fill(dc, {8.f, y, W - 8.f, y + kCurrent - 6.f}, D2D1::ColorF(1.f, 1.f, 1.f, 0.06f), 10.f);
    Glyph(dc, g, {14.f, y, 46.f, y + kCurrent - 6.f}, kText);
    Text(dc, name, textFormat_.Get(), {52.f, y + 6.f, W - 16.f, y + 24.f}, kText);
    Text(dc, status, smallFormat_.Get(), {52.f, y + 23.f, W - 16.f, y + 39.f}, info_.internet ? kAccent : kDim);
    std::wstring detail = details_.ipv4.empty() ? L"" : L"IP " + details_.ipv4;
    if (details_.linkSpeed) {
        const double mbps = details_.linkSpeed / 1e6;
        wchar_t buf[32];
        swprintf_s(buf, mbps >= 1000 ? L"%.0f Gb/s" : L"%.0f Mb/s", mbps >= 1000 ? mbps / 1000 : mbps);
        detail += (detail.empty() ? L"" : L"  •  ") + std::wstring(buf);
    }
    Text(dc, detail, smallFormat_.Get(), {52.f, y + 38.f, W - 16.f, y + 54.f}, kDim);
    y += kCurrent;

    // Wi-Fi
    if (hasWifi_) {
        Text(dc, L"Sieci Wi-Fi", smallFormat_.Get(), {16.f, y + 4.f, W - 16.f, y + 26.f}, kDim);
        y = WifiTop();
        if (wifi_.empty()) Text(dc, L"Szukanie sieci…", smallFormat_.Get(), {16.f, y, W - 16.f, y + kRow}, kDim);
        for (size_t i = 0; i < wifi_.size(); ++i) {
            const WifiNetwork& n = wifi_[i];
            const D2D1_RECT_F row{8.f, y, W - 8.f, y + kRow};
            if (hover_ == static_cast<int>(i) || passwordFor_ == static_cast<int>(i)) Fill(dc, row, kHover, 8.f);
            Glyph(dc, kWifi[std::clamp(n.signal / 25, 0, 3)], {14.f, y, 42.f, y + kRow}, kText);
            Text(dc, n.ssid, textFormat_.Get(), {48.f, y, W - 110.f, y + kRow}, kText);
            if (n.secured) Glyph(dc, kLock, {W - 106.f, y, W - 86.f, y + kRow}, kDim);
            std::wstring right = n.connected ? L"Połączono" : (hover_ == static_cast<int>(i) ? L"Połącz" : L"");
            if (n.connected && hover_ == static_cast<int>(i)) right = L"Rozłącz";
            Text(dc, right, smallFormat_.Get(), {W - 82.f, y, W - 14.f, y + kRow}, n.connected ? kAccent : kText);
            y += kRow;
            if (passwordFor_ == static_cast<int>(i)) {  // pole hasła
                const D2D1_RECT_F box{48.f, y + 6.f, W - 16.f, y + kPasswordRow - 6.f};
                Fill(dc, box, D2D1::ColorF(1.f, 1.f, 1.f, 0.12f), 8.f);
                brush_->SetColor(kAccent);
                dc->DrawRoundedRectangle(D2D1::RoundedRect(box, 8.f, 8.f), brush_.Get(), 1.f);
                const std::wstring shown = password_.empty() ? L"Hasło — wpisz i naciśnij Enter" : std::wstring(password_.size(), L'\x2022');
                Text(dc, shown, smallFormat_.Get(), {box.left + 10.f, box.top, box.right - 8.f, box.bottom},
                     password_.empty() ? kDim : kText);
                y += kPasswordRow;
            }
        }
    }

    // Stopka
    const float footerTop = size_.height - kFooter;
    Fill(dc, {12.f, footerTop, W - 12.f, footerTop + 1.f}, D2D1::ColorF(1.f, 1.f, 1.f, 0.12f));
    if (hover_ == HoverSettings) Fill(dc, {8.f, footerTop + 4.f, W - 8.f, size_.height - 4.f}, kHover, 8.f);
    Text(dc, L"Połączenia sieciowe…", smallFormat_.Get(), {16.f, footerTop, W - 16.f, size_.height}, kText);
    if (!status_.empty()) Text(dc, status_, smallFormat_.Get(), {W - 140.f, footerTop, W - 16.f, size_.height}, kAccent);
}

void NetworkPopup::OnMouseMove(D2D1_POINT_2F p) {
    const int hit = RowAt(p);
    if (hit != hover_) {
        hover_ = hit;
        Redraw();
    }
}

void NetworkPopup::OnMouseDown(D2D1_POINT_2F p) {
    const int hit = RowAt(p);
    if (hit == HoverSettings) {
        ShellExecuteW(nullptr, nullptr, L"control.exe", L"ncpa.cpl", nullptr, SW_SHOWNORMAL);
        Hide();
        return;
    }
    if (hit < 0) return;
    const WifiNetwork& n = wifi_[static_cast<size_t>(hit)];
    if (n.connected) {
        network_->DisconnectWifi();
        status_ = L"Rozłączono";
    } else if (!n.profile.empty() || !n.secured) {
        status_ = network_->ConnectWifi(n, L"") ? L"Łączenie…" : L"Nie udało się";
    } else {
        passwordFor_ = passwordFor_ == hit ? -1 : hit;  // sieć bez zapisanego hasła — pokaż pole
        password_.clear();
        Relayout();
        return;
    }
    Redraw();
}

void NetworkPopup::OnChar(wchar_t ch) {
    if (passwordFor_ < 0) return;
    if (ch == L'\b') {
        if (!password_.empty()) password_.pop_back();
    } else if (ch == L'\r') {
        status_ = network_->ConnectWifi(wifi_[static_cast<size_t>(passwordFor_)], password_) ? L"Łączenie…" : L"Nie udało się";
        SecureZeroMemory(password_.data(), password_.size() * sizeof(wchar_t));
        password_.clear();
        passwordFor_ = -1;
        Relayout();
        return;
    } else if (ch >= L' ') {
        password_ += ch;
    }
    Redraw();
}
