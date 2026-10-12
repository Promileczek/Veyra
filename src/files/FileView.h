#pragma once
#include "../core/Graphics.h"
#include <shobjidl.h>
#include <functional>
#include <string>
#include <thread>
#include <vector>

struct FileItem {
    ComPtr<IShellItem> item;
    std::wstring name;     // nazwa wyświetlana
    std::wstring parsing;  // pełna nazwa powłoki (identyfikator)
    bool folder = false;
    bool selected = false;
    bool iconLoaded = false;
    ComPtr<IWICBitmapSource> pendingIcon;  // wczytana przed rysowaniem, zamieniana na bitmapę D2D w Draw
    ComPtr<ID2D1Bitmap> icon;
    ComPtr<IDWriteTextLayout> label;
};

// Zawartość folderu jako siatka ikon (wspólna dla panelu "Pliki" na pulpicie i okien Explodera).
// Nie ma własnego okna: gospodarz podaje prostokąt, rysuje ją w swoim kontekście D2D i przekazuje zdarzenia
// (współrzędne w DIP, w tym samym układzie co SetBounds). Obsługuje: nawigację z historią, zaznaczanie
// (klik, Ctrl, Shift, ramka), otwieranie, nowy folder, zmianę nazwy w miejscu, usuwanie, kopiuj/wytnij/wklej
// i menu kontekstowe Windows. Zmiany w folderze na dysku → `changedMessage` do okna gospodarza.
class FileView {
public:
    ~FileView();
    void Init(Graphics& g, HWND owner, UINT changedMessage, std::function<void()> requestRedraw);

    bool Navigate(ComPtr<IShellItem> folder, bool addToHistory = true);
    bool NavigateKnown(REFKNOWNFOLDERID id);
    bool NavigateParsing(const std::wstring& parsing);
    void Back();
    void Forward();
    void Up();
    void Reload();
    bool CanBack() const { return historyPos_ > 0; }
    bool CanForward() const { return historyPos_ + 1 < history_.size(); }
    bool CanUp() const;
    std::wstring FolderName() const { return folderName_; }
    std::wstring FolderParsing() const;
    IShellItem* Folder() const { return folder_.Get(); }
    size_t ItemCount() const { return items_.size(); }

    void SetBounds(const D2D1_RECT_F& r);
    const D2D1_RECT_F& Bounds() const { return bounds_; }
    // Ikony/miniatury wczytuje się PRZED rysowaniem: wywołania powłoki potrafią przetworzyć inne komunikaty
    // (np. odświeżenie folderu), a to w trakcie Draw zmieniałoby listę pod pętlą rysowania (awaria).
    void PrepareIcons(float dpi);
    void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float dpi);

    void OnMouseDown(D2D1_POINT_2F p, bool ctrl, bool shift);
    void OnMouseMove(D2D1_POINT_2F p);
    void OnMouseUp(D2D1_POINT_2F p);
    void OnMouseLeave();
    void OnDoubleClick(D2D1_POINT_2F p);
    void OnRightClick(D2D1_POINT_2F p, POINT screen);
    void OnWheel(int delta);
    bool OnKey(UINT vk, bool ctrl, bool shift, bool alt);  // true = obsłużone
    bool OnChar(wchar_t ch);
    bool Renaming() const { return renaming_ >= 0; }
    void CommitRename(bool accept);

    void NewFolder();
    // Podmenu w menu kontekstowym ("Wyślij do", "Nowy") — gospodarz przekazuje WM_INITMENUPOPUP/DRAWITEM/MEASUREITEM
    bool HandleMenuMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);

    // Opcjonalnie: "Otwórz w nowym oknie" w menu (gospodarz tworzy okno Explodera)
    std::function<void(ComPtr<IShellItem>)> openInNewWindow;
    // Opcjonalnie: tryb wyboru — otwarcie pliku (nie folderu) wybiera go zamiast uruchamiać
    std::function<void(ComPtr<IShellItem>)> pickFile;

private:
    enum class Mode { None, Pending, Selecting };

    void Load();
    void StartWatcher();
    void StopWatcher();
    void OpenItem(size_t index);
    void OpenSelected();
    void DeleteSelected(bool permanent);
    void CopySelected(bool cut);
    void Paste();
    void StartRename(size_t index);
    void SelectOnly(int index);
    void EnsureVisible(size_t index);
    std::vector<size_t> Selected() const;
    void ShowBackgroundMenu(POINT screen);
    void ShowItemMenu(size_t index, POINT screen);

    int Columns() const;
    D2D1_RECT_F CellRect(size_t index) const;  // z uwzględnieniem przewinięcia
    int ItemAt(D2D1_POINT_2F p) const;
    float ContentHeight() const;
    void ClampScroll();

    Graphics* g_ = nullptr;
    HWND owner_ = nullptr;
    UINT changedMessage_ = 0;
    std::function<void()> requestRedraw_;
    ComPtr<IDWriteTextFormat> labelFormat_, emptyFormat_;
    ComPtr<IDWriteInlineObject> ellipsis_;

    ComPtr<IShellItem> folder_;
    std::wstring folderName_;
    std::vector<ComPtr<IShellItem>> history_;
    size_t historyPos_ = 0;
    std::vector<FileItem> items_;

    D2D1_RECT_F bounds_{};
    float scroll_ = 0.f;
    int hover_ = -1;
    int anchor_ = -1;  // początek zaznaczenia z Shift
    Mode mode_ = Mode::None;
    D2D1_POINT_2F downPt_{}, curPt_{};
    int downItem_ = -1;

    int renaming_ = -1;
    std::wstring editText_;
    size_t caret_ = 0;

    IContextMenu2* activeMenu_ = nullptr;

    std::thread watcher_;
    HANDLE stopEvent_ = nullptr;
};
