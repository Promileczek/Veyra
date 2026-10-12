#include "MediaService.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <atomic>
#include <chrono>
#include <mutex>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

using Manager = GlobalSystemMediaTransportControlsSessionManager;
using Session = GlobalSystemMediaTransportControlsSession;

struct MediaService::Impl : std::enable_shared_from_this<MediaService::Impl> {
    HWND hwnd = nullptr;
    UINT message = 0;
    std::atomic<bool> stopped{false};
    std::atomic<bool> polling{false};  // tryb zastępczy (brak explorera)

    // Tryb zastępczy
    HWND source = nullptr;
    bool sourceIsSpotify = false;
    std::wstring lastTitle, lastArtist;

    mutable std::mutex stateMutex;
    MediaState state;

    std::recursive_mutex sessionMutex;  // manager/session i subskrypcje
    Manager manager{nullptr};
    Session session{nullptr};
    event_token sessionChanged{}, propsChanged{}, playbackChanged{}, timelineChanged{};

    void Notify() {
        if (!stopped) PostMessageW(hwnd, message, 0, 0);
    }

    Session Current() {
        std::lock_guard lock(sessionMutex);
        return session;
    }

    // Wywoływane z puli wątków (MTA) — tam wolno czekać na operacje asynchroniczne przez .get().
    void Attach() {
        std::weak_ptr<Impl> weak = weak_from_this();
        {
            std::lock_guard lock(sessionMutex);
            if (stopped || !manager) return;
            if (session) {
                session.MediaPropertiesChanged(propsChanged);
                session.PlaybackInfoChanged(playbackChanged);
                session.TimelinePropertiesChanged(timelineChanged);
            }
            session = manager.GetCurrentSession();
            if (session) {
                propsChanged = session.MediaPropertiesChanged([weak](auto&&, auto&&) {
                    if (auto self = weak.lock()) self->UpdateProperties();
                });
                playbackChanged = session.PlaybackInfoChanged([weak](auto&&, auto&&) {
                    if (auto self = weak.lock()) self->UpdatePlayback();
                });
                timelineChanged = session.TimelinePropertiesChanged([weak](auto&&, auto&&) {
                    if (auto self = weak.lock()) self->UpdatePlayback();
                });
            }
        }
        UpdateProperties();
        UpdatePlayback();
    }

    void UpdateProperties() {
        const Session s = Current();
        MediaState fresh;
        if (s) {
            try {
                const auto props = s.TryGetMediaPropertiesAsync().get();
                fresh.hasSession = true;
                fresh.title = props.Title();
                fresh.artist = props.Artist();
                if (const auto thumb = props.Thumbnail()) {
                    const auto stream = thumb.OpenReadAsync().get();
                    const uint32_t size = static_cast<uint32_t>(std::min<uint64_t>(stream.Size(), 8u << 20));
                    Buffer buffer(size);
                    const auto read = stream.ReadAsync(buffer, size, InputStreamOptions::None).get();
                    fresh.thumbnail.assign(read.data(), read.data() + read.Length());
                }
            } catch (const hresult_error&) {
            }
        }
        {
            std::lock_guard lock(stateMutex);
            state.hasSession = fresh.hasSession;
            state.title = std::move(fresh.title);
            state.artist = std::move(fresh.artist);
            if (fresh.thumbnail != state.thumbnail) {
                state.thumbnail = std::move(fresh.thumbnail);
                ++state.thumbnailVersion;
            }
        }
        Notify();
    }

    void UpdatePlayback() {
        const Session s = Current();
        bool playing = false;
        double position = 0.0, duration = 0.0;
        if (s) {
            try {
                playing = s.GetPlaybackInfo().PlaybackStatus() ==
                          GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                const auto timeline = s.GetTimelineProperties();
                using seconds = std::chrono::duration<double>;
                duration = std::chrono::duration_cast<seconds>(timeline.EndTime() - timeline.StartTime()).count();
                position = std::chrono::duration_cast<seconds>(timeline.Position() - timeline.StartTime()).count();
                if (playing) {  // pozycja jest z chwili LastUpdatedTime — doliczamy czas, który minął
                    const auto age = clock::now() - timeline.LastUpdatedTime();
                    position += std::max(0.0, std::chrono::duration_cast<seconds>(age).count());
                }
            } catch (const hresult_error&) {
            }
        }
        {
            std::lock_guard lock(stateMutex);
            state.playing = playing;
            state.positionSec = std::min(position, duration > 0 ? duration : position);
            state.durationSec = duration;
            state.positionTick = GetTickCount64();
        }
        Notify();
    }
};

MediaService::MediaService() : impl_(std::make_shared<Impl>()) {}
MediaService::~MediaService() { Stop(); }

void MediaService::Start(HWND notify, UINT message) {
    impl_->hwnd = notify;
    impl_->message = message;
    std::weak_ptr<Impl> weak = impl_;
    try {
        Manager::RequestAsync().Completed([weak](const IAsyncOperation<Manager>& op, AsyncStatus status) {
            auto self = weak.lock();
            if (!self) return;
            if (status != AsyncStatus::Completed) {
                self->polling = true;
                return;
            }
            {
                std::lock_guard lock(self->sessionMutex);
                if (self->stopped) return;
                self->manager = op.GetResults();
                self->sessionChanged = self->manager.CurrentSessionChanged([weak](auto&&, auto&&) {
                    if (auto s = weak.lock()) s->Attach();
                });
            }
            self->Attach();
        });
    } catch (const hresult_error&) {
        impl_->polling = true;
    }
}

void MediaService::Stop() {
    if (!impl_ || impl_->stopped.exchange(true)) return;
    std::lock_guard lock(impl_->sessionMutex);
    try {
        if (impl_->session) {
            impl_->session.MediaPropertiesChanged(impl_->propsChanged);
            impl_->session.PlaybackInfoChanged(impl_->playbackChanged);
            impl_->session.TimelinePropertiesChanged(impl_->timelineChanged);
        }
        if (impl_->manager) impl_->manager.CurrentSessionChanged(impl_->sessionChanged);
    } catch (const hresult_error&) {
    }
    impl_->session = nullptr;
    impl_->manager = nullptr;
}

MediaState MediaService::Snapshot() const {
    std::lock_guard lock(impl_->stateMutex);
    return impl_->state;
}

namespace {
std::wstring ProcessPath(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return {};
    wchar_t path[MAX_PATH * 2]{};
    DWORD len = ARRAYSIZE(path);
    const BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &len);
    CloseHandle(proc);
    return ok ? std::wstring(path, len) : std::wstring();
}

bool EndsWithI(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

void SendMediaKey(WORD vk) {
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

struct Candidate {
    HWND hwnd = nullptr;
    std::wstring title, path;
};

// Polecenia w trybie zastępczym: Spotify rozumie WM_APPCOMMAND, przeglądarki — klawisze multimedialne.
void FallbackCommand(HWND source, bool spotify, short appCommand, WORD mediaKey) {
    if (spotify && IsWindow(source))
        PostMessageW(source, WM_APPCOMMAND, reinterpret_cast<WPARAM>(source), MAKELPARAM(0, appCommand));
    else
        SendMediaKey(mediaKey);
}
}  // namespace

bool MediaService::UsesPolling() const { return impl_->polling; }

void MediaService::Poll() {
    Candidate found[2];  // [0] Spotify, [1] przeglądarka z YouTube
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto* c = reinterpret_cast<Candidate*>(p);
            if (GetWindow(h, GW_OWNER)) return TRUE;
            wchar_t title[512]{};
            if (!GetWindowTextW(h, title, ARRAYSIZE(title))) return TRUE;
            const std::wstring t = title;
            // Karta przeglądarki: YouTube albo Spotify w przeglądarce ("Utwór • Wykonawca")
            const bool youtube = t.find(L"YouTube") != std::wstring::npos || t.find(L" \x2022 ") != std::wstring::npos;
            if (c[0].hwnd && (c[1].hwnd || !youtube)) return TRUE;
            const std::wstring path = ProcessPath(h);
            if (!c[0].hwnd && EndsWithI(path, L"\\Spotify.exe") &&
                (IsWindowVisible(h) || t.find(L" - ") != std::wstring::npos)) {  // też schowany do zasobnika
                c[0] = {h, t, path};
            } else if (!c[1].hwnd && youtube &&
                       (EndsWithI(path, L"\\brave.exe") || EndsWithI(path, L"\\chrome.exe") ||
                        EndsWithI(path, L"\\msedge.exe") || EndsWithI(path, L"\\firefox.exe") ||
                        EndsWithI(path, L"\\opera.exe"))) {
                c[1] = {h, t, path};
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(found));

    Impl& im = *impl_;
    MediaState fresh;
    if (found[0].hwnd) {
        // Spotify gra → tytuł "Wykonawca - Utwór"; pauza → "Spotify Premium" / "Spotify Free" / "Spotify"
        const std::wstring& t = found[0].title;
        const size_t dash = t.find(L" - ");
        fresh.playing = dash != std::wstring::npos;
        if (fresh.playing) {
            im.lastArtist = t.substr(0, dash);
            im.lastTitle = t.substr(dash + 3);
        }
        fresh.title = im.lastTitle.empty() ? L"Spotify" : im.lastTitle;
        fresh.artist = im.lastTitle.empty() ? L"Wstrzymano" : im.lastArtist;
        fresh.sourcePath = found[0].path;
        im.source = found[0].hwnd;
        im.sourceIsSpotify = true;
    } else if (found[1].hwnd) {
        std::wstring t = found[1].title;
        // odcinamy dopisek przeglądarki: "… - Brave", "… – Google Chrome"
        for (const wchar_t* sep : {L" - ", L" \x2013 "})
            if (const size_t cut = t.rfind(sep); cut != std::wstring::npos && cut > 0) {
                t.resize(cut);
                break;
            }
        fresh.playing = true;  // przeglądarka nie zdradza stanu odtwarzania
        if (const size_t bullet = t.find(L" \x2022 "); bullet != std::wstring::npos) {  // Spotify w przeglądarce
            fresh.title = t.substr(0, bullet);
            fresh.artist = t.substr(bullet + 3);
        } else {
            if (const size_t yt = t.rfind(L" - YouTube"); yt != std::wstring::npos) t.resize(yt);
            fresh.title = t;
            fresh.artist = L"YouTube";
        }
        fresh.sourcePath = found[1].path;
        im.source = found[1].hwnd;
        im.sourceIsSpotify = false;
    } else {
        im.source = nullptr;
    }
    fresh.hasSession = im.source != nullptr;

    bool changed;
    {
        std::lock_guard lock(im.stateMutex);
        MediaState& s = im.state;
        changed = s.hasSession != fresh.hasSession || s.playing != fresh.playing || s.title != fresh.title ||
                  s.artist != fresh.artist || s.sourcePath != fresh.sourcePath;
        if (s.sourcePath != fresh.sourcePath) ++s.thumbnailVersion;
        s.hasSession = fresh.hasSession;
        s.playing = fresh.playing;
        s.title = fresh.title;
        s.artist = fresh.artist;
        s.sourcePath = fresh.sourcePath;
        s.thumbnail.clear();
        s.durationSec = 0.0;
    }
    if (changed) im.Notify();
}

// Polecenia wysyłamy bez czekania na wynik — wynik przyjdzie jako zdarzenie zmiany stanu.
void MediaService::PlayPause() {
    if (impl_->polling)
        return FallbackCommand(impl_->source, impl_->sourceIsSpotify, APPCOMMAND_MEDIA_PLAY_PAUSE, VK_MEDIA_PLAY_PAUSE);
    try {
        if (const auto s = impl_->Current()) s.TryTogglePlayPauseAsync();
    } catch (const hresult_error&) {
    }
}

void MediaService::Next() {
    if (impl_->polling)
        return FallbackCommand(impl_->source, impl_->sourceIsSpotify, APPCOMMAND_MEDIA_NEXTTRACK, VK_MEDIA_NEXT_TRACK);
    try {
        if (const auto s = impl_->Current()) s.TrySkipNextAsync();
    } catch (const hresult_error&) {
    }
}

void MediaService::Previous() {
    if (impl_->polling)
        return FallbackCommand(impl_->source, impl_->sourceIsSpotify, APPCOMMAND_MEDIA_PREVIOUSTRACK, VK_MEDIA_PREV_TRACK);
    try {
        if (const auto s = impl_->Current()) s.TrySkipPreviousAsync();
    } catch (const hresult_error&) {
    }
}
