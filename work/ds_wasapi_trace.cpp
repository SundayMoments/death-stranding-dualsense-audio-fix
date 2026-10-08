#define WIN32_LEAN_AND_MEAN
#include <initguid.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <limits>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "MinHook.h"

namespace {

HMODULE gSelf = nullptr;
wchar_t gLogPath[MAX_PATH]{};
CRITICAL_SECTION gLock;
thread_local bool gInternal = false;
bool gForceDualSenseShared = false;
bool gFixStaleBbDeviceApply = false;
bool gLoggingEnabled = true;

constexpr uintptr_t kRvaDesiredDeviceId = 0x4A67364;
constexpr uintptr_t kRvaActiveDeviceId = 0x4A6C3A8;
constexpr uintptr_t kRvaSetDeviceId = 0x1DC6F20;
constexpr uintptr_t kRvaReconcileDeviceId = 0x1DD4960;
constexpr uintptr_t kRvaReconcileReplaceBranchPatch = 0x1DD4A00;
constexpr int32_t kUnknownDeviceId = std::numeric_limits<int32_t>::min();

int32_t gLastDesiredDeviceId = kUnknownDeviceId;

struct InternalGuard {
    InternalGuard() { gInternal = true; }
    ~InternalGuard() { gInternal = false; }
};

struct LockGuard {
    LockGuard() { EnterCriticalSection(&gLock); }
    ~LockGuard() { LeaveCriticalSection(&gLock); }
};

struct FormatInfo {
    WORD tag = 0;
    WORD channels = 0;
    DWORD sampleRate = 0;
    WORD bitsPerSample = 0;
    WORD validBits = 0;
    WORD blockAlign = 0;
    DWORD channelMask = 0;
    GUID subFormat{};
    bool extensible = false;
    bool known = false;
};

struct ClientInfo {
    std::wstring endpointName;
    std::wstring endpointId;
    FormatInfo format;
    AUDCLNT_SHAREMODE shareMode = AUDCLNT_SHAREMODE_SHARED;
    DWORD streamFlags = 0;
};

struct RenderInfo {
    ClientInfo client;
    BYTE* lastBuffer = nullptr;
    UINT32 lastFrames = 0;
    uint64_t releaseCount = 0;
    uint64_t framesSinceLog = 0;
    uint64_t silentBuffersSinceLog = 0;
    double peakSinceLog[8]{};
    DWORD lastLogTick = 0;
};

struct DeferredDeviceApply {
    int32_t previousDesired = 0;
    int32_t targetDesired = 0;
    DWORD delayMs = 0;
};

std::unordered_map<IAudioClient*, ClientInfo> gClients;
std::unordered_map<IAudioRenderClient*, RenderInfo> gRenders;

using CoCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
using DeviceActivateFn = HRESULT(STDMETHODCALLTYPE*)(IMMDevice*, REFIID, DWORD, PROPVARIANT*, void**);
using AudioClientInitializeFn = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, AUDCLNT_SHAREMODE, DWORD,
                                                           REFERENCE_TIME, REFERENCE_TIME,
                                                           const WAVEFORMATEX*, LPCGUID);
using AudioClientGetServiceFn = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, REFIID, void**);
using AudioRenderGetBufferFn = HRESULT(STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, BYTE**);
using AudioRenderReleaseBufferFn = HRESULT(STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, DWORD);
using SetDeviceIdFn = bool(__fastcall*)(int32_t);
using ReconcileDeviceIdFn = void(__fastcall*)();

CoCreateInstanceFn RealCoCreateInstance = nullptr;
DeviceActivateFn RealDeviceActivate = nullptr;
AudioClientInitializeFn RealAudioClientInitialize = nullptr;
AudioClientGetServiceFn RealAudioClientGetService = nullptr;
AudioRenderGetBufferFn RealAudioRenderGetBuffer = nullptr;
AudioRenderReleaseBufferFn RealAudioRenderReleaseBuffer = nullptr;
SetDeviceIdFn RealSetDeviceId = nullptr;
ReconcileDeviceIdFn RealReconcileDeviceId = nullptr;

bool gDeviceHooked = false;
bool gAudioClientHooked = false;
bool gRenderHooked = false;
bool gGameHooksInstalled = false;
volatile LONG gDeferredApplyGeneration = 0;

bool IsEqualGuid(const GUID& a, const GUID& b) {
    return IsEqualGUID(a, b) != FALSE;
}

const GUID kSubTypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID kSubTypeIeeeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

void InitLogPath() {
    GetModuleFileNameW(gSelf, gLogPath, MAX_PATH);
    const std::wstring modulePath = gLogPath;
#ifdef DS_DUALSENSE_PLAYER_BUILD
    // The player DLL keeps its release defaults no matter what the file is named.
    const bool loadedAsXAudioProxy = true;
#else
    const bool loadedAsXAudioProxy = modulePath.find(L"XAudio2_9.dll") != std::wstring::npos;
#endif
    gForceDualSenseShared = modulePath.find(L"shared_fix") != std::wstring::npos ||
                            loadedAsXAudioProxy;
    wchar_t envValue[8]{};
    if (GetEnvironmentVariableW(L"DS_WASAPI_FORCE_DUALSENSE_SHARED", envValue,
                                static_cast<DWORD>(std::size(envValue))) > 0 &&
        envValue[0] == L'1') {
        gForceDualSenseShared = true;
    }
    gFixStaleBbDeviceApply = gForceDualSenseShared;
    if (GetEnvironmentVariableW(L"DS_DISABLE_BB_DEVICE_APPLY_FIX", envValue,
                                static_cast<DWORD>(std::size(envValue))) > 0 &&
        envValue[0] == L'1') {
        gFixStaleBbDeviceApply = false;
    }
    gLoggingEnabled = !loadedAsXAudioProxy;
    if (GetEnvironmentVariableW(L"DS_DUALSENSE_AUDIO_FIX_LOG", envValue,
                                static_cast<DWORD>(std::size(envValue))) > 0 &&
        envValue[0] == L'1') {
        gLoggingEnabled = true;
    }

    wchar_t* slash = wcsrchr(gLogPath, L'\\');
    if (slash) {
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - gLogPath),
                 gForceDualSenseShared ? L"ds_dualsense_shared_fix.log"
                                       : L"ds_wasapi_trace.log");
    }
}

void Log(const char* fmt, ...) {
    if (!gLoggingEnabled) {
        return;
    }

    LockGuard guard;
    FILE* f = nullptr;
    _wfopen_s(&f, gLogPath, L"a");
    if (!f) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(f, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond,
                 st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fprintf(f, "\n");
    std::fclose(f);
}

std::string Narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

bool ContainsNoCase(const std::wstring& haystack, const wchar_t* needle) {
    const std::wstring n = needle;
    if (n.empty() || haystack.size() < n.size()) {
        return false;
    }
    for (size_t i = 0; i + n.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < n.size(); ++j) {
            if (std::towlower(haystack[i + j]) != std::towlower(n[j])) {
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

bool LooksLikeControllerEndpoint(const ClientInfo& info) {
    return ContainsNoCase(info.endpointName, L"DualSense") ||
           ContainsNoCase(info.endpointName, L"Wireless Controller");
}

std::wstring GetEndpointId(IMMDevice* device) {
    LPWSTR raw = nullptr;
    std::wstring id;
    if (device && SUCCEEDED(device->GetId(&raw)) && raw) {
        id = raw;
        CoTaskMemFree(raw);
    }
    return id;
}

std::wstring GetEndpointName(IMMDevice* device) {
    IPropertyStore* store = nullptr;
    PROPVARIANT name;
    PropVariantInit(&name);
    std::wstring friendly = L"(unnamed)";

    if (device && SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store) {
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

FormatInfo ParseFormat(const WAVEFORMATEX* wf) {
    FormatInfo info;
    if (!wf) {
        return info;
    }

    info.tag = wf->wFormatTag;
    info.channels = wf->nChannels;
    info.sampleRate = wf->nSamplesPerSec;
    info.bitsPerSample = wf->wBitsPerSample;
    info.validBits = wf->wBitsPerSample;
    info.blockAlign = wf->nBlockAlign;
    info.known = true;

    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        wf->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        info.extensible = true;
        info.validBits = ext->Samples.wValidBitsPerSample;
        info.channelMask = ext->dwChannelMask;
        info.subFormat = ext->SubFormat;
    }
    return info;
}

const char* ShareModeName(AUDCLNT_SHAREMODE mode) {
    return mode == AUDCLNT_SHAREMODE_EXCLUSIVE ? "exclusive" : "shared";
}

const char* SampleTypeName(const FormatInfo& f) {
    if (f.extensible && IsEqualGuid(f.subFormat, kSubTypeIeeeFloat)) {
        return "float";
    }
    if (f.extensible && IsEqualGuid(f.subFormat, kSubTypePcm)) {
        return "pcm";
    }
    if (f.tag == WAVE_FORMAT_IEEE_FLOAT) {
        return "float";
    }
    if (f.tag == WAVE_FORMAT_PCM) {
        return "pcm";
    }
    return "unknown";
}

bool LooksLikeDualSenseFormat(const FormatInfo& f) {
    return f.known && f.sampleRate == 48000 && f.channels == 4 && f.bitsPerSample == 32 &&
           f.blockAlign == 16 && f.channelMask == 0x00000033 &&
           std::strcmp(SampleTypeName(f), "float") == 0;
}

void LogFormat(const char* prefix, const FormatInfo& f) {
    if (!f.known) {
        Log("%s format=(unknown)", prefix);
        return;
    }
    Log("%s format=%lu Hz, channels=%u, bits=%u, validBits=%u, blockAlign=%u, tag=0x%04X, "
        "extensible=%s, sampleType=%s, channelMask=0x%08lX",
        prefix, static_cast<unsigned long>(f.sampleRate), f.channels, f.bitsPerSample,
        f.validBits, f.blockAlign, f.tag, f.extensible ? "yes" : "no", SampleTypeName(f),
        static_cast<unsigned long>(f.channelMask));
}

double ReadPcm24(const BYTE* p) {
    int32_t value = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
    if (value & 0x00800000) {
        value |= 0xFF000000;
    }
    return std::abs(static_cast<double>(value) / 8388608.0);
}

void AccumulatePeaks(RenderInfo& render, const BYTE* data, UINT32 frames, DWORD flags) {
    const FormatInfo& f = render.client.format;
    render.releaseCount++;
    render.framesSinceLog += frames;
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        render.silentBuffersSinceLog++;
        return;
    }
    if (!data || !f.known || !f.channels || !f.blockAlign) {
        return;
    }

    const WORD channels = std::min<WORD>(f.channels, 8);
    const bool isFloat = (std::strcmp(SampleTypeName(f), "float") == 0);
    const bool isPcm = (std::strcmp(SampleTypeName(f), "pcm") == 0);

    for (UINT32 frame = 0; frame < frames; ++frame) {
        const BYTE* frameBase = data + static_cast<size_t>(frame) * f.blockAlign;
        for (WORD ch = 0; ch < channels; ++ch) {
            double sample = 0.0;
            const BYTE* p = frameBase + static_cast<size_t>(ch) * (f.bitsPerSample / 8);
            if (isFloat && f.bitsPerSample == 32) {
                float v = 0.0f;
                memcpy(&v, p, sizeof(v));
                sample = std::abs(static_cast<double>(v));
            } else if (isPcm && f.bitsPerSample == 16) {
                int16_t v = 0;
                memcpy(&v, p, sizeof(v));
                sample = std::abs(static_cast<double>(v) / 32768.0);
            } else if (isPcm && f.bitsPerSample == 24) {
                sample = ReadPcm24(p);
            } else if (isPcm && f.bitsPerSample == 32) {
                int32_t v = 0;
                memcpy(&v, p, sizeof(v));
                sample = std::abs(static_cast<double>(v) / 2147483648.0);
            }
            render.peakSinceLog[ch] = std::max(render.peakSinceLog[ch], sample);
        }
    }
}

void MaybeLogRenderSummary(IAudioRenderClient* self, RenderInfo& render, DWORD flags) {
    const DWORD now = GetTickCount();
    if (render.lastLogTick == 0) {
        render.lastLogTick = now;
    }
    if (now - render.lastLogTick < 250 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
        return;
    }

    const bool controller = LooksLikeControllerEndpoint(render.client);
    if (!controller && now - render.lastLogTick < 1000) {
        return;
    }

    std::string endpoint = Narrow(render.client.endpointName);
    char peaks[256]{};
    size_t used = 0;
    const WORD channels = std::min<WORD>(render.client.format.channels, 8);
    for (WORD ch = 0; ch < channels; ++ch) {
        int written = snprintf(peaks + used, sizeof(peaks) - used, "%sch%u=%.5f",
                               ch ? " " : "", ch, render.peakSinceLog[ch]);
        if (written <= 0) {
            break;
        }
        used += static_cast<size_t>(written);
        if (used >= sizeof(peaks)) {
            break;
        }
    }

    Log("render summary rc=%p endpoint=\"%s\" controller=%s releases=%llu frames=%llu "
        "silentBuffers=%llu flags=0x%08lX %s",
        self, endpoint.c_str(), controller ? "yes" : "no",
        static_cast<unsigned long long>(render.releaseCount),
        static_cast<unsigned long long>(render.framesSinceLog),
        static_cast<unsigned long long>(render.silentBuffersSinceLog),
        static_cast<unsigned long>(flags), peaks);

    render.framesSinceLog = 0;
    render.silentBuffersSinceLog = 0;
    std::fill(std::begin(render.peakSinceLog), std::end(render.peakSinceLog), 0.0);
    render.lastLogTick = now;
}

void PatchDevice(IMMDevice* device);
void PatchAudioClient(IAudioClient* client);
void PatchAudioRenderClient(IAudioRenderClient* render);

HRESULT STDMETHODCALLTYPE HookDeviceActivate(IMMDevice* self, REFIID iid, DWORD clsCtx,
                                             PROPVARIANT* activationParams, void** object) {
    if (gInternal) {
        return RealDeviceActivate(self, iid, clsCtx, activationParams, object);
    }

    HRESULT hr = RealDeviceActivate(self, iid, clsCtx, activationParams, object);
    if (SUCCEEDED(hr) && object && *object && IsEqualGUID(iid, __uuidof(IAudioClient))) {
        ClientInfo info;
        {
            InternalGuard guard;
            info.endpointName = GetEndpointName(self);
            info.endpointId = GetEndpointId(self);
        }
        IAudioClient* client = static_cast<IAudioClient*>(*object);
        PatchAudioClient(client);
        {
            LockGuard guard;
            gClients[client] = info;
        }
        Log("IMMDevice::Activate IAudioClient client=%p endpoint=\"%s\" id=\"%s\"",
            client, Narrow(info.endpointName).c_str(), Narrow(info.endpointId).c_str());
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookAudioClientInitialize(IAudioClient* self, AUDCLNT_SHAREMODE shareMode,
                                                   DWORD streamFlags,
                                                   REFERENCE_TIME bufferDuration,
                                                   REFERENCE_TIME periodicity,
                                                   const WAVEFORMATEX* format,
                                                   LPCGUID sessionGuid) {
    if (gInternal) {
        return RealAudioClientInitialize(self, shareMode, streamFlags, bufferDuration, periodicity,
                                         format, sessionGuid);
    }

    FormatInfo parsed = ParseFormat(format);
    ClientInfo infoBefore;
    {
        LockGuard guard;
        infoBefore = gClients[self];
    }

    const bool controllerCandidate =
        LooksLikeControllerEndpoint(infoBefore) || LooksLikeDualSenseFormat(parsed);
    const bool forceShared =
        gForceDualSenseShared && shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE && controllerCandidate;

    HRESULT hr = E_FAIL;
    AUDCLNT_SHAREMODE calledShareMode = shareMode;
    REFERENCE_TIME calledPeriodicity = periodicity;
    if (forceShared) {
        calledShareMode = AUDCLNT_SHAREMODE_SHARED;
        calledPeriodicity = 0;
        Log("FORCE_SHARED attempting client=%p endpoint=\"%s\" originalShare=%s flags=0x%08lX "
            "bufferDuration=%lld originalPeriodicity=%lld",
            self, Narrow(infoBefore.endpointName).c_str(), ShareModeName(shareMode),
            static_cast<unsigned long>(streamFlags), static_cast<long long>(bufferDuration),
            static_cast<long long>(periodicity));
        hr = RealAudioClientInitialize(self, calledShareMode, streamFlags, bufferDuration,
                                       calledPeriodicity, format, sessionGuid);
        if (FAILED(hr)) {
            Log("FORCE_SHARED failed hr=0x%08lX; falling back to original exclusive Initialize",
                static_cast<unsigned long>(hr));
            calledShareMode = shareMode;
            calledPeriodicity = periodicity;
            hr = RealAudioClientInitialize(self, shareMode, streamFlags, bufferDuration,
                                           periodicity, format, sessionGuid);
        }
    } else {
        hr = RealAudioClientInitialize(self, shareMode, streamFlags, bufferDuration, periodicity,
                                       format, sessionGuid);
    }

    ClientInfo info;
    {
        LockGuard guard;
        info = gClients[self];
        info.format = parsed;
        info.shareMode = calledShareMode;
        info.streamFlags = streamFlags;
        gClients[self] = info;
    }

    Log("IAudioClient::Initialize client=%p hr=0x%08lX requestedShare=%s calledShare=%s "
        "flags=0x%08lX bufferDuration=%lld periodicity=%lld calledPeriodicity=%lld "
        "endpoint=\"%s\" controller=%s dualsenseFormat=%s forceShared=%s sessionGuid=%s",
        self, static_cast<unsigned long>(hr), ShareModeName(shareMode),
        ShareModeName(calledShareMode),
        static_cast<unsigned long>(streamFlags), static_cast<long long>(bufferDuration),
        static_cast<long long>(periodicity), static_cast<long long>(calledPeriodicity),
        Narrow(info.endpointName).c_str(), LooksLikeControllerEndpoint(info) ? "yes" : "no",
        LooksLikeDualSenseFormat(parsed) ? "yes" : "no", forceShared ? "yes" : "no",
        sessionGuid ? "yes" : "no");
    LogFormat("IAudioClient::Initialize", parsed);

    return hr;
}

HRESULT STDMETHODCALLTYPE HookAudioClientGetService(IAudioClient* self, REFIID iid, void** service) {
    if (gInternal) {
        return RealAudioClientGetService(self, iid, service);
    }

    HRESULT hr = RealAudioClientGetService(self, iid, service);
    if (SUCCEEDED(hr) && service && *service && IsEqualGUID(iid, __uuidof(IAudioRenderClient))) {
        IAudioRenderClient* render = static_cast<IAudioRenderClient*>(*service);
        PatchAudioRenderClient(render);
        ClientInfo info;
        {
            LockGuard guard;
            info = gClients[self];
            gRenders[render].client = info;
        }
        Log("IAudioClient::GetService IAudioRenderClient client=%p render=%p endpoint=\"%s\" "
            "controller=%s",
            self, render, Narrow(info.endpointName).c_str(),
            LooksLikeControllerEndpoint(info) ? "yes" : "no");
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookAudioRenderGetBuffer(IAudioRenderClient* self, UINT32 frames,
                                                   BYTE** data) {
    if (gInternal) {
        return RealAudioRenderGetBuffer(self, frames, data);
    }

    HRESULT hr = RealAudioRenderGetBuffer(self, frames, data);
    if (SUCCEEDED(hr)) {
        LockGuard guard;
        RenderInfo& render = gRenders[self];
        render.lastBuffer = data ? *data : nullptr;
        render.lastFrames = frames;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookAudioRenderReleaseBuffer(IAudioRenderClient* self, UINT32 frames,
                                                       DWORD flags) {
    if (!gInternal) {
        LockGuard guard;
        auto it = gRenders.find(self);
        if (it != gRenders.end()) {
            RenderInfo& render = it->second;
            AccumulatePeaks(render, render.lastBuffer, frames, flags);
            MaybeLogRenderSummary(self, render, flags);
            render.lastBuffer = nullptr;
            render.lastFrames = 0;
        }
    }
    return RealAudioRenderReleaseBuffer(self, frames, flags);
}

HRESULT WINAPI HookCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD clsCtx, REFIID iid,
                                    LPVOID* out) {
    HRESULT hr = RealCoCreateInstance(clsid, outer, clsCtx, iid, out);
    if (!gInternal && SUCCEEDED(hr) && out && *out && IsEqualGUID(iid, __uuidof(IMMDeviceEnumerator))) {
        Log("CoCreateInstance IMMDeviceEnumerator out=%p", *out);
    }
    return hr;
}

BYTE* GameBase() {
    return reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
}

int32_t* DesiredDeviceIdPtr() {
    return reinterpret_cast<int32_t*>(GameBase() + kRvaDesiredDeviceId);
}

int32_t* ActiveDeviceIdPtr() {
    return reinterpret_cast<int32_t*>(GameBase() + kRvaActiveDeviceId);
}

int32_t ReadDeviceId(int32_t* ptr) {
    return *reinterpret_cast<volatile int32_t*>(ptr);
}

void WriteDeviceId(int32_t* ptr, int32_t value) {
    InterlockedExchange(reinterpret_cast<volatile LONG*>(ptr), static_cast<LONG>(value));
}

bool BytesMatch(const void* address, const BYTE* expected, size_t expectedSize) {
    return std::memcmp(address, expected, expectedSize) == 0;
}

void RunReconcileWithPreviousActive(const char* reason, int32_t previousDesired) {
    if (!gFixStaleBbDeviceApply || !RealReconcileDeviceId) {
        return;
    }

    int32_t desired = ReadDeviceId(DesiredDeviceIdPtr());
    int32_t active = ReadDeviceId(ActiveDeviceIdPtr());
    const bool changed = previousDesired != kUnknownDeviceId && previousDesired != desired;

    if (changed && active == desired) {
        Log("BB_DEVICE_APPLY_FIX %s restoring active to previous selected id before reconcile: "
            "desired=%d (0x%08X) active=%d (0x%08X) previous=%d (0x%08X)",
            reason, desired, static_cast<uint32_t>(desired), active,
            static_cast<uint32_t>(active), previousDesired, static_cast<uint32_t>(previousDesired));
        WriteDeviceId(ActiveDeviceIdPtr(), previousDesired);
        active = previousDesired;
    }

    if (desired != active) {
        Log("BB_DEVICE_APPLY_FIX %s calling original reconcile: desired=%d (0x%08X) "
            "active=%d (0x%08X)",
            reason, desired, static_cast<uint32_t>(desired), active,
            static_cast<uint32_t>(active));
        RealReconcileDeviceId();
    }

    gLastDesiredDeviceId = ReadDeviceId(DesiredDeviceIdPtr());
    Log("BB_DEVICE_APPLY_FIX %s after reconcile: desired=%d (0x%08X) active=%d (0x%08X)",
        reason, gLastDesiredDeviceId, static_cast<uint32_t>(gLastDesiredDeviceId),
        ReadDeviceId(ActiveDeviceIdPtr()), static_cast<uint32_t>(ReadDeviceId(ActiveDeviceIdPtr())));
}

DWORD WINAPI DeferredDeviceApplyThread(void* param) {
    DeferredDeviceApply apply = *static_cast<DeferredDeviceApply*>(param);
    delete static_cast<DeferredDeviceApply*>(param);

    const LONG generation = InterlockedIncrement(&gDeferredApplyGeneration);
    Sleep(apply.delayMs);

    if (generation != gDeferredApplyGeneration) {
        Log("BB_DEVICE_APPLY_FIX deferred skipped: superseded generation=%ld", generation);
        return 0;
    }

    const int32_t desired = ReadDeviceId(DesiredDeviceIdPtr());
    if (desired != apply.targetDesired) {
        Log("BB_DEVICE_APPLY_FIX deferred skipped: desired changed target=%d (0x%08X) "
            "current=%d (0x%08X)",
            apply.targetDesired, static_cast<uint32_t>(apply.targetDesired), desired,
            static_cast<uint32_t>(desired));
        return 0;
    }

    int32_t active = ReadDeviceId(ActiveDeviceIdPtr());
    Log("BB_DEVICE_APPLY_FIX deferred before reconcile: desired=%d (0x%08X) "
        "active=%d (0x%08X) previous=%d (0x%08X) delay=%lu",
        desired, static_cast<uint32_t>(desired), active, static_cast<uint32_t>(active),
        apply.previousDesired, static_cast<uint32_t>(apply.previousDesired),
        static_cast<unsigned long>(apply.delayMs));

    if (active == desired) {
        WriteDeviceId(ActiveDeviceIdPtr(), apply.previousDesired);
        active = apply.previousDesired;
        Log("BB_DEVICE_APPLY_FIX deferred restored active to previous selected id: "
            "active=%d (0x%08X)",
            active, static_cast<uint32_t>(active));
    }

    if (RealReconcileDeviceId) {
        RealReconcileDeviceId();
    }

    Log("BB_DEVICE_APPLY_FIX deferred after reconcile: desired=%d (0x%08X) active=%d (0x%08X)",
        ReadDeviceId(DesiredDeviceIdPtr()), static_cast<uint32_t>(ReadDeviceId(DesiredDeviceIdPtr())),
        ReadDeviceId(ActiveDeviceIdPtr()), static_cast<uint32_t>(ReadDeviceId(ActiveDeviceIdPtr())));
    gLastDesiredDeviceId = ReadDeviceId(DesiredDeviceIdPtr());
    return 0;
}

void ScheduleDeferredDeviceApply(int32_t previousDesired, int32_t targetDesired, DWORD delayMs) {
    auto* apply = new DeferredDeviceApply{previousDesired, targetDesired, delayMs};
    HANDLE thread = CreateThread(nullptr, 0, DeferredDeviceApplyThread, apply, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        delete apply;
        Log("BB_DEVICE_APPLY_FIX deferred failed to create thread");
    }
}

bool __fastcall HookSetDeviceId(int32_t deviceId) {
    const int32_t beforeDesired = ReadDeviceId(DesiredDeviceIdPtr());
    const int32_t beforeActive = ReadDeviceId(ActiveDeviceIdPtr());
    const bool ok = RealSetDeviceId(deviceId);
    const int32_t afterDesired = ReadDeviceId(DesiredDeviceIdPtr());
    const int32_t afterActive = ReadDeviceId(ActiveDeviceIdPtr());

    Log("BB_DEVICE_APPLY_FIX setter device=%d (0x%08X) ok=%s before desired=%d (0x%08X) "
        "active=%d (0x%08X) after desired=%d (0x%08X) active=%d (0x%08X)",
        deviceId, static_cast<uint32_t>(deviceId), ok ? "yes" : "no", beforeDesired,
        static_cast<uint32_t>(beforeDesired), beforeActive, static_cast<uint32_t>(beforeActive),
        afterDesired, static_cast<uint32_t>(afterDesired), afterActive,
        static_cast<uint32_t>(afterActive));

    gLastDesiredDeviceId = afterDesired;
    return ok;
}

void __fastcall HookReconcileDeviceId() {
    const int32_t beforeDesired = ReadDeviceId(DesiredDeviceIdPtr());
    const int32_t beforeActive = ReadDeviceId(ActiveDeviceIdPtr());

    RealReconcileDeviceId();
    const int32_t afterDesired = ReadDeviceId(DesiredDeviceIdPtr());
    const int32_t afterActive = ReadDeviceId(ActiveDeviceIdPtr());
    if (afterDesired != beforeDesired || afterActive != beforeActive) {
        Log("BB_DEVICE_APPLY_FIX reconcile passthrough before desired=%d (0x%08X) "
            "active=%d (0x%08X) after desired=%d (0x%08X) active=%d (0x%08X)",
            beforeDesired, static_cast<uint32_t>(beforeDesired), beforeActive,
            static_cast<uint32_t>(beforeActive), afterDesired, static_cast<uint32_t>(afterDesired),
            afterActive, static_cast<uint32_t>(afterActive));
    }
    gLastDesiredDeviceId = afterDesired;
}

void HookFunction(void* target, void* detour, void** original, const char* name) {
    if (!target) {
        return;
    }
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st == MH_OK || st == MH_ERROR_ALREADY_CREATED) {
        st = MH_EnableHook(target);
    }
    Log("hook %-32s target=%p status=%d", name, target, static_cast<int>(st));
}

bool PatchReconcileReplaceBranch(BYTE* base) {
    BYTE* target = base + kRvaReconcileReplaceBranchPatch;

    const BYTE originalBytes[] = {
        0x45, 0x33, 0xC9,                         // xor r9d, r9d
        0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00 // mov dword [rsp+20h], 0
    };
    const BYTE patchedBytes[] = {
        0x44, 0x8B, 0xC3,                         // mov r8d, ebx
        0x45, 0x33, 0xC9,                         // xor r9d, r9d
        0x44, 0x89, 0x4C, 0x24, 0x20              // mov dword [rsp+20h], r9d
    };

    if (BytesMatch(target, patchedBytes, sizeof(patchedBytes))) {
        Log("BB_DEVICE_APPLY_FIX replace-branch patch already present target=%p", target);
        return true;
    }

    if (!BytesMatch(target, originalBytes, sizeof(originalBytes))) {
        Log("BB_DEVICE_APPLY_FIX replace-branch patch skipped: signature mismatch target=%p",
            target);
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, sizeof(patchedBytes), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("BB_DEVICE_APPLY_FIX replace-branch patch failed: VirtualProtect target=%p err=%lu",
            target, static_cast<unsigned long>(GetLastError()));
        return false;
    }

    std::memcpy(target, patchedBytes, sizeof(patchedBytes));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(patchedBytes));

    DWORD ignored = 0;
    VirtualProtect(target, sizeof(patchedBytes), oldProtect, &ignored);

    Log("BB_DEVICE_APPLY_FIX replace-branch patch applied target=%p "
        "(replacement now uses desired id instead of active/old id)",
        target);
    return true;
}

void PatchGameDeviceApplyFunctions() {
    if (!gFixStaleBbDeviceApply || gGameHooksInstalled) {
        return;
    }

    BYTE* base = GameBase();
    if (!base) {
        Log("BB_DEVICE_APPLY_FIX skipped: game base unavailable");
        return;
    }

    BYTE* setTarget = base + kRvaSetDeviceId;
    BYTE* reconcileTarget = base + kRvaReconcileDeviceId;
    const BYTE setSig[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x56, 0x48, 0x83, 0xec, 0x40};
    const BYTE reconcileSig[] = {0x48, 0x83, 0xec, 0x58, 0x48, 0x8b, 0x05};

    if (!BytesMatch(setTarget, setSig, sizeof(setSig)) ||
        !BytesMatch(reconcileTarget, reconcileSig, sizeof(reconcileSig))) {
        Log("BB_DEVICE_APPLY_FIX skipped: ds.exe signature mismatch set=%p reconcile=%p",
            setTarget, reconcileTarget);
        return;
    }

    const bool branchPatchOk = PatchReconcileReplaceBranch(base);
    gLastDesiredDeviceId = ReadDeviceId(DesiredDeviceIdPtr());
    Log("BB_DEVICE_APPLY_FIX enabled desired=%d (0x%08X) active=%d (0x%08X) "
        "replaceBranchPatch=%s",
        gLastDesiredDeviceId, static_cast<uint32_t>(gLastDesiredDeviceId),
        ReadDeviceId(ActiveDeviceIdPtr()), static_cast<uint32_t>(ReadDeviceId(ActiveDeviceIdPtr())),
        branchPatchOk ? "yes" : "no");

    // These detours only record state; the branch patch above implements the fix.
    // Avoid modifying two more game functions when diagnostics are disabled.
    if (gLoggingEnabled) {
        HookFunction(setTarget, reinterpret_cast<void*>(&HookSetDeviceId),
                     reinterpret_cast<void**>(&RealSetDeviceId), "DS::SetControllerSpeakerDeviceId");
        HookFunction(reconcileTarget, reinterpret_cast<void*>(&HookReconcileDeviceId),
                     reinterpret_cast<void**>(&RealReconcileDeviceId),
                     "DS::ReconcileControllerSpeakerDeviceId");
    }
    gGameHooksInstalled = branchPatchOk;
}

void PatchDevice(IMMDevice* device) {
    if (!device || gDeviceHooked) {
        return;
    }
    void** vt = *reinterpret_cast<void***>(device);
    HookFunction(vt[3], reinterpret_cast<void*>(&HookDeviceActivate),
                 reinterpret_cast<void**>(&RealDeviceActivate), "IMMDevice::Activate");
    gDeviceHooked = true;
}

void PatchAudioClient(IAudioClient* client) {
    if (!client || gAudioClientHooked) {
        return;
    }
    void** vt = *reinterpret_cast<void***>(client);
    HookFunction(vt[3], reinterpret_cast<void*>(&HookAudioClientInitialize),
                 reinterpret_cast<void**>(&RealAudioClientInitialize), "IAudioClient::Initialize");
    if (gLoggingEnabled) {
        HookFunction(vt[14], reinterpret_cast<void*>(&HookAudioClientGetService),
                     reinterpret_cast<void**>(&RealAudioClientGetService), "IAudioClient::GetService");
    }
    gAudioClientHooked = true;
}

void PatchAudioRenderClient(IAudioRenderClient* render) {
    if (!gLoggingEnabled || !render || gRenderHooked) {
        return;
    }
    void** vt = *reinterpret_cast<void***>(render);
    HookFunction(vt[3], reinterpret_cast<void*>(&HookAudioRenderGetBuffer),
                 reinterpret_cast<void**>(&RealAudioRenderGetBuffer),
                 "IAudioRenderClient::GetBuffer");
    HookFunction(vt[4], reinterpret_cast<void*>(&HookAudioRenderReleaseBuffer),
                 reinterpret_cast<void**>(&RealAudioRenderReleaseBuffer),
                 "IAudioRenderClient::ReleaseBuffer");
    gRenderHooked = true;
}

void BootstrapComVtables() {
    InternalGuard guard;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        Log("CoInitializeEx failed hr=0x%08lX", static_cast<unsigned long>(hr));
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        Log("bootstrap MMDeviceEnumerator failed hr=0x%08lX", static_cast<unsigned long>(hr));
        if (uninit) {
            CoUninitialize();
        }
        return;
    }

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr) && collection) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (SUCCEEDED(collection->Item(i, &device)) && device) {
                PatchDevice(device);

                IAudioClient* client = nullptr;
                HRESULT activateHr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                                       reinterpret_cast<void**>(&client));
                if (SUCCEEDED(activateHr) && client) {
                    PatchAudioClient(client);

                    WAVEFORMATEX* mix = nullptr;
                    if (gLoggingEnabled && SUCCEEDED(client->GetMixFormat(&mix)) && mix) {
                        HRESULT initHr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000,
                                                            0, mix, nullptr);
                        if (SUCCEEDED(initHr)) {
                            IAudioRenderClient* render = nullptr;
                            if (SUCCEEDED(client->GetService(__uuidof(IAudioRenderClient),
                                                             reinterpret_cast<void**>(&render))) &&
                                render) {
                                PatchAudioRenderClient(render);
                                render->Release();
                            }
                            client->Stop();
                        }
                        CoTaskMemFree(mix);
                    }
                    client->Release();
                }
                device->Release();
            }
        }
        collection->Release();
    }

    enumerator->Release();
    if (uninit) {
        CoUninitialize();
    }
}

DWORD WINAPI TraceThread(void*) {
    InitLogPath();
    if (gLoggingEnabled) {
        DeleteFileW(gLogPath);
    }
    Log("ds_wasapi_trace loaded pid=%lu forceDualSenseShared=%s fixStaleBbDeviceApply=%s logging=%s",
        static_cast<unsigned long>(GetCurrentProcessId()),
        gForceDualSenseShared ? "yes" : "no", gFixStaleBbDeviceApply ? "yes" : "no",
        gLoggingEnabled ? "yes" : "no");

    MH_STATUS st = MH_Initialize();
    Log("MinHook initialize status=%d", static_cast<int>(st));
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        return 0;
    }

    PatchGameDeviceApplyFunctions();

    // This detour only records enumerator creation, so skip it when logging is off.
    if (gLoggingEnabled) {
        HMODULE ole32 = GetModuleHandleW(L"ole32.dll");
        if (!ole32) {
            ole32 = LoadLibraryW(L"ole32.dll");
        }
        if (ole32) {
            void* coCreate = reinterpret_cast<void*>(GetProcAddress(ole32, "CoCreateInstance"));
            HookFunction(coCreate, reinterpret_cast<void*>(&HookCoCreateInstance),
                         reinterpret_cast<void**>(&RealCoCreateInstance), "CoCreateInstance");
        }
    }

    BootstrapComVtables();
    Log("bootstrap complete. Toggle Controller Speaker, reload, or restart audio to capture Initialize/GetService.");
    return 0;
}

#ifdef DS_DUALSENSE_PLAYER_BUILD
// The hooks and hard-coded offsets are only meant for Death Stranding. In any
// other process the player DLL just forwards XAudio2 calls.
bool HostIsGame() {
    std::wstring path(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!len || len >= path.size()) {
        return false;
    }
    path.resize(len);
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring exeName = slash == std::wstring::npos ? path : path.substr(slash + 1);
    return _wcsicmp(exeName.c_str(), L"ds.exe") == 0;
}
#endif

} // namespace

#ifndef DS_DUALSENSE_PLAYER_BUILD
extern "C" __declspec(dllexport) DWORD DsWasapiTraceVersion() {
    return 1;
}
#endif

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        gSelf = module;
        InitializeCriticalSection(&gLock);
        DisableThreadLibraryCalls(module);
#ifdef DS_DUALSENSE_PLAYER_BUILD
        if (!HostIsGame()) {
            return TRUE;
        }
#endif
        HANDLE thread = CreateThread(nullptr, 0, TraceThread, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        DeleteCriticalSection(&gLock);
    }
    return TRUE;
}
