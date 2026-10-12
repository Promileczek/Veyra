// Kalkulator Explodera — własny styl: jednolite ciemne tło, wynik na karcie z historią ostatnich obliczeń,
// cyfry krojem Bahnschrift, zaokrąglone przyciski (działania fioletowe, "=" z gradientem fiolet→niebieski, C czerwone),
// funkcje √x x² 1/x CE jako małe przyciski nad klawiaturą.
// Klawiatura: cyfry, + - * /, przecinek/kropka, Enter/=, Backspace, Esc (C), Delete (CE), %, Ctrl+C / Ctrl+V.
#include "../../src/core/AppWindow.h"
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
enum class Op { None, Add, Sub, Mul, Div };
enum class K { Digit, Comma, Percent, CE, C, Back, Inv, Sqr, Sqrt, Op, Equals, Negate };

struct Key {
    const wchar_t* label;
    K kind;
    int style;     // 0 cyfra, 1 działanie, 2 "=", 3 kasowanie, 4 funkcja, 5 mały przycisk nad klawiaturą
    int col, row;  // row -1 = rząd małych przycisków
};
constexpr int kCols = 4, kRows = 5;
constexpr Key kKeys[] = {
    {L"√x", K::Sqrt, 5, 0, -1}, {L"x²", K::Sqr, 5, 1, -1},      {L"1/x", K::Inv, 5, 2, -1},  {L"CE", K::CE, 5, 3, -1},
    {L"C", K::C, 3, 0, 0},      {L"\xE750", K::Back, 4, 1, 0}, {L"%", K::Percent, 4, 2, 0}, {L"÷", K::Op, 1, 3, 0},
    {L"7", K::Digit, 0, 0, 1},  {L"8", K::Digit, 0, 1, 1},     {L"9", K::Digit, 0, 2, 1},   {L"×", K::Op, 1, 3, 1},
    {L"4", K::Digit, 0, 0, 2},  {L"5", K::Digit, 0, 1, 2},     {L"6", K::Digit, 0, 2, 2},   {L"−", K::Op, 1, 3, 2},
    {L"1", K::Digit, 0, 0, 3},  {L"2", K::Digit, 0, 1, 3},     {L"3", K::Digit, 0, 2, 3},   {L"+", K::Op, 1, 3, 3},
    {L"±", K::Negate, 4, 0, 4}, {L"0", K::Digit, 0, 1, 4},     {L",", K::Comma, 0, 2, 4},   {L"=", K::Equals, 2, 3, 4},
};
constexpr int kKeyCount = static_cast<int>(std::size(kKeys));
constexpr float kChipH = 30.f, kGap = 8.f, kPad = 14.f;
const D2D1_COLOR_F kBg = D2D1::ColorF(0x121318), kCard = D2D1::ColorF(0x1B1D25), kViolet = D2D1::ColorF(0xA78BFA);

D2D1_COLOR_F Shade(D2D1_COLOR_F c, float d) {
    return {std::clamp(c.r + d, 0.f, 1.f), std::clamp(c.g + d, 0.f, 1.f), std::clamp(c.b + d, 0.f, 1.f), c.a};
}

// Liczba → tekst: do 15 cyfr znaczących, przecinek, grupy tysięcy oddzielone wąską spacją
std::wstring Format(double v) {
    if (!std::isfinite(v)) return L"Nie można dzielić przez zero";
    if (v == 0) return L"0";
    wchar_t buf[64];
    const double a = std::fabs(v);
    if (a >= 1e16 || a < 1e-12) {
        swprintf_s(buf, L"%.9e", v);
    } else {
        const int intDigits = a >= 1 ? static_cast<int>(std::floor(std::log10(a))) + 1 : 1;
        swprintf_s(buf, L"%.*f", std::max(0, 15 - intDigits), v);
        std::wstring s = buf;
        if (s.find(L'.') != std::wstring::npos) {  // zera na końcu części ułamkowej
            while (!s.empty() && s.back() == L'0') s.pop_back();
            if (!s.empty() && s.back() == L'.') s.pop_back();
        }
        wcscpy_s(buf, s.c_str());
    }
    std::wstring s = buf, out;
    const size_t dot = s.find_first_of(L".e");
    const size_t intEnd = dot == std::wstring::npos ? s.size() : dot;
    const size_t start = s[0] == L'-' ? 1 : 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i > start && i < intEnd && (intEnd - i) % 3 == 0) out += L'\x2009';
        out += s[i] == L'.' ? L',' : s[i];
    }
    return out;
}

class Calculator : public AppWindow {
public:
    bool Init(HINSTANCE inst, Graphics& g) {
        Options o;
        o.className = L"ExploderCalculator";
        o.title = L"Kalkulator";
        o.width = 340.f;
        o.height = 580.f;
        o.minWidth = 300.f;
        o.minHeight = 500.f;
        if (!Create(inst, g, o)) return false;
        exprFormat_ = MakeFormat(L"Bahnschrift", 15.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
        historyFormat_ = MakeFormat(L"Bahnschrift", 12.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
        keyFormat_ = MakeFormat(L"Bahnschrift", 15.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        digitFormat_ = MakeFormat(L"Bahnschrift", 22.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        opFormat_ = MakeFormat(L"Bahnschrift", 26.f, DWRITE_FONT_WEIGHT_LIGHT, DWRITE_TEXT_ALIGNMENT_CENTER);
        glyphFormat_ = MakeFormat(L"Segoe MDL2 Assets", 16.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        Redraw();
        return true;
    }

protected:
    // Pędzel liniowy (gradient) — tworzony na bieżąco, rysowanie jest rzadkie
    ComPtr<ID2D1LinearGradientBrush> Gradient(ID2D1DeviceContext* dc, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c0, D2D1_COLOR_F c1) {
        const D2D1_GRADIENT_STOP stops[] = {{0.f, c0}, {1.f, c1}};
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1LinearGradientBrush> grad;
        dc->CreateGradientStopCollection(stops, 2, &coll);
        if (coll) dc->CreateLinearGradientBrush({a, b}, coll.Get(), &grad);
        return grad;
    }

    void Paint(ID2D1DeviceContext* dc) override {
        const float W = widthDip_, H = heightDip_;
        dc->Clear(kBg);
        if (auto g = Gradient(dc, {0.f, 0.f}, {0.f, H}, D2D1::ColorF(0x171A26), D2D1::ColorF(0x0E0F14))) dc->FillRectangle({0.f, 0.f, W, H}, g.Get());

        // Karta z wynikiem: gradient, fioletowa poświata w rogu, akcent u dołu
        const D2D1_RECT_F card{kPad, titleH_ + 2.f, W - kPad, ChipsTop() - kGap - 2.f};
        const auto cardShape = D2D1::RoundedRect(card, 20.f, 20.f);
        if (auto g = Gradient(dc, {card.left, card.top}, {card.right, card.bottom}, D2D1::ColorF(0x23263A), D2D1::ColorF(0x181A24)))
            dc->FillRoundedRectangle(cardShape, g.Get());
        {
            const D2D1_GRADIENT_STOP stops[] = {{0.f, D2D1::ColorF(0x7C5CFF, 0.22f)}, {1.f, D2D1::ColorF(0x7C5CFF, 0.f)}};
            ComPtr<ID2D1GradientStopCollection> coll;
            ComPtr<ID2D1RadialGradientBrush> glow;
            dc->CreateGradientStopCollection(stops, 2, &coll);
            if (coll)
                dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({card.right - 30.f, card.top + 20.f}, {0.f, 0.f}, 160.f, 110.f),
                                              coll.Get(), &glow);
            if (glow) dc->FillRoundedRectangle(cardShape, glow.Get());
        }
        brush_->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.07f));
        dc->DrawRoundedRectangle(D2D1::RoundedRect({card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f}, 20.f, 20.f),
                                 brush_.Get(), 1.f);
        if (auto g = Gradient(dc, {card.left + 24.f, 0.f}, {card.right - 24.f, 0.f}, D2D1::ColorF(0x7C5CFF), D2D1::ColorF(0x3D8BFF)))
            dc->FillRoundedRectangle(D2D1::RoundedRect({card.left + 24.f, card.bottom - 3.f, card.right - 24.f, card.bottom - 1.f}, 1.f, 1.f), g.Get());

        float hy = card.top + 12.f;
        for (size_t i = 0; i < history_.size(); ++i, hy += 17.f)
            Text(dc, history_[i], historyFormat_.Get(), {card.left + 16.f, hy, card.right - 18.f, hy + 16.f},
                 D2D1::ColorF(1.f, 1.f, 1.f, 0.22f + 0.08f * static_cast<float>(i)));
        Text(dc, expression_, exprFormat_.Get(), {card.left + 16.f, card.bottom - 88.f, card.right - 18.f, card.bottom - 66.f}, kViolet);
        const float size = std::clamp(52.f * 11.f / std::max<size_t>(display_.size(), 11), 24.f, 52.f);
        if (size != bigSize_ || !bigFormat_) {
            bigSize_ = size;
            bigFormat_ = MakeFormat(L"Bahnschrift", size, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
        Text(dc, display_, bigFormat_.Get(), {card.left + 12.f, card.bottom - 68.f, card.right - 18.f, card.bottom - 10.f}, D2D1::ColorF(1.f, 1.f, 1.f));

        for (int i = 0; i < kKeyCount; ++i) {
            const Key& k = kKeys[i];
            const D2D1_RECT_F r = KeyRect(i);
            const float lift = i == pressed_ ? -0.03f : i == hover_ ? 0.05f : 0.f;
            const bool selectedOp = k.kind == K::Op && OpOf(k.label) == op_ && newEntry_ && op_ != Op::None;
            D2D1_COLOR_F top = D2D1::ColorF(0x242733), bottom = D2D1::ColorF(0x1C1E27), fg = D2D1::ColorF(1.f, 1.f, 1.f, 0.95f);
            IDWriteTextFormat* f = digitFormat_.Get();
            switch (k.style) {
            case 1:  // działania: fioletowy odcień, wybrane — pełny fiolet
                top = selectedOp ? D2D1::ColorF(0x7C5CFF) : D2D1::ColorF(0x2E2850);
                bottom = selectedOp ? D2D1::ColorF(0x5B45C9) : D2D1::ColorF(0x221E3A);
                fg = selectedOp ? D2D1::ColorF(0xFFFFFF) : kViolet;
                break;
            case 2: top = D2D1::ColorF(0x8A6BFF), bottom = D2D1::ColorF(0x3D8BFF); break;
            case 3: top = D2D1::ColorF(0x3A2129), bottom = D2D1::ColorF(0x2A1A20), fg = D2D1::ColorF(0xFF6B6B), f = keyFormat_.Get(); break;
            case 4:
                top = D2D1::ColorF(0x2B2E3B), bottom = D2D1::ColorF(0x22242F), fg = D2D1::ColorF(0xC9CCD6);
                f = k.kind == K::Back ? glyphFormat_.Get() : keyFormat_.Get();
                break;
            default: break;
            }
            if (k.style == 5) {  // małe funkcje: obrysowane pigułki
                const float rad = kChipH / 2;
                if (i == hover_) Fill(dc, r, D2D1::ColorF(1.f, 1.f, 1.f, 0.06f), rad);
                brush_->SetColor(D2D1::ColorF(0x7C5CFF, i == hover_ ? 0.8f : 0.45f));
                dc->DrawRoundedRectangle(D2D1::RoundedRect({r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f}, rad, rad), brush_.Get(), 1.f);
                Text(dc, k.label, keyFormat_.Get(), r, D2D1::ColorF(0xC4B5FD));
                continue;
            }
            const float radius = 18.f;
            const auto shape = D2D1::RoundedRect(r, radius, radius);
            const bool diagonal = k.style == 2;  // "=": gradient po skosie, reszta z góry na dół (głębia)
            if (auto g = Gradient(dc, {r.left, r.top}, diagonal ? D2D1_POINT_2F{r.right, r.bottom} : D2D1_POINT_2F{r.left, r.bottom},
                                  Shade(top, lift), Shade(bottom, lift)))
                dc->FillRoundedRectangle(shape, g.Get());
            // Jaśniejsza krawędź u góry — przycisk wygląda na lekko wypukły
            if (auto g = Gradient(dc, {r.left, r.top}, {r.left, r.bottom}, D2D1::ColorF(1.f, 1.f, 1.f, 0.10f), D2D1::ColorF(1.f, 1.f, 1.f, 0.f)))
                dc->DrawRoundedRectangle(D2D1::RoundedRect({r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f}, radius, radius), g.Get(), 1.f);
            if (k.kind == K::Op || k.kind == K::Equals || k.kind == K::Negate) DrawSymbol(dc, k.label, r, fg);
            else Text(dc, k.label, f, r, fg);
        }
    }

    // Symbole działań liniami — równa grubość i środek niezależnie od czcionki
    void DrawSymbol(ID2D1DeviceContext* dc, const std::wstring& label, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
        const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, a = 8.f, w = 2.2f;
        brush_->SetColor(c);
        ComPtr<ID2D1StrokeStyle> round;
        ComPtr<ID2D1Factory> factory;
        dc->GetFactory(&factory);
        const D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
        factory->CreateStrokeStyle(props, nullptr, 0, &round);
        auto line = [&](float x1, float y1, float x2, float y2) { dc->DrawLine({x1, y1}, {x2, y2}, brush_.Get(), w, round.Get()); };
        auto dot = [&](float x, float y) { dc->FillEllipse(D2D1::Ellipse({x, y}, 1.9f, 1.9f), brush_.Get()); };
        if (label == L"+") {
            line(cx - a, cy, cx + a, cy);
            line(cx, cy - a, cx, cy + a);
        } else if (label == L"−") {
            line(cx - a, cy, cx + a, cy);
        } else if (label == L"×") {
            const float d = a * 0.72f;
            line(cx - d, cy - d, cx + d, cy + d);
            line(cx - d, cy + d, cx + d, cy - d);
        } else if (label == L"÷") {
            line(cx - a, cy, cx + a, cy);
            dot(cx, cy - 5.5f);
            dot(cx, cy + 5.5f);
        } else if (label == L"=") {
            line(cx - a, cy - 3.5f, cx + a, cy - 3.5f);
            line(cx - a, cy + 3.5f, cx + a, cy + 3.5f);
        } else if (label == L"±") {
            line(cx - 6.f, cy - 3.f, cx + 6.f, cy - 3.f);
            line(cx, cy - 9.f, cx, cy + 3.f);
            line(cx - 6.f, cy + 7.f, cx + 6.f, cy + 7.f);
        }
    }

    void OnMouseMove(D2D1_POINT_2F p) override {
        const int h = KeyAt(p);
        if (h != hover_) {
            hover_ = h;
            Redraw();
        }
    }
    void OnMouseLeave() override {
        hover_ = -1;
        Redraw();
    }
    void OnMouseDown(D2D1_POINT_2F p, bool right) override {
        if (right) return;
        pressed_ = KeyAt(p);
        if (pressed_ >= 0) SetCapture(hwnd_);
        Redraw();
    }
    void OnMouseUp(D2D1_POINT_2F p) override {
        if (pressed_ >= 0 && KeyAt(p) == pressed_) Press(pressed_);
        pressed_ = -1;
        ReleaseCapture();
        Redraw();
    }
    void OnDoubleClick(D2D1_POINT_2F p) override {  // szybkie klikanie w ten sam przycisk
        if (const int k = KeyAt(p); k >= 0) Press(k);
    }

    bool OnKeyDown(UINT vk) override {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (ctrl && vk == 'C') return CopyResult(), true;
        if (ctrl && vk == 'V') return Paste(), true;
        switch (vk) {
        case VK_RETURN: PressLabel(L"="); return true;
        case VK_ESCAPE: PressLabel(L"C"); return true;
        case VK_BACK: PressLabel(L"\xE750"); return true;
        case VK_DELETE: PressLabel(L"CE"); return true;
        }
        return false;
    }
    void OnChar(wchar_t ch) override {
        if (ch >= L'0' && ch <= L'9') PressLabel(std::wstring(1, ch));
        else if (ch == L'+') PressLabel(L"+");
        else if (ch == L'-') PressLabel(L"−");
        else if (ch == L'*' || ch == L'x') PressLabel(L"×");
        else if (ch == L'/' || ch == L':') PressLabel(L"÷");
        else if (ch == L',' || ch == L'.') PressLabel(L",");
        else if (ch == L'=') PressLabel(L"=");
        else if (ch == L'%') PressLabel(L"%");
        else if (ch == L'r') PressLabel(L"1/x");
        else if (ch == L'@') PressLabel(L"√x");
        else if (ch == L'q') PressLabel(L"x²");
    }

private:
    static Op OpOf(const std::wstring& l) {
        if (l == L"+") return Op::Add;
        if (l == L"−") return Op::Sub;
        if (l == L"×") return Op::Mul;
        if (l == L"÷") return Op::Div;
        return Op::None;
    }
    static const wchar_t* OpSign(Op op) {
        switch (op) {
        case Op::Add: return L"+";
        case Op::Sub: return L"−";
        case Op::Mul: return L"×";
        case Op::Div: return L"÷";
        default: return L"";
        }
    }
    static double Apply(Op op, double a, double b) {
        switch (op) {
        case Op::Add: return a + b;
        case Op::Sub: return a - b;
        case Op::Mul: return a * b;
        case Op::Div: return b == 0 ? NAN : a / b;
        default: return b;
        }
    }

    // Układ: klawiatura 4×5 na dole, nad nią rząd małych przycisków, wyżej karta z wynikiem
    D2D1_RECT_F Grid() const {
        const float h = std::min(heightDip_ * 0.56f, (widthDip_ - 2 * kPad) * 1.15f);
        return {kPad, heightDip_ - kPad - h, widthDip_ - kPad, heightDip_ - kPad};
    }
    float ChipsTop() const { return Grid().top - kGap - kChipH; }
    D2D1_RECT_F KeyRect(int i) const {
        const D2D1_RECT_F g = Grid();
        const Key& k = kKeys[i];
        const float w = (g.right - g.left - (kCols - 1) * kGap) / kCols;
        const float x = g.left + k.col * (w + kGap);
        if (k.row < 0) return {x, ChipsTop(), x + w, ChipsTop() + kChipH};
        const float h = (g.bottom - g.top - (kRows - 1) * kGap) / kRows;
        const float y = g.top + k.row * (h + kGap);
        return {x, y, x + w, y + h};
    }
    int KeyAt(D2D1_POINT_2F p) const {
        for (int i = 0; i < kKeyCount; ++i)
            if (Inside(KeyRect(i), p)) return i;
        return -1;
    }
    void PressLabel(const std::wstring& label) {
        for (int i = 0; i < kKeyCount; ++i)
            if (label == kKeys[i].label) return Press(i);
    }

    double Value() const {
        std::wstring s;
        for (wchar_t c : display_)
            if (c == L',') s += L'.';
            else if (c != L'\x2009' && c != L' ') s += c;
        return _wtof(s.c_str());
    }
    void Show(double v) {
        display_ = Format(v);
        error_ = !std::isfinite(v);
    }
    void ClearAll() {
        display_ = L"0";
        expression_.clear();
        acc_ = lastOperand_ = 0;
        op_ = lastOp_ = Op::None;
        error_ = false;
        newEntry_ = true;
    }

    // Funkcje jednoargumentowe (1/x, x², √x): wynik od razu, zapis w linii działania
    void Unary(const wchar_t* name, double result) {
        const std::wstring arg = Format(Value());
        const std::wstring prefix = op_ != Op::None ? Format(acc_) + L" " + OpSign(op_) + L" " : L"";
        expression_ = prefix + name + L"(" + arg + L")";
        Show(result);
        newEntry_ = true;
        unaryShown_ = true;
    }

    void Press(int index) {
        const Key& k = kKeys[index];
        const std::wstring key = k.label;
        if (error_ && k.kind != K::C && k.kind != K::CE) ClearAll();
        switch (k.kind) {
        case K::Digit: {
            if (newEntry_ || display_ == L"0") display_.clear();
            if (justEvaluated_) expression_.clear(), justEvaluated_ = false;
            if (unaryShown_ && op_ == Op::None) expression_.clear();
            unaryShown_ = false;
            newEntry_ = false;
            size_t digits = 0;
            for (wchar_t c : display_) digits += c >= L'0' && c <= L'9';
            if (digits < 16) display_ = Regroup(display_ + key);
            break;
        }
        case K::Comma:
            if (newEntry_) display_ = L"0", newEntry_ = false;
            if (display_.find(L',') == std::wstring::npos) display_ += L',';
            break;
        case K::Op: {
            const Op op = OpOf(key);
            if (op_ != Op::None && !newEntry_) {
                acc_ = Apply(op_, acc_, Value());
                Show(acc_);
            } else if (op_ == Op::None || !unaryShown_) {
                acc_ = Value();
            } else {
                acc_ = Apply(op_, acc_, Value());
                Show(acc_);
            }
            op_ = op;
            newEntry_ = true;
            justEvaluated_ = unaryShown_ = false;
            expression_ = Format(acc_) + L" " + OpSign(op);
            break;
        }
        case K::Equals: {
            if (op_ != Op::None) {
                lastOp_ = op_;
                lastOperand_ = newEntry_ && !unaryShown_ ? acc_ : Value();
                expression_ = Format(acc_) + L" " + OpSign(op_) + L" " + Format(lastOperand_) + L" =";
                acc_ = Apply(op_, acc_, lastOperand_);
                op_ = Op::None;
            } else if (lastOp_ != Op::None) {  // kolejne "=" powtarza ostatnie działanie
                const double v = Value();
                expression_ = Format(v) + L" " + OpSign(lastOp_) + L" " + Format(lastOperand_) + L" =";
                acc_ = Apply(lastOp_, v, lastOperand_);
            } else {
                acc_ = Value();
                expression_ = Format(acc_) + L" =";
            }
            Show(acc_);
            history_.push_back(expression_ + L" " + display_);  // karta pokazuje 3 ostatnie
            if (history_.size() > 3) history_.erase(history_.begin());
            newEntry_ = justEvaluated_ = true;
            unaryShown_ = false;
            break;
        }
        case K::C: ClearAll(); break;
        case K::CE:
            display_ = L"0";
            newEntry_ = true;
            error_ = false;
            break;
        case K::Back:
            if (newEntry_ || error_) break;
            display_.pop_back();
            if (display_.empty() || display_ == L"-") display_ = L"0";
            else display_ = Regroup(display_);
            break;
        case K::Negate:
            if (display_ != L"0") display_ = display_[0] == L'-' ? display_.substr(1) : L"-" + display_;
            if (justEvaluated_) expression_.clear(), justEvaluated_ = false;
            newEntry_ = false;
            break;
        case K::Percent: {  // jak w Windows: 200 + 10% = 220, 200 × 10% = 20
            const double v = Value();
            Show(op_ == Op::Add || op_ == Op::Sub ? acc_ * v / 100.0 : v / 100.0);
            newEntry_ = false;
            break;
        }
        case K::Inv: Unary(L"1/", Value() == 0 ? NAN : 1.0 / Value()); break;
        case K::Sqr: Unary(L"sqr", Value() * Value()); break;
        case K::Sqrt: Unary(L"√", Value() < 0 ? NAN : std::sqrt(Value())); break;
        }
        if (error_ && display_.rfind(L"Nie", 0) == 0 && k.kind == K::Sqrt) display_ = L"Nieprawidłowe dane wejściowe";
        Redraw();
    }

    // Grupy tysięcy w trakcie wpisywania (część całkowita)
    static std::wstring Regroup(const std::wstring& s) {
        std::wstring raw;
        for (wchar_t c : s)
            if (c != L'\x2009') raw += c;
        const size_t comma = raw.find(L',');
        const size_t intEnd = comma == std::wstring::npos ? raw.size() : comma;
        const size_t start = !raw.empty() && raw[0] == L'-' ? 1 : 0;
        std::wstring out;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (i > start && i < intEnd && (intEnd - i) % 3 == 0) out += L'\x2009';
            out += raw[i];
        }
        return out;
    }

    void CopyResult() {
        std::wstring s;
        for (wchar_t c : display_)
            if (c != L'\x2009') s += c;
        if (!OpenClipboard(hwnd_)) return;
        EmptyClipboard();
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t))) {
            memcpy(GlobalLock(mem), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
        CloseClipboard();
    }
    void Paste() {
        if (!OpenClipboard(hwnd_)) return;
        std::wstring s;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) s = p;
            GlobalUnlock(h);
        }
        CloseClipboard();
        std::wstring num;
        for (wchar_t c : s)
            if ((c >= L'0' && c <= L'9') || c == L',' || c == L'.' || (c == L'-' && num.empty())) num += c == L'.' ? L',' : c;
        if (num.empty() || num == L"-") return;
        display_ = Regroup(num);
        newEntry_ = false;
        Redraw();
    }

    ComPtr<IDWriteTextFormat> bigFormat_, exprFormat_, historyFormat_, keyFormat_, digitFormat_, opFormat_, glyphFormat_;
    std::vector<std::wstring> history_;
    float bigSize_ = 0.f;
    std::wstring display_ = L"0", expression_;
    double acc_ = 0, lastOperand_ = 0;
    Op op_ = Op::None, lastOp_ = Op::None;
    bool newEntry_ = true, justEvaluated_ = false, error_ = false, unaryShown_ = false;
    int hover_ = -1, pressed_ = -1;
};
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Graphics graphics;
    if (!graphics.Init()) return 1;
    auto calc = std::make_unique<Calculator>();
    if (!calc->Init(inst, graphics)) return 1;
    const int code = AppWindow::RunMessageLoop();
    calc.reset();
    return code;
}
