#pragma once
#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <netlistmgr.h>
#include <wlanapi.h>
#include <wrl/client.h>
#include <string>
#include <vector>

struct NetworkInfo {
    enum class Kind { None, Ethernet, Wifi };
    Kind kind = Kind::None;
    bool internet = false;
    int signal = 0;     // Wi-Fi 0–100
    std::wstring name;  // nazwa sieci Wi-Fi albo "Ethernet"
    bool operator==(const NetworkInfo&) const = default;
};

struct NetworkDetails {
    std::wstring adapter;  // opis karty sieciowej
    std::wstring ipv4;
    ULONG64 linkSpeed = 0;  // b/s
};

struct WifiNetwork {
    std::wstring ssid;
    std::wstring profile;  // zapisany profil (puste = brak — trzeba podać hasło)
    int signal = 0;        // 0–100
    bool secured = false;
    bool connected = false;
    DOT11_AUTH_ALGORITHM auth = DOT11_AUTH_ALGO_80211_OPEN;
    DOT11_CIPHER_ALGORITHM cipher = DOT11_CIPHER_ALGO_NONE;
};

// Stan sieci (sprawdzany co kilka sekund — tanie wywołania, bez wątku w tle).
class NetworkStatus {
public:
    ~NetworkStatus();
    NetworkInfo Query();
    NetworkDetails Details();

    // Wi-Fi (gdy jest karta bezprzewodowa)
    bool HasWifi();
    void ScanWifi();
    std::vector<WifiNetwork> WifiNetworks();
    bool ConnectWifi(const WifiNetwork& net, const std::wstring& password);  // password: gdy brak profilu
    void DisconnectWifi();

private:
    bool WlanInterface(GUID& out);

    Microsoft::WRL::ComPtr<INetworkListManager> nlm_;
    HANDLE wlan_ = nullptr;
};

// Głośność domyślnego urządzenia odtwarzania. Zmiany (także z klawiatury/innych programów) → `message`.
class VolumeControl {
public:
    ~VolumeControl();
    void Start(HWND notify, UINT message);
    void Refresh();  // domyślne urządzenie mogło się zmienić (np. słuchawki)
    bool Available() const { return endpoint_ != nullptr; }
    const std::wstring& DeviceName() const { return deviceName_; }
    float Level() const;  // 0–1
    bool Muted() const;
    void SetLevel(float level);
    void ToggleMute();

private:
    void Release();
    HWND hwnd_ = nullptr;
    UINT message_ = 0;
    std::wstring deviceId_;
    std::wstring deviceName_;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> endpoint_;
    Microsoft::WRL::ComPtr<IAudioEndpointVolumeCallback> callback_;
};
