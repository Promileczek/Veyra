#pragma once
#include "../core/Popup.h"
#include <string>

// Okienko "Uruchom" Veyry (Win+R, menu Windows → Uruchom…) zamiast systemowego.
// "cmd" → Terminal Veyry, "calc" → Kalkulator Veyry; każda inna komenda jak w Windows (ShellExecute).
// Prawdziwy cmd.exe w systemie zostaje bez zmian (skrypty, Terminal Veyry pod spodem).
class RunBox : public Popup {
public:
    void Open(UINT dpi);  // na środku ekranu, nad dockiem

protected:
    D2D1_SIZE_F ContentSize() override { return {470.f, 168.f}; }
    void Paint(ID2D1DeviceContext* dc) override;
    void OnShow() override;
    void OnMouseMove(D2D1_POINT_2F p) override;
    void OnMouseDown(D2D1_POINT_2F p) override;
    void OnChar(wchar_t ch) override;

private:
    bool Run();  // false = błąd (komunikat zostaje w okienku)

    std::wstring text_, error_;
    bool selectAll_ = false;  // ostatnia komenda zaznaczona — pierwszy znak ją zastępuje
    int hover_ = -1;          // 0 OK, 1 Anuluj
};
