#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>

#include <cstdio>

namespace {

HMODULE gRealXAudio = nullptr;

HMODULE RealXAudio() {
    if (gRealXAudio) {
        return gRealXAudio;
    }

    wchar_t path[MAX_PATH]{};
    const UINT len = GetSystemDirectoryW(path, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return nullptr;
    }
    wcscat_s(path, L"\\XAudio2_9.dll");
    gRealXAudio = LoadLibraryW(path);
    return gRealXAudio;
}

FARPROC RealProc(const char* name) {
    HMODULE real = RealXAudio();
    return real ? GetProcAddress(real, name) : nullptr;
}

} // namespace

extern "C" __declspec(dllexport) HRESULT WINAPI XAudio2Create(
    IXAudio2** pxaudio2, UINT32 flags, XAUDIO2_PROCESSOR processor) {
    using Fn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    auto fn = reinterpret_cast<Fn>(RealProc("XAudio2Create"));
    return fn ? fn(pxaudio2, flags, processor) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

extern "C" __declspec(dllexport) HRESULT WINAPI XAudio2CreateWithVersionInfo(
    IXAudio2** pxaudio2, UINT32 flags, XAUDIO2_PROCESSOR processor, DWORD version) {
    using Fn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR, DWORD);
    auto fn = reinterpret_cast<Fn>(RealProc("XAudio2CreateWithVersionInfo"));
    return fn ? fn(pxaudio2, flags, processor, version)
              : XAudio2Create(pxaudio2, flags, processor);
}

extern "C" __declspec(dllexport) HRESULT WINAPI XAudio2CreateV2_9(
    IXAudio2** pxaudio2, UINT32 flags, XAUDIO2_PROCESSOR processor) {
    using Fn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    auto fn = reinterpret_cast<Fn>(RealProc("XAudio2CreateV2_9"));
    return fn ? fn(pxaudio2, flags, processor) : XAudio2Create(pxaudio2, flags, processor);
}

extern "C" __declspec(dllexport) HRESULT WINAPI XAudio2CreateWithSharedContexts(
    IXAudio2** pxaudio2, UINT32 flags, XAUDIO2_PROCESSOR processor) {
    using Fn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    auto fn = reinterpret_cast<Fn>(RealProc("XAudio2CreateWithSharedContexts"));
    return fn ? fn(pxaudio2, flags, processor) : XAudio2Create(pxaudio2, flags, processor);
}
