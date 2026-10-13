#pragma once
#include "../core/Graphics.h"
#include <functional>
#include <string>
#include <thread>
#include <atomic>

// Powiadomienie Veyry: karta w prawym górnym rogu (pod górnym paskiem), nie zabiera fokusu, wysuwa się z prawej,
// po 12 s chowa się sama z animacją w prawo (nie gdy jest pod kursorem). Ikona w akcencie, tytuł, opis, przycisk główny i „Później”.
class Notification {
public:
    struct Content {
        wchar_t glyph = L'\xE895';  // Segoe MDL2: aktualizacja
        std::wstring title, body, action = L"OK", dismiss = L"Później";
        std::function<void()> onAction;
    };
    bool Create(HINSTANCE inst, Graphics& g);
    void Show(Content content, UINT dpi);
    void Hide();
    void Leave();  // schowanie z animacją
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    HWND Hwnd() const { return hwnd_; }
    void Destroy();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void Render();
    void Place(float slide);  // slide: 0 = na miejscu, 1 = całkiem z prawej
    int HitAt(D2D1_POINT_2F p) const;

    HWND hwnd_ = nullptr;
    Graphics* g_ = nullptr;
    UINT dpi_ = 96;
    Surface surface_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> titleFormat_, bodyFormat_, buttonFormat_, glyphFormat_;
    Content content_;
    D2D1_RECT_F actionRect_{}, dismissRect_{}, closeRect_{};
    int hover_ = -1;
    bool tracking_ = false;
    float slide_ = 1.f;
    bool leaving_ = false;
};

// Co jakiś czas (minutę po starcie, potem co 6 h) sprawdza GitHuba; nowa wersja → powiadomienie (raz na wersję).
// Tylko Veyra Full (Minimal nie ma aktualizatora). Wyłączane w Ustawienia → Aktualizacje ("autoUpdateCheck").
// Test: settings.txt "updateNotifyTest=1" pokazuje powiadomienie przy najbliższym sprawdzeniu, nawet bez nowej wersji.
class UpdateWatcher {
public:
    void Start(HINSTANCE inst, Graphics& g, std::function<UINT()> dpi);
    void Stop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void CheckAsync();

    HWND hwnd_ = nullptr;  // okno komunikatów (timer, wynik z wątku)
    Notification toast_;
    std::function<UINT()> dpi_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
};
