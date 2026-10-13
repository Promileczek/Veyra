#pragma once
#include "../core/Graphics.h"
#include "../files/FileView.h"
#include <functional>

// Panel "Pliki" na pulpicie (po lewej od panelu aplikacji): mini-Exploder rysowany przez Desktop.
//  - kropki: czerwona = zamknij (otwiera się z powrotem kafelkiem "Pliki" w panelu na środku),
//            żółta = zwiń do paska tytułu, zielona = otwórz folder w dużym oknie Explodera,
//  - przeciąganie za pasek tytułu, zmiana rozmiaru za krawędzie/róg,
//  - stan (widoczność, zwinięcie, położenie, folder) zapisywany w %LOCALAPPDATA%\Exploder\files-widget.txt.
class FilesWidget {
public:
    struct Callbacks {
        std::function<void()> redraw;                        // przerysuj warstwę ikon
        std::function<void()> glassChanged;                  // nowe położenie/rozmiar → przelicz szkło pod panelem
        std::function<void(ComPtr<IShellItem>)> openWindow;  // duże okno Explodera
    };
    void Init(Graphics& g, HWND owner, UINT changedMessage, Callbacks callbacks);
    void SetDefaultRect(const D2D1_RECT_F& r);  // gdy nie ma zapisanego stanu
    void ClampTo(const D2D1_RECT_F& work);

    bool Visible() const { return visible_; }
    void Show();
    void Disable() { disabled_ = true; visible_ = false; }  // Veyra Minimal: panelu nie ma
    D2D1_RECT_F Bounds() const;  // pusty prostokąt, gdy zamknięty
    bool Dragging() const { return drag_ != Drag::None; }
    bool Focused() const { return focused_; }
    void SetFocused(bool f);
    FileView& View() { return view_; }

    void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float dpi);
    bool Contains(D2D1_POINT_2F p) const;
    bool OnMouseDown(D2D1_POINT_2F p, bool ctrl, bool shift);  // true = przejęte (gospodarz robi SetCapture)
    void OnMouseMove(D2D1_POINT_2F p);
    void OnMouseUp(D2D1_POINT_2F p);
    void OnMouseLeave();
    void OnDoubleClick(D2D1_POINT_2F p);
    void OnRightClick(D2D1_POINT_2F p, POINT screen);
    void OnWheel(int delta);
    bool OnKey(UINT vk, bool ctrl, bool shift, bool alt);
    bool OnChar(wchar_t ch);
    HCURSOR CursorAt(D2D1_POINT_2F p) const;

private:
    enum class Drag { None, Move, Resize, View };
    enum Edge { EdgeNone = 0, EdgeLeft = 1, EdgeRight = 2, EdgeTop = 4, EdgeBottom = 8 };
    int EdgeAt(D2D1_POINT_2F p) const;
    D2D1_POINT_2F LightsOrigin() const { return {rect_.right - 58.f, rect_.top + 20.f}; }  // po prawej, jak w oknach
    D2D1_RECT_F BackButton() const;
    D2D1_RECT_F NewFolderButton() const;
    void UpdateViewBounds();
    void Load();
    void Save() const;

    Graphics* g_ = nullptr;
    Callbacks cb_;
    FileView view_;
    ComPtr<IDWriteTextFormat> titleFormat_, glyphFormat_;

    D2D1_RECT_F rect_{};  // pełny rozmiar (także gdy zwinięty)
    bool hasSaved_ = false;
    bool visible_ = true;
    bool disabled_ = false;
    bool collapsed_ = false;
    bool focused_ = false;
    bool hoverLights_ = false;
    int hoverButton_ = 0;  // 1 = wstecz, 2 = nowy folder

    Drag drag_ = Drag::None;
    int edges_ = EdgeNone;
    D2D1_POINT_2F dragStart_{};
    D2D1_RECT_F startRect_{};
};
