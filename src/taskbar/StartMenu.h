#pragma once
#include "../core/Popup.h"
#include <functional>
#include <vector>

// Proste menu przycisku Windows w docku: aplikacje Explodera, kilka skrótów systemowych i zasilanie.
class StartMenu : public Popup {
public:
    std::function<void()> openFiles;    // nowe okno Explodera (Ten komputer)
    std::function<void()> showDesktop;  // dawne działanie przycisku: zminimalizuj wszystko / przywróć

protected:
    D2D1_SIZE_F ContentSize() override;
    void Paint(ID2D1DeviceContext* dc) override;
    void OnShow() override;
    void OnMouseMove(D2D1_POINT_2F p) override;
    void OnMouseDown(D2D1_POINT_2F p) override;

private:
    enum class Action { Terminal, Calculator, Store, Files, Settings, TaskManager, Desktop, Run, Lock, Sleep, Restart, Shutdown, Logoff };
    struct Item {
        Action action;
        wchar_t glyph;
        const wchar_t* label;
    };
    int ItemAt(D2D1_POINT_2F p) const;    // wiersz menu albo -1
    int PowerAt(D2D1_POINT_2F p) const;   // przycisk zasilania albo -1
    D2D1_RECT_F RowRect(int i) const;
    D2D1_RECT_F PowerRect(int i) const;
    void Run(Action a);

    std::wstring user_;
    ComPtr<IDWriteTextFormat> centered_;
    int hover_ = -1, hoverPower_ = -1;
};
