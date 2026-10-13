#pragma once
#include "../core/Popup.h"
#include <wincodec.h>
#include <string>
#include <vector>

// Historia schowka Veyry (Win+V) — bez Eksploratora systemowa nie działa. Zapamiętuje do 25 ostatnich wpisów
// (tekst i obrazy, tylko w pamięci, do wylogowania). Kliknięcie wpisu: wraca do poprzedniego okna i wkleja go (Ctrl+V).
class ClipboardPanel : public Popup {
public:
    bool Start(HINSTANCE inst, Graphics& g);  // nasłuch zmian schowka
    void Stop();
    void Open(UINT dpi);

protected:
    D2D1_SIZE_F ContentSize() override;
    void Paint(ID2D1DeviceContext* dc) override;
    void OnShow() override;
    void OnMouseMove(D2D1_POINT_2F p) override;
    void OnMouseDown(D2D1_POINT_2F p) override;
    void OnWheel(int delta) override;

private:
    struct Entry {
        std::wstring text;
        HBITMAP bitmap = nullptr;  // własna kopia obrazu
        ComPtr<ID2D1Bitmap> d2d;   // miniatura (tworzona przy rysowaniu)
        float width = 0.f, height = 0.f;
    };
    static LRESULT CALLBACK ListenerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnClipboardChanged();  // nowa zawartość schowka → na początek listy
    void Paste(size_t index);
    float ItemHeight(const Entry& e) const { return e.bitmap ? 104.f : 58.f; }
    int HitAt(D2D1_POINT_2F p) const;

    HWND listener_ = nullptr;
    HWND previous_ = nullptr;  // okno, do którego wklejamy
    std::vector<Entry> entries_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IDWriteTextFormat> itemFormat_, buttonFormat_;
    bool ignoreNext_ = false;  // własna zmiana schowka (wklejanie) — nie dopisujemy drugi raz
    float scroll_ = 0.f, contentH_ = 0.f;
    int hover_ = -1;  // indeks wpisu, -2 = "Wyczyść"
};
