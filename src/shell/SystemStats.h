#pragma once
#include <windows.h>
#include <string>
#include <utility>
#include <vector>

// Pomiary systemu do panelu statystyk (próbkowane co ~2 s przez gospodarza).
// Czas w aplikacjach: przy każdej próbce doliczamy upływ czasu do programu na pierwszym planie
// (pomijając bezczynność > 3 min). Zapis dzienny: %LOCALAPPDATA%\Exploder\usage\RRRR-MM-DD.txt.
class SystemStats {
public:
    SystemStats();
    ~SystemStats();
    void Sample();

    double cpu = 0.0;        // 0–1
    double ramUsed = 0.0;    // GB
    double ramTotal = 0.0;   // GB
    double disk = 0.0;       // aktywność dysków 0–1 (najbardziej zajęty dysk, jak w Menedżerze zadań)
    double diskRead = 0.0;   // B/s (suma dysków)
    double diskWrite = 0.0;
    double diskFreeGB = 0.0; // wolne na C:
    double diskTotalGB = 0.0;
    double netDown = 0.0;    // B/s
    double netUp = 0.0;
    ULONGLONG uptimeMs = 0;
    std::vector<std::pair<std::wstring, double>> topApps;  // nazwa, sekundy (malejąco, max 5)

    void SaveUsage();

private:
    void SampleUsage(double elapsedSec);
    void LoadUsage();
    std::wstring UsagePath() const;

    ULONGLONG lastIdle_ = 0, lastKernel_ = 0, lastUser_ = 0;
    ULONG64 lastIn_ = 0, lastOut_ = 0;
    ULONGLONG lastTick_ = 0, lastSave_ = 0;
    // Dyski fizyczne: liczniki ze sterownika (IOCTL_DISK_PERFORMANCE — nie wymaga administratora; liczniki
    // wydajności Windows / PDH bywają uszkodzone — u użytkownika "PhysicalDisk" nie istnieje)
    struct Disk {
        HANDLE handle = INVALID_HANDLE_VALUE;
        LONGLONG idle = 0, query = 0, read = 0, written = 0;
    };
    std::vector<Disk> disks_;
    std::vector<std::pair<std::wstring, double>> usage_;  // dzisiejsze czasy
    WORD day_ = 0;
};
