#pragma once
#include <windows.h>
#include <userenv.h>
#include <string>
#include <vector>
#pragma comment(lib, "userenv.lib")

// Środowisko powłoki odtwarzane z rejestru użytkownika (jak robi to Eksplorator). Inaczej Veyra przejmuje zmienne
// programu, który ją uruchomił (instalator, aktualizator, konsola — np. NO_COLOR=1 wyłączało kolory w Claude Code
// w Terminalu), i przekazuje je wszystkim uruchamianym programom. Wołane przy starcie i po WM_SETTINGCHANGE "Environment".
namespace Environment {
inline void ResetFromUser() {
    HANDLE token = nullptr;
    void* block = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token)) return;
    const bool ok = CreateEnvironmentBlock(&block, token, FALSE) != FALSE;
    CloseHandle(token);
    if (!ok || !block) return;
    // Najpierw usuwamy bieżące zmienne (bez ukrytych "=C:" itp.), potem ustawiamy te z rejestru
    std::vector<std::wstring> names;
    if (wchar_t* current = GetEnvironmentStringsW()) {
        for (const wchar_t* p = current; *p; p += wcslen(p) + 1) {
            const wchar_t* eq = wcschr(p + 1, L'=');
            if (*p != L'=' && eq) names.emplace_back(p, eq);
        }
        FreeEnvironmentStringsW(current);
    }
    for (const std::wstring& n : names) SetEnvironmentVariableW(n.c_str(), nullptr);
    for (const wchar_t* p = static_cast<const wchar_t*>(block); *p; p += wcslen(p) + 1) {
        const wchar_t* eq = wcschr(p + 1, L'=');
        if (*p == L'=' || !eq) continue;
        SetEnvironmentVariableW(std::wstring(p, eq).c_str(), eq + 1);
    }
    DestroyEnvironmentBlock(block);
}
}  // namespace Environment
