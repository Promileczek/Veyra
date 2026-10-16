#pragma once
#include <windows.h>
#include <vector>

// Kursor w stylu Veyry oraz funkcja „potrząśnij, aby znaleźć” (shake to find jak w macOS).
// Renderowany w Direct2D (32-bit PBGRA z cieniem, białym obrysem i błękitnym akcentem #61B3FF).
// Przy potrząśnięciu myszą kursor na chwilę się powiększa (do wybranego w Ustawieniach rozmiaru).
// Przy wyłączeniu / zamknięciu powłoki / awarii — systemowe kursory są natychmiast przywracane.
class VeyraCursor {
public:
    VeyraCursor() = default;
    ~VeyraCursor();

    static void Restore();  // przywraca oryginalne systemowe kursory (bezpieczne w crash handlerze)

    bool Install(HINSTANCE inst);
    void Uninstall();
    void ReloadSettings();

private:
    static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);
    void OnTimer();

    void LoadSettings();
    void CreateCachedCursors();
    void DestroyCachedCursors();

    void ApplyCursors();
    void ApplyEnlargedCursor();
    void RestoreNormalCursor();

    struct Sample {
        POINT pt;
        ULONGLONG time;
    };

    bool CheckShakeCondition() const;

    HINSTANCE inst_ = nullptr;
    UINT_PTR timer_ = 0;
    bool installed_ = false;

    bool veyraCursorEnabled_ = true;
    bool shakeToFindEnabled_ = true;
    int shakeScale_ = 96;
    int baseSize_ = 32;

    std::vector<Sample> samples_;
    bool isShaking_ = false;
    ULONGLONG shakeEndTime_ = 0;

    HCURSOR hNormalArrow_ = nullptr;
    HCURSOR hNormalHand_ = nullptr;
    HCURSOR hNormalIBeam_ = nullptr;
    HCURSOR hEnlargedArrow_ = nullptr;
    int cachedEnlargedSize_ = 0;
};
