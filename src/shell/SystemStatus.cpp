#include "SystemStatus.h"
#include <winsock2.h>
#include <iphlpapi.h>
#include <wlanapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;

NetworkStatus::~NetworkStatus() {
    if (wlan_) WlanCloseHandle(wlan_, nullptr);
}

NetworkInfo NetworkStatus::Query() {
    NetworkInfo info;

    // Aktywna karta = włączona i z bramą domyślną.
    ULONG size = 16 * 1024;
    std::vector<BYTE> buffer(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, list, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, list, &size);
    }
    if (rc == NO_ERROR) {
        for (auto* a = list; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || !a->FirstGatewayAddress) continue;
            if (a->IfType == IF_TYPE_IEEE80211) info.kind = NetworkInfo::Kind::Wifi;
            else if (a->IfType == IF_TYPE_ETHERNET_CSMACD) info.kind = NetworkInfo::Kind::Ethernet;
            else continue;
            break;
        }
    }

    if (!nlm_) CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&nlm_));
    NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED;
    if (nlm_ && SUCCEEDED(nlm_->GetConnectivity(&connectivity)))
        info.internet = (connectivity & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) != 0;

    if (info.kind == NetworkInfo::Kind::Ethernet) info.name = L"Ethernet";
    if (info.kind == NetworkInfo::Kind::Wifi) {
        DWORD version = 0;
        if (!wlan_) WlanOpenHandle(2, nullptr, &version, &wlan_);
        WLAN_INTERFACE_INFO_LIST* interfaces = nullptr;
        if (wlan_ && WlanEnumInterfaces(wlan_, nullptr, &interfaces) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
                const auto& itf = interfaces->InterfaceInfo[i];
                if (itf.isState != wlan_interface_state_connected) continue;
                DWORD dataSize = 0;
                WLAN_CONNECTION_ATTRIBUTES* conn = nullptr;
                if (WlanQueryInterface(wlan_, &itf.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr, &dataSize,
                                       reinterpret_cast<void**>(&conn), nullptr) == ERROR_SUCCESS) {
                    info.name = conn->strProfileName;
                    info.signal = static_cast<int>(conn->wlanAssociationAttributes.wlanSignalQuality);
                    WlanFreeMemory(conn);
                }
                break;
            }
            WlanFreeMemory(interfaces);
        }
        if (info.name.empty()) info.name = L"Wi-Fi";
    }
    return info;
}

NetworkDetails NetworkStatus::Details() {
    NetworkDetails d;
    ULONG size = 16 * 1024;
    std::vector<BYTE> buffer(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size);
    }
    if (rc != NO_ERROR) return d;
    for (auto* a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || !a->FirstGatewayAddress || !a->FirstUnicastAddress) continue;
        d.adapter = a->Description;
        d.linkSpeed = a->ReceiveLinkSpeed;
        wchar_t ip[64]{};
        DWORD len = ARRAYSIZE(ip);
        const auto& addr = a->FirstUnicastAddress->Address;
        if (WSAAddressToStringW(addr.lpSockaddr, addr.iSockaddrLength, nullptr, ip, &len) == 0) d.ipv4 = ip;
        break;
    }
    return d;
}

bool NetworkStatus::WlanInterface(GUID& out) {
    DWORD version = 0;
    if (!wlan_ && WlanOpenHandle(2, nullptr, &version, &wlan_) != ERROR_SUCCESS) return false;
    WLAN_INTERFACE_INFO_LIST* list = nullptr;
    if (WlanEnumInterfaces(wlan_, nullptr, &list) != ERROR_SUCCESS) return false;
    const bool any = list->dwNumberOfItems > 0;
    if (any) out = list->InterfaceInfo[0].InterfaceGuid;
    WlanFreeMemory(list);
    return any;
}

bool NetworkStatus::HasWifi() {
    GUID g{};
    return WlanInterface(g);
}

void NetworkStatus::ScanWifi() {
    GUID g{};
    if (WlanInterface(g)) WlanScan(wlan_, &g, nullptr, nullptr, nullptr);
}

std::vector<WifiNetwork> NetworkStatus::WifiNetworks() {
    std::vector<WifiNetwork> out;
    GUID g{};
    WLAN_AVAILABLE_NETWORK_LIST* list = nullptr;
    if (!WlanInterface(g) || WlanGetAvailableNetworkList(wlan_, &g, 0, nullptr, &list) != ERROR_SUCCESS) return out;
    for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
        const auto& n = list->Network[i];
        if (!n.dot11Ssid.uSSIDLength) continue;  // ukryta sieć
        WifiNetwork w;
        wchar_t ssid[64]{};
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(n.dot11Ssid.ucSSID), static_cast<int>(n.dot11Ssid.uSSIDLength),
                            ssid, ARRAYSIZE(ssid) - 1);
        w.ssid = ssid;
        if (n.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) w.profile = n.strProfileName;
        w.signal = static_cast<int>(n.wlanSignalQuality);
        w.secured = n.bSecurityEnabled != FALSE;
        w.connected = (n.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
        w.auth = n.dot11DefaultAuthAlgorithm;
        w.cipher = n.dot11DefaultCipherAlgorithm;
        // Ta sama sieć bywa na liście dwa razy (z profilem i bez) — zostawiamy lepszy wpis
        auto same = std::find_if(out.begin(), out.end(), [&](const WifiNetwork& o) { return o.ssid == w.ssid; });
        if (same == out.end()) out.push_back(w);
        else if (w.connected || (!same->connected && !w.profile.empty())) *same = w;
    }
    WlanFreeMemory(list);
    std::sort(out.begin(), out.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        return a.connected != b.connected ? a.connected : a.signal > b.signal;
    });
    return out;
}

bool NetworkStatus::ConnectWifi(const WifiNetwork& net, const std::wstring& password) {
    GUID g{};
    if (!WlanInterface(g)) return false;
    std::wstring profile = net.profile;
    if (profile.empty()) {
        // Nowy profil: typ zabezpieczeń z ogłoszenia sieci
        const wchar_t* auth = L"open";
        const wchar_t* cipher = L"none";
        if (net.secured) {
            switch (net.auth) {
            case DOT11_AUTH_ALGO_WPA_PSK: auth = L"WPAPSK"; break;
            case DOT11_AUTH_ALGO_WPA3_SAE: auth = L"WPA3SAE"; break;
            default: auth = L"WPA2PSK"; break;
            }
            cipher = net.cipher == DOT11_CIPHER_ALGO_TKIP ? L"TKIP" : L"AES";
        }
        std::wstring xml = L"<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
                           L"<name>" + net.ssid + L"</name><SSIDConfig><SSID><name>" + net.ssid +
                           L"</name></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>auto</connectionMode>"
                           L"<MSM><security><authEncryption><authentication>" + auth + L"</authentication><encryption>" + cipher +
                           L"</encryption><useOneX>false</useOneX></authEncryption>";
        if (net.secured)
            xml += L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>" + password +
                   L"</keyMaterial></sharedKey>";
        xml += L"</security></MSM></WLANProfile>";
        DWORD reason = 0;
        if (WlanSetProfile(wlan_, &g, 0, xml.c_str(), nullptr, TRUE, nullptr, &reason) != ERROR_SUCCESS) return false;
        profile = net.ssid;
    }
    WLAN_CONNECTION_PARAMETERS params{};
    params.wlanConnectionMode = wlan_connection_mode_profile;
    params.strProfile = profile.c_str();
    params.dot11BssType = dot11_BSS_type_infrastructure;
    return WlanConnect(wlan_, &g, &params, nullptr) == ERROR_SUCCESS;
}

void NetworkStatus::DisconnectWifi() {
    GUID g{};
    if (WlanInterface(g)) WlanDisconnect(wlan_, &g, nullptr);
}

namespace {
class VolumeCallback : public IAudioEndpointVolumeCallback {
public:
    VolumeCallback(HWND hwnd, UINT message) : hwnd_(hwnd), message_(message) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = InterlockedDecrement(&refs_);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
            *out = static_cast<IAudioEndpointVolumeCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
        PostMessageW(hwnd_, message_, 0, 0);
        return S_OK;
    }

private:
    LONG refs_ = 1;
    HWND hwnd_;
    UINT message_;
};
}  // namespace

VolumeControl::~VolumeControl() { Release(); }

void VolumeControl::Release() {
    if (endpoint_ && callback_) endpoint_->UnregisterControlChangeNotify(callback_.Get());
    endpoint_.Reset();
    callback_.Reset();
    deviceId_.clear();
    deviceName_.clear();
}

void VolumeControl::Start(HWND notify, UINT message) {
    hwnd_ = notify;
    message_ = message;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_));
    Refresh();
}

void VolumeControl::Refresh() {
    if (!enumerator_) return;
    ComPtr<IMMDevice> device;
    PWSTR id = nullptr;
    if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) || FAILED(device->GetId(&id))) {
        Release();
        return;
    }
    const bool same = deviceId_ == id;
    std::wstring newId = id;
    CoTaskMemFree(id);
    if (same && endpoint_) return;

    Release();
    if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpoint_))) return;
    deviceId_ = std::move(newId);
    ComPtr<IPropertyStore> props;
    PROPVARIANT name;
    PropVariantInit(&name);
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) &&
        name.vt == VT_LPWSTR)
        deviceName_ = name.pwszVal;
    PropVariantClear(&name);
    callback_.Attach(new VolumeCallback(hwnd_, message_));
    endpoint_->RegisterControlChangeNotify(callback_.Get());
    PostMessageW(hwnd_, message_, 0, 0);
}

float VolumeControl::Level() const {
    float level = 0.f;
    if (endpoint_) endpoint_->GetMasterVolumeLevelScalar(&level);
    return level;
}

bool VolumeControl::Muted() const {
    BOOL muted = FALSE;
    if (endpoint_) endpoint_->GetMute(&muted);
    return muted != FALSE;
}

void VolumeControl::SetLevel(float level) {
    if (endpoint_) endpoint_->SetMasterVolumeLevelScalar(std::clamp(level, 0.f, 1.f), nullptr);
}

void VolumeControl::ToggleMute() {
    if (endpoint_) endpoint_->SetMute(!Muted(), nullptr);
}
