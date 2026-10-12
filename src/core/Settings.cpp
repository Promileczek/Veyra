#include "Settings.h"
#include <windows.h>
#include <cstdio>
#include <map>

namespace {
std::map<std::wstring, std::wstring>& Values() {
    static std::map<std::wstring, std::wstring> values;
    return values;
}
bool g_loaded = false;

std::wstring FilePath() {
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    std::wstring dir = std::wstring(base, n) + L"\\Exploder";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.txt";
}

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    FILE* f = nullptr;
    if (_wfopen_s(&f, FilePath().c_str(), L"r, ccs=UTF-8") || !f) return;
    wchar_t line[4096];
    while (fgetws(line, ARRAYSIZE(line), f)) {
        std::wstring s = line;
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
        const size_t eq = s.find(L'=');
        if (eq != std::wstring::npos && eq > 0) Values()[s.substr(0, eq)] = s.substr(eq + 1);
    }
    fclose(f);
}

void Save() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, FilePath().c_str(), L"w, ccs=UTF-8") || !f) return;
    for (const auto& [k, v] : Values()) fwprintf(f, L"%s=%s\n", k.c_str(), v.c_str());
    fclose(f);
}
}  // namespace

namespace Settings {
std::wstring Get(const std::wstring& key, const std::wstring& fallback) {
    Load();
    const auto it = Values().find(key);
    return it == Values().end() ? fallback : it->second;
}

void Reload() {
    g_loaded = false;
    Values().clear();
}

unsigned int ChangedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"VeyraSettingsChanged");
    return msg;
}

bool GetBool(const std::wstring& key, bool fallback) {
    const std::wstring v = Get(key);
    return v.empty() ? fallback : v == L"1";
}

void Set(const std::wstring& key, const std::wstring& value) {
    Load();
    Values()[key] = value;
    Save();
}

void SetBool(const std::wstring& key, bool value) { Set(key, value ? L"1" : L"0"); }
}  // namespace Settings
