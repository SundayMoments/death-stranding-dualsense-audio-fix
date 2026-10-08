#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr wchar_t kDefaultGameExe[] =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\DEATH STRANDING DIRECTORS CUT\\ds.exe";

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring ExeDir(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring DefaultDllPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) {
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path),
                 L"ds_dualsense_shared_fix.dll");
    }

    wchar_t full[MAX_PATH]{};
    GetFullPathNameW(path, MAX_PATH, full, nullptr);
    return full;
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool InjectDll(HANDLE process, const std::wstring& dllPath) {
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::printf("VirtualAllocEx failed, error=%lu\n", GetLastError());
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, &written) ||
        written != bytes) {
        std::printf("WriteProcessMemory failed, error=%lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLibraryW =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
    if (!loadLibraryW) {
        std::printf("LoadLibraryW address not found\n");
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remote, 0, nullptr);
    if (!thread) {
        std::printf("CreateRemoteThread failed, error=%lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(thread, 10000);
    DWORD result = 0;
    GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);

    if (!result) {
        std::printf("LoadLibraryW failed in target process\n");
        return false;
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 &&
        (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0)) {
        std::printf("usage:\n");
        std::printf("  ds_dualsense_shared_fix_launcher.exe\n");
        std::printf("  ds_dualsense_shared_fix_launcher.exe <path-to-ds.exe>\n");
        return 0;
    }

    std::wstring gameExe = argc >= 2 ? argv[1] : kDefaultGameExe;
    wchar_t fullGameExe[MAX_PATH]{};
    GetFullPathNameW(gameExe.c_str(), MAX_PATH, fullGameExe, nullptr);
    gameExe = fullGameExe;

    const std::wstring dllPath = DefaultDllPath();
    const std::wstring workDir = ExeDir(gameExe);
    if (!FileExists(gameExe)) {
        std::printf("game exe not found\n");
        std::wprintf(L"  %ls\n", gameExe.c_str());
        return 2;
    }
    if (!FileExists(dllPath)) {
        std::printf("fix dll not found\n");
        std::wprintf(L"  %ls\n", dllPath.c_str());
        return 3;
    }

    std::wstring commandLine = Quote(gameExe);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(gameExe.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, workDir.c_str(), &si, &pi)) {
        std::printf("CreateProcessW failed, error=%lu\n", GetLastError());
        std::printf("Steam may need to be running, or this game may refuse direct launch.\n");
        return 4;
    }

    bool injected = InjectDll(pi.hProcess, dllPath);
    if (!injected) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 5;
    }

    ResumeThread(pi.hThread);
    std::printf("launched Death Stranding with DualSense shared-endpoint fix\n");
    std::printf("pid: %lu\n", static_cast<unsigned long>(pi.dwProcessId));
    std::wprintf(L"log: %ls\\ds_dualsense_shared_fix.log\n", ExeDir(dllPath).c_str());
    std::printf("test: set BB Voice Output to DualSense and trigger BB audio\n");

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
