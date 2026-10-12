#pragma once
#include <string>

// Ustawienia Explodera: %LOCALAPPDATA%\Exploder\settings.txt, linie "klucz=wartość".
// Wczytywane przy pierwszym użyciu, zapisywane od razu przy zmianie.
namespace Settings {
std::wstring Get(const std::wstring& key, const std::wstring& fallback = L"");
bool GetBool(const std::wstring& key, bool fallback);
void Set(const std::wstring& key, const std::wstring& value);
void SetBool(const std::wstring& key, bool value);
}
