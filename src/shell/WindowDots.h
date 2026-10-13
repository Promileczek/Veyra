#pragma once
#include "../core/Graphics.h"
#include <memory>
#include <vector>

// Kropki macOS na przyciskach okien innych programów (cmd, Brave, Discord, Panel sterowania…). Windows nie pozwala
// podmienić przycisków w cudzym oknie, więc nad przyciskami każdego widocznego okna leży nasza nakładka: tło w kolorze
// paska tytułu (próbka z ekranu) + kropki wyrównane do prawej. Klik → WM_SYSCOMMAND (zamknij / minimalizuj / maksymalizuj).
//  - nakładka NIE jest "zawsze na wierzchu": leży w kolejności okien tuż nad swoim oknem (inne okna mogą ją zasłonić),
//  - na czas przeciągania okna znika (wraca po upuszczeniu),
//  - pomija: pełny ekran, okna narzędziowe, okna Explodera (mają własne kropki), nieznane okna Chromium/Electron.
// Położenie przycisków: DWMWA_CAPTION_BUTTON_BOUNDS (zwykłe okna) albo stałe wymiary (Brave/Chrome/Edge, Discord).
class WindowDots {
public:
    bool Install(HINSTANCE inst, Graphics& g);
    void Uninstall();
    bool Enabled() const { return enabled_; }
    void SetEnabled(bool on);

private:
    struct Overlay {
        HWND hwnd = nullptr;
        HWND target = nullptr;
        Surface surface;
        ComPtr<ID2D1SolidColorBrush> brush;
        D2D1_COLOR_F background{0.1f, 0.1f, 0.1f, 1.f};
        UINT dpi = 96;
        float widthDip = 0.f, heightDip = 0.f, scale = 1.f, dy = 0.f;
        bool hover = false, trackingMouse = false, moving = false;
        int resampleTicks = 0;
    };

    static void CALLBACK OnEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(Overlay& o, UINT msg, WPARAM wp, LPARAM lp);
    void HandleEvent(DWORD event, HWND hwnd, LONG idObject);
    void ScanAll();                   // nakładki dla wszystkich widocznych okien
    void Track(HWND target);          // dodaje nakładkę, jeśli okno się nadaje
    void Drop(HWND target);
    Overlay* Find(HWND target);
    bool IsCandidate(HWND h) const;   // okno, które w ogóle może mieć kropki (bez sprawdzania stanu)
    bool ButtonArea(HWND target, RECT& out, float& scale, float* dy = nullptr) const;  // piksele ekranu; false = teraz bez kropek
    void Periodic();  // co 1,5 s: nowe okna, schowane nakładki do pokazania, martwe okna
    void Update(Overlay& o);          // położenie + kolejność + kolor + rysowanie
    void Restack();                   // każda nakładka tuż nad swoim oknem (po zmianie aktywnego okna)
    void Hide(Overlay& o);
    bool SampleBackground(Overlay& o, const RECT& area);
    void StartResample(Overlay& o);
    void Render(Overlay& o);

    HINSTANCE inst_ = nullptr;
    Graphics* g_ = nullptr;
    std::vector<std::unique_ptr<Overlay>> overlays_;
    HWINEVENTHOOK systemHook_ = nullptr;    // pierwszy plan, minimalizacja, przeciąganie
    HWINEVENTHOOK objectHook_ = nullptr;    // zniszczenie/pokazanie/ukrycie okna
    HWINEVENTHOOK locationHook_ = nullptr;  // ruch/zmiana rozmiaru okien
    bool enabled_ = true;
    UINT_PTR timer_ = 0;
};
