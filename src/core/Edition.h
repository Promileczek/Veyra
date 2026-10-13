#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

// Wersja Veyry z instalatora. Plik "veyra-minimal" obok exploder.exe → Minimal: sam wygląd powłoki (pulpit, dock,
// górny pasek, menu Start, Alt+Tab, panel "Pliki" z plikami pulpitu) — bez kropek w oknach, okien "Pliki" i aplikacji
// Veyry; zamiast nich systemowe (Eksplorator, cmd, Kalkulator, Microsoft Store, ustawienia Windows), a explorer.exe
// działa w tle (src/shell/ExplorerUi.h). Brak pliku → Full (też build deweloperski).
namespace Edition {
inline bool Minimal() {
    static const bool minimal = [] {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash) return false;
        wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - path), L"veyra-minimal");
        return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    }();
    return minimal;
}

// Folder w oknie Eksploratora Windows (Minimal); nullptr = Ten komputer
inline void OpenInExplorer(IShellItem* folder) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (folder) SHGetIDListFromObject(folder, &pidl);
    else SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidl);
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_IDLIST;
    sei.lpVerb = L"open";
    sei.lpIDList = pidl;
    sei.nShow = SW_SHOWNORMAL;
    if (pidl) ShellExecuteExW(&sei);
    CoTaskMemFree(pidl);
}
}  // namespace Edition
