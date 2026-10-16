#include "NowPlayingHost.h"
#include <windows.h>
#include <objbase.h>
#include <servprov.h>
#include <cstring>

namespace {

constexpr CLSID kNowPlaying = {0xBCBB9860, 0xC012, 0x4AD7, {0xA9, 0x38, 0x6E, 0x33, 0x7A, 0xE6, 0xAB, 0xA5}};
constexpr CLSID kImmersiveShell = {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
constexpr IID kViewCollection = {0x1841C6D7, 0x4F9D, 0x42C0, {0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5}};

IClassFactory* g_inner = nullptr;  // fabryka z NPSM.dll
decltype(&CoRegisterClassObject) g_realRegister = nullptr;

HRESULT WINAPI CaptureRegister(REFCLSID clsid, LPUNKNOWN unk, DWORD ctx, DWORD flags, LPDWORD cookie) {
    if (clsid == kNowPlaying && !g_inner) unk->QueryInterface(IID_PPV_ARGS(&g_inner));
    return g_realRegister(clsid, unk, ctx, flags, cookie);
}

// Podmiana importu w module
void HookImport(HMODULE module, const char* name, void* replacement, void** original) {
    auto base = reinterpret_cast<BYTE*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto patch = [&](IMAGE_THUNK_DATA* iat, IMAGE_THUNK_DATA* names) {
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(byName->Name), name) != 0) continue;
            DWORD old;
            VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old);
            if (!*original) *original = reinterpret_cast<void*>(iat->u1.Function);
            iat->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old, &old);
        }
    };
    const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress)
        for (auto d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); d->Name; ++d)
            patch(reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk),
                  reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk));
}

// Nakładka na fabrykę NPSM — jej własnej rejestracji COM nie przyjmuje
struct Factory : IClassFactory {
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *out = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        return g_inner->QueryInterface(riid, out);  // programy zgłaszające muzykę pytają o prywatny interfejs NPSM
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        return g_inner->CreateInstance(outer, riid, out);
    }
    STDMETHODIMP LockServer(BOOL) override { return S_OK; }
} g_factory;

// Atrapa IApplicationViewCollection (Windows 10 19041+: 3 GetViews … 12 RegisterForApplicationViewChanges,
// 13 UnregisterForApplicationViewChanges). Argumenty w x64 sprząta wywołujący, więc wspólna sygnatura wystarcza.
template <int Slot>
HRESULT STDMETHODCALLTYPE ViewSlot(void*, void*, void* second, void*) {
    if constexpr (Slot == 12) {
        if (second) *static_cast<DWORD*>(second) = 1;  // ciasteczko rejestracji
        return S_OK;
    } else if constexpr (Slot == 13) {
        return S_OK;
    } else {
        return E_NOTIMPL;
    }
}
HRESULT STDMETHODCALLTYPE ViewQueryInterface(void* self, REFIID riid, void** out) {
    if (riid == IID_IUnknown || riid == kViewCollection) {
        *out = self;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE ViewRef(void*) { return 1; }
void* g_viewVtbl[] = {reinterpret_cast<void*>(&ViewQueryInterface), reinterpret_cast<void*>(&ViewRef),
                      reinterpret_cast<void*>(&ViewRef), reinterpret_cast<void*>(&ViewSlot<3>),
                      reinterpret_cast<void*>(&ViewSlot<4>), reinterpret_cast<void*>(&ViewSlot<5>),
                      reinterpret_cast<void*>(&ViewSlot<6>), reinterpret_cast<void*>(&ViewSlot<7>),
                      reinterpret_cast<void*>(&ViewSlot<8>), reinterpret_cast<void*>(&ViewSlot<9>),
                      reinterpret_cast<void*>(&ViewSlot<10>), reinterpret_cast<void*>(&ViewSlot<11>),
                      reinterpret_cast<void*>(&ViewSlot<12>), reinterpret_cast<void*>(&ViewSlot<13>),
                      reinterpret_cast<void*>(&ViewSlot<14>), reinterpret_cast<void*>(&ViewSlot<15>),
                      reinterpret_cast<void*>(&ViewSlot<16>), reinterpret_cast<void*>(&ViewSlot<17>)};
struct {
    void** vtbl;
} g_viewCollection = {g_viewVtbl};

struct ImmersiveShell : IServiceProvider {
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IServiceProvider) {
            *out = static_cast<IServiceProvider*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP QueryService(REFGUID service, REFIID riid, void** out) override {
        if (service == kViewCollection && riid == kViewCollection) {
            *out = &g_viewCollection;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
} g_shell;

struct ShellFactory : IClassFactory {
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *out = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        return g_shell.QueryInterface(riid, out);
    }
    STDMETHODIMP LockServer(BOOL) override { return S_OK; }
} g_shellFactory;

HANDLE g_thread = nullptr;
HANDLE g_stop = nullptr;

HANDLE g_ready = nullptr;  // statyczne: wątek może skończyć start po czasie oczekiwania Start()
volatile bool g_ok = false;

// Wątek MTA: rejestracje obsługuje pula wątków RPC, wątek tylko czeka na koniec
DWORD WINAPI HostThread(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD shellCookie = 0, factoryCookie = 0;
    void (*unregister)() = nullptr;
    bool ok = false;
    if (HMODULE npsm = LoadLibraryW(L"NPSM.dll")) {
        auto registerFactory =
            reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(npsm, "RegisterNowPlayingSessionManagerFactory"));
        unregister = reinterpret_cast<void (*)()>(GetProcAddress(npsm, "UnregisterNowPlayingSessionManagerFactory"));
        HookImport(npsm, "CoRegisterClassObject", reinterpret_cast<void*>(&CaptureRegister),
                   reinterpret_cast<void**>(&g_realRegister));
        if (registerFactory && g_realRegister && SUCCEEDED(registerFactory()) && g_inner &&
            SUCCEEDED(CoRegisterClassObject(kImmersiveShell, &g_shellFactory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE,
                                            &shellCookie)) &&
            SUCCEEDED(CoRegisterClassObject(kNowPlaying, &g_factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE,
                                            &factoryCookie)))
            ok = true;
    }
    g_ok = ok;
    SetEvent(g_ready);
    if (ok) WaitForSingleObject(g_stop, INFINITE);
    if (factoryCookie) CoRevokeClassObject(factoryCookie);
    if (shellCookie) CoRevokeClassObject(shellCookie);
    if (unregister && g_inner) unregister();
    if (g_inner) g_inner->Release(), g_inner = nullptr;
    CoUninitialize();
    return 0;
}

}  // namespace

namespace NowPlayingHost {

bool Start() {
    if (g_thread) return true;
    if (!g_ready) g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ResetEvent(g_ready);
    g_ok = false;
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, HostThread, nullptr, 0, nullptr);
    if (g_thread) WaitForSingleObject(g_ready, 5000);
    if (!g_ok) Stop();
    return g_ok;
}

void Stop() {
    if (g_thread) {
        SetEvent(g_stop);
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_stop) CloseHandle(g_stop), g_stop = nullptr;
}

}  // namespace NowPlayingHost
