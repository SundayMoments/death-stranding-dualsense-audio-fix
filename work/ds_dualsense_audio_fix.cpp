#define WIN32_LEAN_AND_MEAN
#include <initguid.h>
#include <windows.h>
#include <tlhelp32.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>

#ifndef __IAudioMeterInformation_INTERFACE_DEFINED__
#define __IAudioMeterInformation_INTERFACE_DEFINED__
DEFINE_GUID(IID_IAudioMeterInformation, 0xC02216F6, 0x8C67, 0x4B5B, 0x9D, 0x00, 0xD0, 0x08,
            0xE7, 0x3E, 0x00, 0x64);

MIDL_INTERFACE("C02216F6-8C67-4B5B-9D00-D008E73E0064")
IAudioMeterInformation : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* pfPeak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT* pnChannelCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 u32ChannelCount,
                                                            float* afPeakValues) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* pdwHardwareSupportMask) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IAudioMeterInformation, 0xC02216F6, 0x8C67, 0x4B5B, 0x9D, 0x00, 0xD0, 0x08,
                0xE7, 0x3E, 0x00, 0x64)
#endif
#endif

namespace {

constexpr wchar_t kProcessName[] = L"ds.exe";

constexpr uintptr_t kRvaDesiredDeviceId = 0x4A67364;
constexpr uintptr_t kRvaActiveDeviceId = 0x4A6C3A8;

const char* DeviceStateName(DWORD state) {
    switch (state) {
    case DEVICE_STATE_ACTIVE:
        return "active";
    case DEVICE_STATE_DISABLED:
        return "disabled";
    case DEVICE_STATE_NOTPRESENT:
        return "not-present";
    case DEVICE_STATE_UNPLUGGED:
        return "unplugged";
    default:
        return "unknown";
    }
}

const wchar_t* RoleName(ERole role) {
    switch (role) {
    case eConsole:
        return L"console";
    case eMultimedia:
        return L"multimedia";
    case eCommunications:
        return L"communications";
    default:
        return L"unknown";
    }
}

const char* SessionStateName(AudioSessionState state) {
    switch (state) {
    case AudioSessionStateInactive:
        return "inactive";
    case AudioSessionStateActive:
        return "active";
    case AudioSessionStateExpired:
        return "expired";
    default:
        return "unknown";
    }
}

const char* HrName(HRESULT hr) {
    switch (hr) {
    case S_OK:
        return "S_OK";
    case AUDCLNT_E_DEVICE_IN_USE:
        return "AUDCLNT_E_DEVICE_IN_USE";
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return "AUDCLNT_E_UNSUPPORTED_FORMAT";
    case AUDCLNT_E_ENDPOINT_CREATE_FAILED:
        return "AUDCLNT_E_ENDPOINT_CREATE_FAILED";
    case AUDCLNT_E_SERVICE_NOT_RUNNING:
        return "AUDCLNT_E_SERVICE_NOT_RUNNING";
    case E_ACCESSDENIED:
        return "E_ACCESSDENIED";
    default:
        return "HRESULT";
    }
}

std::wstring GetEndpointName(IMMDevice* device) {
    IPropertyStore* store = nullptr;
    PROPVARIANT name;
    PropVariantInit(&name);

    std::wstring friendly = L"(unnamed)";
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name)) &&
            name.vt == VT_LPWSTR && name.pwszVal) {
            friendly = name.pwszVal;
        }
    }

    PropVariantClear(&name);
    if (store) {
        store->Release();
    }
    return friendly;
}

bool ContainsNoCase(const std::wstring& haystack, const wchar_t* needle) {
    const std::wstring needleString = needle;
    if (needleString.empty() || haystack.size() < needleString.size()) {
        return false;
    }

    for (size_t i = 0; i + needleString.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needleString.size(); ++j) {
            if (std::towlower(haystack[i + j]) != std::towlower(needleString[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

void PrintDefaultEndpoint(IMMDeviceEnumerator* enumerator, ERole role) {
    IMMDevice* device = nullptr;
    HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, role, &device);
    if (FAILED(hr)) {
        std::printf("default render endpoint (%ls): unavailable, hr=0x%08lX\n", RoleName(role),
                    static_cast<unsigned long>(hr));
        return;
    }

    LPWSTR id = nullptr;
    hr = device->GetId(&id);
    if (SUCCEEDED(hr) && id) {
        std::printf("default render endpoint (%ls): %ls\n", RoleName(role), id);
        CoTaskMemFree(id);
    }
    device->Release();
}

void PrintAudioEndpoints() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitCom = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::printf("CoInitializeEx failed, hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        std::printf("CoCreateInstance(MMDeviceEnumerator) failed, hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    PrintDefaultEndpoint(enumerator, eConsole);
    PrintDefaultEndpoint(enumerator, eMultimedia);
    PrintDefaultEndpoint(enumerator, eCommunications);

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(
        eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_NOTPRESENT |
                     DEVICE_STATE_UNPLUGGED,
        &collection);
    if (FAILED(hr)) {
        std::printf("EnumAudioEndpoints failed, hr=0x%08lX\n", static_cast<unsigned long>(hr));
        enumerator->Release();
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    UINT count = 0;
    collection->GetCount(&count);
    std::printf("render endpoints: %u\n", count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device) {
            continue;
        }

        DWORD state = 0;
        device->GetState(&state);

        LPWSTR id = nullptr;
        device->GetId(&id);

        IPropertyStore* store = nullptr;
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
            store->GetValue(PKEY_Device_FriendlyName, &name);
        }

        const wchar_t* friendly =
            (name.vt == VT_LPWSTR && name.pwszVal) ? name.pwszVal : L"(unnamed)";
        std::printf("[%u] %s | %ls\n", i, DeviceStateName(state), friendly);
        if (id) {
            std::printf("    %ls\n", id);
            CoTaskMemFree(id);
        }

        PropVariantClear(&name);
        if (store) {
            store->Release();
        }
        device->Release();
    }

    collection->Release();
    enumerator->Release();
    if (uninitCom) {
        CoUninitialize();
    }
}

void PrintAudioSessionsForProcess(DWORD targetPid) {
    if (!targetPid) {
        std::printf("ds.exe is not running; no process-specific audio sessions to match\n");
        return;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitCom = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::printf("CoInitializeEx failed, hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        std::printf("CoCreateInstance(MMDeviceEnumerator) failed, hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        std::printf("EnumAudioEndpoints(active render) failed, hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        enumerator->Release();
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    UINT endpointCount = 0;
    collection->GetCount(&endpointCount);

    bool found = false;
    std::printf("ds.exe audio sessions on active render endpoints:\n");
    for (UINT i = 0; i < endpointCount; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device) {
            continue;
        }

        IAudioSessionManager2* manager = nullptr;
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&manager));
        if (FAILED(hr) || !manager) {
            device->Release();
            continue;
        }

        IAudioSessionEnumerator* sessions = nullptr;
        hr = manager->GetSessionEnumerator(&sessions);
        if (FAILED(hr) || !sessions) {
            manager->Release();
            device->Release();
            continue;
        }

        int sessionCount = 0;
        sessions->GetCount(&sessionCount);
        bool printedEndpoint = false;
        for (int j = 0; j < sessionCount; ++j) {
            IAudioSessionControl* control = nullptr;
            if (FAILED(sessions->GetSession(j, &control)) || !control) {
                continue;
            }

            IAudioSessionControl2* control2 = nullptr;
            if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                                  reinterpret_cast<void**>(&control2))) &&
                control2) {
                DWORD pid = 0;
                control2->GetProcessId(&pid);
                if (pid == targetPid) {
                    if (!printedEndpoint) {
                        std::wstring friendly = GetEndpointName(device);
                        LPWSTR endpointId = nullptr;
                        device->GetId(&endpointId);
                        std::printf("  endpoint: %ls\n", friendly.c_str());
                        if (endpointId) {
                            std::printf("    %ls\n", endpointId);
                            CoTaskMemFree(endpointId);
                        }
                        printedEndpoint = true;
                    }

                    AudioSessionState state = AudioSessionStateInactive;
                    control->GetState(&state);
                    LPWSTR displayName = nullptr;
                    control->GetDisplayName(&displayName);
                    const HRESULT systemSession = control2->IsSystemSoundsSession();

                    float volume = -1.0f;
                    BOOL muted = FALSE;
                    ISimpleAudioVolume* simpleVolume = nullptr;
                    if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                          reinterpret_cast<void**>(&simpleVolume))) &&
                        simpleVolume) {
                        simpleVolume->GetMasterVolume(&volume);
                        simpleVolume->GetMute(&muted);
                        simpleVolume->Release();
                    }

                    std::printf("    pid=%lu state=%s system=%s volume=%.3f muted=%s display=%ls\n",
                                static_cast<unsigned long>(pid), SessionStateName(state),
                                systemSession == S_OK ? "yes" : "no", volume,
                                muted ? "yes" : "no",
                                displayName ? displayName : L"(none)");
                    if (displayName) {
                        CoTaskMemFree(displayName);
                    }
                    found = true;
                }
                control2->Release();
            }

            control->Release();
        }

        sessions->Release();
        manager->Release();
        device->Release();
    }

    if (!found) {
        std::printf("  no ds.exe sessions found on active render endpoints\n");
    }

    collection->Release();
    enumerator->Release();
    if (uninitCom) {
        CoUninitialize();
    }
}

struct MeterTarget {
    std::wstring name;
    std::wstring id;
    DWORD hardwareSupport;
    IAudioMeterInformation* meter;
};

bool DeviceHasSessionForProcess(IMMDevice* device, DWORD targetPid) {
    IAudioSessionManager2* manager = nullptr;
    HRESULT hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&manager));
    if (FAILED(hr) || !manager) {
        return false;
    }

    IAudioSessionEnumerator* sessions = nullptr;
    hr = manager->GetSessionEnumerator(&sessions);
    if (FAILED(hr) || !sessions) {
        manager->Release();
        return false;
    }

    int sessionCount = 0;
    sessions->GetCount(&sessionCount);
    bool found = false;
    for (int i = 0; i < sessionCount; ++i) {
        IAudioSessionControl* control = nullptr;
        if (FAILED(sessions->GetSession(i, &control)) || !control) {
            continue;
        }

        IAudioSessionControl2* control2 = nullptr;
        if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                              reinterpret_cast<void**>(&control2))) &&
            control2) {
            DWORD pid = 0;
            control2->GetProcessId(&pid);
            if (pid == targetPid) {
                found = true;
            }
            control2->Release();
        }

        control->Release();
        if (found) {
            break;
        }
    }

    sessions->Release();
    manager->Release();
    return found;
}

void MonitorEndpointPeaksForProcess(DWORD targetPid, int seconds) {
    if (!targetPid) {
        std::printf("ds.exe is not running; no endpoint meters to monitor\n");
        return;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitCom = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::printf("CoInitializeEx failed, hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        std::printf("CoCreateInstance(MMDeviceEnumerator) failed, hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        std::printf("EnumAudioEndpoints(active render) failed, hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        enumerator->Release();
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    std::vector<MeterTarget> targets;
    UINT endpointCount = 0;
    collection->GetCount(&endpointCount);
    for (UINT i = 0; i < endpointCount; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device) {
            continue;
        }

        if (DeviceHasSessionForProcess(device, targetPid)) {
            IAudioMeterInformation* meter = nullptr;
            hr = device->Activate(IID_IAudioMeterInformation, CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&meter));
            if (SUCCEEDED(hr) && meter) {
                DWORD hardwareSupport = 0;
                meter->QueryHardwareSupport(&hardwareSupport);
                LPWSTR endpointId = nullptr;
                std::wstring id;
                if (SUCCEEDED(device->GetId(&endpointId)) && endpointId) {
                    id = endpointId;
                    CoTaskMemFree(endpointId);
                }
                targets.push_back({GetEndpointName(device), id, hardwareSupport, meter});
            }
        }

        device->Release();
    }

    collection->Release();
    enumerator->Release();

    if (targets.empty()) {
        std::printf("no ds.exe endpoint meters found to monitor\n");
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    if (seconds < 1) {
        seconds = 1;
    } else if (seconds > 120) {
        seconds = 120;
    }

    std::printf("monitoring endpoint peak meters for ds.exe endpoints for %d seconds\n",
                seconds);
    for (size_t i = 0; i < targets.size(); ++i) {
        std::printf("  [%zu] %ls\n", i, targets[i].name.c_str());
        if (!targets[i].id.empty()) {
            std::printf("      %ls\n", targets[i].id.c_str());
        }
        std::printf("      hardware meter support: %s\n",
                    (targets[i].hardwareSupport & ENDPOINT_HARDWARE_SUPPORT_METER) ? "yes"
                                                                                    : "no");
    }

    constexpr DWORD kSampleMs = 250;
    const int sampleCount = (seconds * 1000) / static_cast<int>(kSampleMs);
    for (int sample = 0; sample <= sampleCount; ++sample) {
        std::printf("t=%5.2fs", (sample * static_cast<double>(kSampleMs)) / 1000.0);
        for (const MeterTarget& target : targets) {
            float peak = 0.0f;
            target.meter->GetPeakValue(&peak);
            std::printf(" | %ls=%.4f", target.name.c_str(), peak);
        }
        std::printf("\n");
        if (sample != sampleCount) {
            Sleep(kSampleMs);
        }
    }

    for (MeterTarget& target : targets) {
        target.meter->Release();
    }

    if (uninitCom) {
        CoUninitialize();
    }
}

void PrintSharedAccessTestForDevice(IMMDevice* device, const std::wstring& friendly) {
    IAudioClient* client = nullptr;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&client));
    if (FAILED(hr) || !client) {
        std::printf("    IAudioClient activate: %s (0x%08lX)\n", HrName(hr),
                    static_cast<unsigned long>(hr));
        return;
    }

    WAVEFORMATEX* mix = nullptr;
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
        std::printf("    GetMixFormat: %s (0x%08lX)\n", HrName(hr),
                    static_cast<unsigned long>(hr));
        client->Release();
        return;
    }

    std::printf("    mix format: %lu Hz, %u channels, %u bits, tag=0x%04X\n",
                static_cast<unsigned long>(mix->nSamplesPerSec), mix->nChannels,
                mix->wBitsPerSample, mix->wFormatTag);

    constexpr REFERENCE_TIME kOneSecond = 10000000;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kOneSecond, 0, mix, nullptr);
    std::printf("    shared Initialize: %s (0x%08lX)", HrName(hr),
                static_cast<unsigned long>(hr));
    if (hr == AUDCLNT_E_DEVICE_IN_USE) {
        std::printf("  <-- blocked; endpoint is probably held exclusive\n");
    } else {
        std::printf("\n");
    }

    if (SUCCEEDED(hr)) {
        client->Stop();
    }

    CoTaskMemFree(mix);
    client->Release();
    (void)friendly;
}

void PrintEndpointAccessTests(DWORD targetPid) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitCom = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::printf("CoInitializeEx failed, hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        std::printf("CoCreateInstance(MMDeviceEnumerator) failed, hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        std::printf("EnumAudioEndpoints(active render) failed, hr=0x%08lX\n",
                    static_cast<unsigned long>(hr));
        enumerator->Release();
        if (uninitCom) {
            CoUninitialize();
        }
        return;
    }

    UINT endpointCount = 0;
    collection->GetCount(&endpointCount);
    std::printf("shared-mode access test for controller/ds.exe endpoints:\n");

    bool printed = false;
    for (UINT i = 0; i < endpointCount; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device) {
            continue;
        }

        const std::wstring friendly = GetEndpointName(device);
        const bool looksLikeController =
            ContainsNoCase(friendly, L"DualSense") || ContainsNoCase(friendly, L"Wireless Controller");
        const bool ownedByGame = targetPid && DeviceHasSessionForProcess(device, targetPid);
        if (looksLikeController || ownedByGame) {
            LPWSTR endpointId = nullptr;
            device->GetId(&endpointId);
            std::printf("  endpoint: %ls%s%s\n", friendly.c_str(),
                        looksLikeController ? " [controller-like]" : "",
                        ownedByGame ? " [ds.exe session]" : "");
            if (endpointId) {
                std::printf("    %ls\n", endpointId);
                CoTaskMemFree(endpointId);
            }
            PrintSharedAccessTestForDevice(device, friendly);
            printed = true;
        }

        device->Release();
    }

    if (!printed) {
        std::printf("  no active controller-like or ds.exe-owned endpoints found\n");
    }

    collection->Release();
    enumerator->Release();
    if (uninitCom) {
        CoUninitialize();
    }
}

bool ReadMem(HANDLE process, uintptr_t address, void* out, size_t size) {
    SIZE_T read = 0;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), out, size, &read) &&
           read == size;
}

DWORD FindProcessId() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, kProcessName) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t FindModuleBase(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    uintptr_t base = 0;
    if (Module32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, kProcessName) == 0) {
                base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return base;
}

void PrintState(HANDLE process, uintptr_t base) {
    int32_t desired = 0;
    int32_t active = 0;
    if (ReadMem(process, base + kRvaDesiredDeviceId, &desired, sizeof(desired)) &&
        ReadMem(process, base + kRvaActiveDeviceId, &active, sizeof(active))) {
        std::printf("controller-speaker desired id: %d (0x%08X)\n", desired,
                    static_cast<uint32_t>(desired));
        std::printf("controller-speaker active id:  %d (0x%08X)\n", active,
                    static_cast<uint32_t>(active));
    }
}

} // namespace

int main(int argc, char** argv) {
    bool showEndpoints = false;
    bool showSessions = false;
    bool showMemoryState = false;
    bool monitorPeaks = false;
    bool accessTest = false;
    int monitorSeconds = 15;
    bool showHelp = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--diagnose") == 0) {
            showMemoryState = true;
            showSessions = true;
        } else if (std::strcmp(argv[i], "--endpoints") == 0) {
            showEndpoints = true;
        } else if (std::strcmp(argv[i], "--sessions") == 0) {
            showSessions = true;
        } else if (std::strcmp(argv[i], "--monitor") == 0) {
            monitorPeaks = true;
            showSessions = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                monitorSeconds = std::atoi(argv[i + 1]);
                ++i;
            }
        } else if (std::strcmp(argv[i], "--access-test") == 0) {
            accessTest = true;
            showSessions = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            showHelp = true;
        }
    }

    const bool noModeRequested =
        !showEndpoints && !showSessions && !showMemoryState && !monitorPeaks && !accessTest &&
        !showHelp;
    if (noModeRequested) {
        showEndpoints = true;
        showSessions = true;
        showMemoryState = true;
    }

    std::printf("Death Stranding DualSense controller-audio safe probe\n");

    if (showHelp) {
        std::printf("usage:\n");
        std::printf("  ds_dualsense_audio_probe.exe               read-only endpoint/session/state scan\n");
        std::printf("  ds_dualsense_audio_probe.exe --endpoints   list Windows render endpoints\n");
        std::printf("  ds_dualsense_audio_probe.exe --sessions    show ds.exe audio sessions by endpoint\n");
        std::printf("  ds_dualsense_audio_probe.exe --diagnose    show sessions and controller-speaker ids\n");
        std::printf("  ds_dualsense_audio_probe.exe --monitor [seconds]  sample endpoint peak meters\n");
        std::printf("  ds_dualsense_audio_probe.exe --access-test  test shared-mode access to endpoints\n");
        return 0;
    }

    std::printf("read-only mode: no process memory will be patched\n");

    if (showEndpoints) {
        PrintAudioEndpoints();
    }

    const DWORD pid = FindProcessId();
    if (showSessions) {
        PrintAudioSessionsForProcess(pid);
    }

    if (monitorPeaks) {
        MonitorEndpointPeaksForProcess(pid, monitorSeconds);
    }

    if (accessTest) {
        PrintEndpointAccessTests(pid);
    }

    if (!pid) {
        std::printf("ds.exe is not running. Start the game first, then run this tool.\n");
        return 0;
    }

    if (!showMemoryState) {
        return 0;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        std::printf("failed to open ds.exe process; try running this tool as administrator\n");
        return 3;
    }

    const uintptr_t base = FindModuleBase(pid);
    if (!base) {
        std::printf("failed to locate ds.exe module base\n");
        CloseHandle(process);
        return 4;
    }

    std::printf("ds.exe pid: %lu\n", static_cast<unsigned long>(pid));
    std::printf("ds.exe base: 0x%p\n", reinterpret_cast<void*>(base));
    PrintState(process, base);
    CloseHandle(process);
    return 0;
}
