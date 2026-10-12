#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

struct MediaState {
    bool hasSession = false;
    bool playing = false;
    std::wstring title;
    std::wstring artist;
    std::vector<BYTE> thumbnail;  // okładka w oryginalnym formacie (PNG/JPEG)
    unsigned thumbnailVersion = 0;
    double positionSec = 0.0;      // pozycja w chwili positionTick
    double durationSec = 0.0;
    ULONGLONG positionTick = 0;    // GetTickCount64()
    std::wstring sourcePath;       // tryb zastępczy: exe źródła (jego ikona zamiast okładki)
};

// "Teraz odtwarzane" z Windows (Spotify, przeglądarka/YouTube, odtwarzacze) — te same dane, które
// pokazuje systemowa nakładka głośności. Zdarzenia przychodzą z puli wątków; po każdej zmianie
// do okna trafia `message`, a stan odczytuje się przez Snapshot().
//
// UWAGA: serwer tych danych działa w explorer.exe — bez niego RequestAsync zwraca "klasa niezarejestrowana".
// Wtedy tryb zastępczy (Poll co ~1,5 s): tytuł okna Spotify ("Wykonawca - Utwór") albo karty YouTube
// w przeglądarce; sterowanie przez WM_APPCOMMAND (Spotify) / klawisze multimedialne (przeglądarka).
class MediaService {
public:
    MediaService();
    ~MediaService();
    void Start(HWND notify, UINT message);
    void Stop();
    MediaState Snapshot() const;
    void PlayPause();
    void Next();
    void Previous();
    bool UsesPolling() const;
    void Poll();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
