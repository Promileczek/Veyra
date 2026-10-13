#pragma once
// Aktualizacje Veyry (Ustawienia → Aktualizacje).
// Sprawdzanie: najnowsze wydanie na GitHubie (Releases: tag vX.Y + plik .zip z gotowymi programami); bez wydania —
// plik VERSION z repozytorium. "Co nowego": tytuły ostatnich commitów.
// Aktualizacja: skrypt PowerShell w tle — z wydania pobiera gotowe pliki; bez wydania pobiera kod i buduje go
// (Visual Studio Build Tools). Dopiero potem zamyka Veyrę, podmienia pliki i uruchamia ją ponownie.
// Postęp: %LOCALAPPDATA%\Exploder\update\status.txt ("OK <wersja>" / "BŁĄD <opis>" na końcu), dziennik: update.log, build.log.
// Źródło można zmienić w settings.txt (np. na własny serwer): updateReleaseUrl, updateVersionUrl, updateZipUrl, updateCommitsUrl.
#include "../../src/core/Settings.h"
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <vector>

namespace Updater {

inline std::wstring Current() { return L"" VEYRA_VERSION; }

inline std::wstring ReleaseUrl() {
    return Settings::Get(L"updateReleaseUrl", L"https://api.github.com/repos/Promileczek/Veyra/releases/latest");
}
inline std::wstring VersionUrl() {
    return Settings::Get(L"updateVersionUrl", L"https://raw.githubusercontent.com/Promileczek/Veyra/main/VERSION");
}
inline std::wstring ZipUrl() { return Settings::Get(L"updateZipUrl", L"https://codeload.github.com/Promileczek/Veyra/zip/refs/heads/main"); }
inline std::wstring CommitsUrl() {
    return Settings::Get(L"updateCommitsUrl", L"https://api.github.com/repos/Promileczek/Veyra/commits?per_page=6");
}

inline std::wstring Dir() {
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring dir = std::wstring(base, n) + L"\\Exploder\\update";
    CreateDirectoryW((std::wstring(base, n) + L"\\Exploder").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// GET przez WinHTTP (HTTPS, przekierowania); zwraca kod HTTP albo 0 przy błędzie sieci
inline int HttpGet(const std::wstring& url, std::string& body) {
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    wchar_t extra[1024]{};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = ARRAYSIZE(extra);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return 0;
    int status = 0;
    HINTERNET session = WinHttpOpen(L"Veyra-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"Veyra-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return 0;
    WinHttpSetTimeouts(session, 8000, 8000, 10000, 15000);
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    const std::wstring object = std::wstring(path) + extra;
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                         : nullptr;
    if (req && WinHttpSendRequest(req, L"Accept: application/vnd.github+json\r\nCache-Control: no-cache\r\n", static_cast<DWORD>(-1L),
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD code = 0, size = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size,
                            WINHTTP_NO_HEADER_INDEX);
        status = static_cast<int>(code);
        for (DWORD avail = 0; WinHttpQueryDataAvailable(req, &avail) && avail;) {
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (!WinHttpReadData(req, chunk.data(), avail, &got) || !got) break;
            body.append(chunk.data(), got);
            if (body.size() > 4 * 1024 * 1024) break;
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return status;
}

inline std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// -1: a < b, 0: równe, 1: a > b ("v1.2.10" > "1.2.9")
inline int Compare(std::wstring a, std::wstring b) {
    auto parts = [](std::wstring s) {
        if (!s.empty() && (s[0] == L'v' || s[0] == L'V')) s.erase(0, 1);
        std::vector<int> v;
        size_t i = 0;
        while (i < s.size()) {
            const size_t dot = s.find(L'.', i);
            v.push_back(_wtoi(s.substr(i, dot == std::wstring::npos ? std::wstring::npos : dot - i).c_str()));
            if (dot == std::wstring::npos) break;
            i = dot + 1;
        }
        return v;
    };
    const auto pa = parts(a), pb = parts(b);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

struct CheckResult {
    bool ok = false;
    std::wstring latest;
    std::wstring binaryZip;             // adres .zip z gotowymi programami (wydanie); pusty = budowanie z kodu
    std::vector<std::wstring> changes;  // "12.10 · tytuł commita"
    std::wstring error;
};

// Wartość pola tekstowego JSON ("key":"value") od pozycji from
inline std::string JsonString(const std::string& json, const std::string& key, size_t from = 0) {
    const size_t k = json.find("\"" + key + "\":\"", from);
    if (k == std::string::npos) return {};
    std::string out;
    for (size_t i = k + key.size() + 4; i < json.size() && json[i] != '"'; ++i) out += json[i] == '\\' && i + 1 < json.size() ? json[++i] : json[i];
    return out;
}

inline CheckResult Check() {
    CheckResult r;
    // 1) Wydanie na GitHubie: gotowe pliki
    std::string rel;
    if (HttpGet(ReleaseUrl(), rel) == 200) {
        const std::string tag = JsonString(rel, "tag_name");
        const size_t assets = rel.find("\"assets\"");
        for (size_t pos = assets; assets != std::string::npos && (pos = rel.find("\"browser_download_url\":\"", pos)) != std::string::npos; ++pos) {
            const std::string url = JsonString(rel, "browser_download_url", pos);
            if (url.size() > 4 && _stricmp(url.c_str() + url.size() - 4, ".zip") == 0) {
                r.binaryZip = FromUtf8(url);
                break;
            }
        }
        if (!tag.empty() && !r.binaryZip.empty()) {
            r.latest = FromUtf8(tag);
            if (!r.latest.empty() && (r.latest[0] == L'v' || r.latest[0] == L'V')) r.latest.erase(0, 1);
            r.ok = true;
        }
    }
    // 2) Bez wydania: plik VERSION z repozytorium (aktualizacja = budowanie z kodu)
    std::string body;
    const int code = r.ok ? 200 : HttpGet(VersionUrl(), body);
    if (!r.ok) {
        if (code == 0) {
            r.error = L"Brak połączenia z serwerem aktualizacji";
            return r;
        }
        if (code != 200) {
            r.error = L"Serwer aktualizacji zwrócił błąd " + std::to_wstring(code);
            return r;
        }
        std::wstring v = FromUtf8(body);
        const size_t a = v.find_first_not_of(L" \t\r\n\xFEFF"), b = v.find_last_not_of(L" \t\r\n");
        r.latest = a == std::wstring::npos ? L"" : v.substr(a, b - a + 1);
        r.ok = !r.latest.empty() && r.latest.size() < 32;
        if (!r.ok) r.error = L"Nieprawidłowy plik wersji";
    }

    // "Co nowego": tytuły ostatnich commitów (pole "message", tylko pierwsza linia) + data
    std::string json;
    if (HttpGet(CommitsUrl(), json) == 200) {
        size_t pos = 0;
        while (r.changes.size() < 6 && (pos = json.find("\"message\":\"", pos)) != std::string::npos) {
            pos += 11;
            std::string msg;
            for (; pos < json.size() && json[pos] != '"'; ++pos) {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    const char e = json[++pos];
                    if (e == 'n') break;  // tylko pierwsza linia
                    msg += e == 't' ? ' ' : e;
                } else {
                    msg += json[pos];
                }
            }
            // Data commita: ostatnie "date":"YYYY-MM-DD…" przed "message"
            const size_t d = json.rfind("\"date\":\"", pos);
            std::wstring when;
            if (d != std::string::npos && d + 18 < json.size()) when = FromUtf8(json.substr(d + 16, 2) + "." + json.substr(d + 13, 2)) + L" · ";
            if (!msg.empty()) r.changes.push_back(when + FromUtf8(msg));
        }
    }
    return r;
}

inline bool BuildToolsAvailable() {
    wchar_t pf[MAX_PATH]{};
    GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, MAX_PATH);
    return GetFileAttributesW((std::wstring(pf) + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Skrypt aktualizacji (zapisywany z BOM — PowerShell 5 czyta wtedy polskie znaki poprawnie)
constexpr char kScript[] = R"PS(param([string]$Dest, [string]$ZipUrl, [string]$Version, [switch]$Binary, [switch]$Elevated)
$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'
# Skrypt jako administrator (instalacja w Program Files) → Veyrę i Ustawienia uruchamiamy z uprawnieniami zwykłego użytkownika
function Run($exe, $arg) {
  if ($Elevated) { Start-Process runas.exe -ArgumentList ('/trustlevel:0x20000 "\"' + $exe + '\" ' + $arg + '"') -WindowStyle Hidden }
  elseif ($arg) { Start-Process $exe -ArgumentList $arg } else { Start-Process $exe }
}
$root = Join-Path $env:LOCALAPPDATA 'Exploder\update'
$status = Join-Path $root 'status.txt'; $log = Join-Path $root 'update.log'
function S($t) { Set-Content -Path $status -Value $t -Encoding UTF8; Add-Content -Path $log -Value ((Get-Date -Format 'HH:mm:ss') + ' ' + $t) -Encoding UTF8 }
try {
  if ($Binary) { S 'Pobieranie nowej wersji…' } else { S 'Pobieranie kodu z GitHuba…' }
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  $zip = Join-Path $root 'veyra.zip'
  Invoke-WebRequest -Uri $ZipUrl -OutFile $zip -UseBasicParsing
  S 'Rozpakowywanie…'
  $src = Join-Path $root 'src'
  if (Test-Path $src) { Remove-Item $src -Recurse -Force }
  Expand-Archive $zip $src -Force
  if ($Binary) {
    # Gotowe pliki: exploder.exe w głównym folderze archiwum albo w jedynym podfolderze
    $bld = $src
    if (-not (Test-Path (Join-Path $bld 'exploder.exe'))) { $bld = (Get-ChildItem $src -Directory | Select-Object -First 1).FullName }
    if (-not (Test-Path (Join-Path $bld 'exploder.exe'))) { throw 'w pobranym archiwum brak exploder.exe' }
  } else {
  $proj = (Get-ChildItem $src -Directory | Select-Object -First 1).FullName
  S 'Budowanie — to potrwa 1–3 minuty…'
  $bld = Join-Path $root 'build'
  $bat = Join-Path $root 'build.cmd'
  Set-Content -Path $bat -Encoding ASCII -Value @(
    '@echo off',
    'set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"',
    'for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSDIR=%%i',
    'call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1',
    'cmake -S "%~1" -B "%~2" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1',
    'cmake --build "%~2" || exit /b 1')
  $p = Start-Process cmd.exe -ArgumentList ('/c ""' + $bat + '" "' + $proj + '" "' + $bld + '""') -WindowStyle Hidden -Wait -PassThru `
       -RedirectStandardOutput (Join-Path $root 'build.log') -RedirectStandardError (Join-Path $root 'build-err.log')
  if ($p.ExitCode -ne 0) { throw 'budowanie nie powiodło się (szczegóły: update\build.log)' }
  }
  S 'Instalowanie…'
  $hadExplorer = [bool](Get-Process explorer -ErrorAction SilentlyContinue)
  $hadShell = [bool](Get-Process exploder -ErrorAction SilentlyContinue)
  # taskkill /f (kod wyjścia 1): po Stop-Process (kod -1) Windows sam uruchamia Eksplorator ponownie
  & taskkill.exe /f /im exploder.exe /im exploder-terminal.exe /im exploder-calc.exe /im exploder-store.exe /im exploder-settings.exe 2>&1 | Out-Null
  Start-Sleep 1
  Copy-Item (Join-Path $bld '*.exe') $Dest -Force
  Copy-Item (Join-Path $bld '*.pdb') $Dest -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force (Join-Path $Dest 'wallpapers') | Out-Null
  if (Test-Path (Join-Path $bld 'wallpapers')) { Copy-Item (Join-Path $bld 'wallpapers\*') (Join-Path $Dest 'wallpapers') -Force }
  if (-not $hadExplorer) {  # Eksplorator potrafi wrócić sam — zamykamy, aż zniknie
    for ($i = 0; $i -lt 5 -and (Get-Process explorer -ErrorAction SilentlyContinue); $i++) {
      & taskkill.exe /f /im explorer.exe 2>&1 | Out-Null; Start-Sleep -Milliseconds 1500
    }
  }
  if ($hadShell) { Run (Join-Path $Dest 'exploder.exe') '' }
  S ('OK ' + $Version)
  Start-Sleep 2
  Run (Join-Path $Dest 'exploder-settings.exe') '--aktualizacje'
} catch {
  S ('BŁĄD ' + $_.Exception.Message)
}
)PS";

// Zapisuje skrypt i uruchamia go w tle (bez okna). false = nie udało się uruchomić.
// binaryZip: adres gotowych plików z wydania; pusty = pobranie kodu i budowanie
inline bool Start(const std::wstring& exeDir, const std::wstring& version, const std::wstring& binaryZip) {
    const std::wstring dir = Dir();
    const std::wstring script = dir + L"\\veyra-update.ps1";
    FILE* f = nullptr;
    if (_wfopen_s(&f, script.c_str(), L"wb") || !f) return false;
    fwrite("\xEF\xBB\xBF", 1, 3, f);
    fwrite(kScript, 1, sizeof(kScript) - 1, f);
    fclose(f);
    if (FILE* s = nullptr; !_wfopen_s(&s, (dir + L"\\status.txt").c_str(), L"w, ccs=UTF-8") && s) {
        fputws(L"Uruchamianie aktualizacji…", s);
        fclose(s);
    }
    std::wstring dest = exeDir;
    if (!dest.empty() && dest.back() == L'\\') dest.pop_back();
    // Folder bez prawa zapisu (instalacja w Program Files) → skrypt jako administrator (prośba UAC)
    const HANDLE probe = CreateFileW((dest + L"\\.veyra-zapis").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    const bool elevate = probe == INVALID_HANDLE_VALUE;
    if (!elevate) CloseHandle(probe);
    const std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\" -Dest \"" + dest +
                              L"\" -ZipUrl \"" + (binaryZip.empty() ? ZipUrl() : binaryZip) + L"\" -Version \"" + version + L"\"" +
                              (binaryZip.empty() ? L"" : L" -Binary") + (elevate ? L" -Elevated" : L"");
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, elevate ? L"runas" : L"open", L"powershell.exe", args.c_str(), nullptr, SW_HIDE)) > 32;
}

// Bieżący stan skryptu (pusty = brak)
inline std::wstring Status() {
    std::wstring out;
    FILE* f = nullptr;
    if (_wfopen_s(&f, (Dir() + L"\\status.txt").c_str(), L"r, ccs=UTF-8") || !f) return out;
    wchar_t line[1024];
    if (fgetws(line, ARRAYSIZE(line), f)) out = line;
    fclose(f);
    while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r')) out.pop_back();
    return out;
}

}  // namespace Updater
