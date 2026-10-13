#pragma once
// Zainstalowane programy (jak „Aplikacje” w Ustawieniach Windows, które w Veyrze Full nie działają) i ich odinstalowanie.
// Źródło: klucze Uninstall w HKLM (64 i 32 bit) oraz HKCU; bez składników systemu i aktualizacji.
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <string>
#include <vector>

namespace Programs {
struct Program {
    std::wstring name, publisher, version, uninstall;
    DWORD sizeKb = 0;
    bool machine = false;  // HKLM — odinstalowanie zwykle wymaga uprawnień administratora
};

namespace detail {
inline std::wstring Value(HKEY key, const wchar_t* name) {
    wchar_t buf[2048]{};
    DWORD size = sizeof(buf);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}
inline DWORD Dword(HKEY key, const wchar_t* name) {
    DWORD v = 0, size = sizeof(v);
    RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v;
}
inline void Scan(HKEY root, REGSAM view, bool machine, std::vector<Program>& out) {
    HKEY base = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &base) != ERROR_SUCCESS) return;
    wchar_t sub[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = ARRAYSIZE(sub);
        if (RegEnumKeyExW(base, i, sub, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        HKEY k = nullptr;
        if (RegOpenKeyExW(base, sub, 0, KEY_READ | view, &k) != ERROR_SUCCESS) continue;
        Program p;
        p.name = Value(k, L"DisplayName");
        p.uninstall = Value(k, L"UninstallString");
        const bool hidden = Dword(k, L"SystemComponent") == 1 || !Value(k, L"ParentKeyName").empty() ||
                            Value(k, L"ReleaseType").find(L"Update") != std::wstring::npos;
        if (!hidden && !p.name.empty() && !p.uninstall.empty()) {
            p.publisher = Value(k, L"Publisher");
            p.version = Value(k, L"DisplayVersion");
            p.sizeKb = Dword(k, L"EstimatedSize");
            p.machine = machine;
            if (std::none_of(out.begin(), out.end(), [&](const Program& o) { return o.name == p.name && o.version == p.version; }))
                out.push_back(std::move(p));
        }
        RegCloseKey(k);
    }
    RegCloseKey(base);
}
}  // namespace detail

inline std::vector<Program> List() {
    std::vector<Program> out;
    detail::Scan(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, true, out);
    detail::Scan(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, true, out);
    detail::Scan(HKEY_CURRENT_USER, 0, false, out);
    std::sort(out.begin(), out.end(), [](const Program& a, const Program& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.name.c_str(), -1, b.name.c_str(), -1, nullptr, nullptr, 0) ==
               CSTR_LESS_THAN;
    });
    return out;
}

// Uruchamia deinstalator programu (jego własne okno). MSI: /I{…} → /X{…}. HKLM → prośba UAC.
inline bool Uninstall(const Program& p) {
    std::wstring cmd = p.uninstall, file, args;
    if (!cmd.empty() && cmd[0] == L'"') {
        const size_t end = cmd.find(L'"', 1);
        file = cmd.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
        args = end == std::wstring::npos ? L"" : cmd.substr(end + 1);
    } else {
        std::wstring low = cmd;
        std::transform(low.begin(), low.end(), low.begin(), ::towlower);
        const size_t exe = low.find(L".exe");
        file = exe == std::wstring::npos ? cmd : cmd.substr(0, exe + 4);
        args = exe == std::wstring::npos ? L"" : cmd.substr(exe + 4);
    }
    std::wstring lowFile = file;
    std::transform(lowFile.begin(), lowFile.end(), lowFile.begin(), ::towlower);
    if (lowFile.find(L"msiexec") != std::wstring::npos)
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == L'/' && (args[i + 1] == L'I' || args[i + 1] == L'i')) args[i + 1] = L'X';
    wchar_t expanded[MAX_PATH * 2]{};
    ExpandEnvironmentStringsW(file.c_str(), expanded, ARRAYSIZE(expanded));
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.lpVerb = p.machine ? L"runas" : L"open";
    sei.lpFile = expanded;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}
}  // namespace Programs
