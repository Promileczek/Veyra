#pragma once
#include <windows.h>
#include <d2d1.h>

enum class TaskbarStyle {
    Bar,   // pełna szerokość, styl Win11
    Dock,  // pływający, zaokrąglony, styl macOS
};

// Wygląd paska zadań. Na razie wartości domyślne; docelowo wczytywane z themes/*.json.
struct TaskbarTheme {
    TaskbarStyle style = TaskbarStyle::Dock;

    // --- Bar (wszystkie wymiary w DIP) ---
    float height = 48.f;
    float buttonWidth = 44.f;
    float buttonHeight = 40.f;
    float iconSize = 24.f;
    float radius = 5.f;
    bool centered = true;

    // --- Dock ---
    float dockHeight = 58.f;
    float dockMargin = 6.f;    // odstęp od krawędzi ekranu
    float dockPadding = 5.f;   // odstęp ikon od brzegu docka
    float dockCell = 48.f;     // szerokość miejsca na jedną ikonę
    float dockIconSize = 40.f;
    float dockRadius = 15.f;
    D2D1_COLOR_F dockBackground{0.12f, 0.12f, 0.14f, 0.30f};
    D2D1_COLOR_F dockBorder{1.f, 1.f, 1.f, 0.20f};
    D2D1_COLOR_F dockDot{1.f, 1.f, 1.f, 0.90f};

    float clockWidth = 80.f;

    bool acrylic = true;
    DWORD acrylicTint = 0x70201C1C;  // 0xAABBGGRR

    D2D1_COLOR_F background{0.06f, 0.06f, 0.08f, 0.30f};
    D2D1_COLOR_F border{1.f, 1.f, 1.f, 0.08f};
    D2D1_COLOR_F hover{1.f, 1.f, 1.f, 0.08f};
    D2D1_COLOR_F hoverActive{1.f, 1.f, 1.f, 0.15f};
    D2D1_COLOR_F pressed{1.f, 1.f, 1.f, 0.05f};
    D2D1_COLOR_F activeBg{1.f, 1.f, 1.f, 0.11f};
    D2D1_COLOR_F accent{0.38f, 0.70f, 1.f, 1.f};
    D2D1_COLOR_F indicator{1.f, 1.f, 1.f, 0.55f};
    D2D1_COLOR_F flash{0.95f, 0.55f, 0.15f, 0.55f};
    D2D1_COLOR_F text{1.f, 1.f, 1.f, 1.f};

    const wchar_t* font = L"Segoe UI";
    float fontSize = 12.f;
};
