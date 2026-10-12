#include "StatsWidget.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPad = 18.f, kHeader = 40.f, kMeter = 46.f, kNet = 36.f, kUptime = 34.f, kAppsHeader = 30.f, kAppRow = 26.f;

std::wstring Fmt(const wchar_t* format, double v) {
    wchar_t buf[64];
    swprintf_s(buf, format, v);
    return buf;
}

std::wstring Speed(double bytesPerSec) {
    if (bytesPerSec >= 1048576.0) return Fmt(L"%.1f MB/s", bytesPerSec / 1048576.0);
    if (bytesPerSec >= 1024.0) return Fmt(L"%.0f KB/s", bytesPerSec / 1024.0);
    return Fmt(L"%.0f B/s", bytesPerSec);
}

std::wstring Duration(double seconds) {
    const long long s = static_cast<long long>(seconds);
    const long long d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
    if (d > 0) return std::to_wstring(d) + L" d " + std::to_wstring(h) + L" h " + std::to_wstring(m) + L" min";
    if (h > 0) return std::to_wstring(h) + L" h " + std::to_wstring(m) + L" min";
    if (m > 0) return std::to_wstring(m) + L" min";
    return std::to_wstring(s) + L" s";
}

D2D1_COLOR_F LoadColor(double v) {  // zielony → żółty → czerwony
    if (v < 0.6) return D2D1::ColorF(0.30f, 0.80f, 0.55f, 1.f);
    if (v < 0.85) return D2D1::ColorF(0.98f, 0.75f, 0.25f, 1.f);
    return D2D1::ColorF(1.f, 0.40f, 0.36f, 1.f);
}
}  // namespace

void StatsWidget::Init(Graphics& g) {
    g_ = &g;
    IDWriteFactory* dw = g_->DWrite();
    auto make = [dw](DWRITE_FONT_WEIGHT weight, float size, DWRITE_TEXT_ALIGNMENT align, ComPtr<IDWriteTextFormat>& out) {
        dw->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"pl-PL", &out);
        if (!out) return;
        out->SetTextAlignment(align);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteInlineObject> ellipsis;
        dw->CreateEllipsisTrimmingSign(out.Get(), &ellipsis);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        out->SetTrimming(&trimming, ellipsis.Get());
    };
    make(DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.f, DWRITE_TEXT_ALIGNMENT_LEADING, titleFormat_);
    make(DWRITE_FONT_WEIGHT_NORMAL, 12.f, DWRITE_TEXT_ALIGNMENT_LEADING, labelFormat_);
    make(DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.f, DWRITE_TEXT_ALIGNMENT_TRAILING, valueFormat_);
    make(DWRITE_FONT_WEIGHT_NORMAL, 11.f, DWRITE_TEXT_ALIGNMENT_LEADING, smallFormat_);
}

float StatsWidget::Height() const {
    const size_t apps = std::max<size_t>(stats_.topApps.size(), 1);
    return kHeader + 3 * kMeter + kNet + kUptime + kAppsHeader + apps * kAppRow + kPad;
}

void StatsWidget::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush) {
    if (!labelFormat_ || bounds_.right <= bounds_.left) return;
    const D2D1_RECT_F& b = bounds_;
    const float L = b.left + kPad, R = b.right - kPad;
    auto text = [&](const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
        brush->SetColor(c);
        dc->DrawText(s.c_str(), static_cast<UINT32>(s.size()), f, r, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    const D2D1_COLOR_F white{1.f, 1.f, 1.f, 0.95f}, dim{1.f, 1.f, 1.f, 0.55f};

    // Ramka panelu (szkło pod spodem rysuje Desktop)
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.22f));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(b, 22.f, 22.f), brush, 1.f);
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
    dc->DrawLine({b.left + 22.f, b.top + 1.5f}, {b.right - 22.f, b.top + 1.5f}, brush, 1.f);

    float y = b.top + 6.f;
    text(L"Statystyki", titleFormat_.Get(), {L, y, R, y + kHeader - 6.f}, white);
    y = b.top + kHeader;

    auto meter = [&](const std::wstring& label, double v, const std::wstring& value, const std::wstring& extra) {
        text(label, labelFormat_.Get(), {L, y, R, y + 18.f}, white);
        text(value, valueFormat_.Get(), {L, y, R, y + 18.f}, white);
        const float barY = y + 24.f;
        brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.12f));
        dc->FillRoundedRectangle(D2D1::RoundedRect({L, barY, R, barY + 5.f}, 2.5f, 2.5f), brush);
        brush->SetColor(LoadColor(v));
        dc->FillRoundedRectangle(D2D1::RoundedRect({L, barY, L + (R - L) * static_cast<float>(std::clamp(v, 0.0, 1.0)), barY + 5.f}, 2.5f, 2.5f),
                                 brush);
        if (!extra.empty()) text(extra, smallFormat_.Get(), {L, barY + 6.f, R, barY + 20.f}, dim);
        y += kMeter;
    };
    const SystemStats& s = stats_;
    meter(L"Procesor", s.cpu, Fmt(L"%.0f%%", s.cpu * 100.0), L"");
    meter(L"Pamięć RAM", s.ramTotal > 0 ? s.ramUsed / s.ramTotal : 0.0, Fmt(L"%.1f", s.ramUsed) + Fmt(L" / %.0f GB", s.ramTotal), L"");
    // Pasek: zajętość C: (aktywność bezczynnego dysku to zwykle ~0%, więc idzie do opisu, z dokładnością 0,1%)
    const double used = s.diskTotalGB > 0 ? 1.0 - s.diskFreeGB / s.diskTotalGB : 0.0;
    meter(L"Dysk C:", used, Fmt(L"%.0f%%", used * 100.0) + Fmt(L"  (%.0f GB wolne)", s.diskFreeGB),
          Fmt(L"Aktywność %.1f%%", s.disk * 100.0) + L"  •  Odczyt " + Speed(s.diskRead) + L"  •  Zapis " + Speed(s.diskWrite));

    // Sieć
    text(L"Sieć", labelFormat_.Get(), {L, y + 4.f, R, y + 22.f}, white);
    text(L"\x2193 " + Speed(s.netDown) + L"    \x2191 " + Speed(s.netUp), valueFormat_.Get(), {L, y + 4.f, R, y + 22.f}, white);
    y += kNet;

    // Czas działania
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
    dc->FillRectangle({L, y, R, y + 1.f}, brush);
    text(L"Czas działania", labelFormat_.Get(), {L, y + 6.f, R, y + kUptime - 4.f}, white);
    text(Duration(s.uptimeMs / 1000.0), valueFormat_.Get(), {L, y + 6.f, R, y + kUptime - 4.f}, white);
    y += kUptime;

    // Dziś w aplikacjach
    brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.10f));
    dc->FillRectangle({L, y, R, y + 1.f}, brush);
    text(L"Dziś w aplikacjach", smallFormat_.Get(), {L, y + 6.f, R, y + kAppsHeader}, dim);
    y += kAppsHeader;
    if (s.topApps.empty()) text(L"Jeszcze nic", smallFormat_.Get(), {L, y, R, y + kAppRow}, dim);
    const double most = s.topApps.empty() ? 1.0 : std::max(1.0, s.topApps.front().second);
    for (const auto& [name, sec] : s.topApps) {
        brush->SetColor(D2D1::ColorF(0.40f, 0.65f, 1.f, 0.22f));  // pasek proporcjonalny do najdłuższego
        dc->FillRoundedRectangle(D2D1::RoundedRect({L, y + 3.f, L + (R - L) * static_cast<float>(sec / most), y + kAppRow - 3.f}, 5.f, 5.f),
                                 brush);
        text(name, labelFormat_.Get(), {L + 8.f, y, R - 90.f, y + kAppRow}, white);
        text(Duration(sec), valueFormat_.Get(), {R - 100.f, y, R - 6.f, y + kAppRow}, white);
        y += kAppRow;
    }
}
