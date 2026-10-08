#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr wchar_t kProcessName[] = L"ds.exe";

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

std::string DefaultDllPath() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) {
        strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "ds_wasapi_trace.dll");
    }

    char full[MAX_PATH]{};
    GetFullPathNameA(path, MAX_PATH, full, nullptr);
    return full;
}

std::string DefaultLogPath() {
    std::string path = DefaultDllPath();
    const size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) {
        path.resize(slash + 1);
    } else {
        path.clear();
    }
    path += "ds_wasapi_trace.log";
    return path;
}

bool FileExists(const std::string& path) {
    const DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

int main(int argc, char** argv) {
    DWORD pid = 0;
    std::string dllPath = DefaultDllPath();

    if (argc >= 2) {
        if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
            std::printf("usage:\n");
            std::printf("  ds_wasapi_trace_injector.exe              inject into running ds.exe\n");
            std::printf("  ds_wasapi_trace_injector.exe <pid>        inject into pid\n");
            std::printf("  ds_wasapi_trace_injector.exe <pid> <dll>  inject explicit dll path\n");
            return 0;
        }
        pid = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 0));
    }
    if (argc >= 3) {
        char full[MAX_PATH]{};
        GetFullPathNameA(argv[2], MAX_PATH, full, nullptr);
        dllPath = full;
    }

    if (!pid) {
        pid = FindProcessId();
    }
    if (!pid) {
        std::printf("ds.exe is not running\n");
        return 2;
    }
    if (!FileExists(dllPath)) {
        std::printf("trace dll not found: %s\n", dllPath.c_str());
        return 3;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        std::printf("failed to open pid %lu; try running as administrator\n",
                    static_cast<unsigned long>(pid));
        return 4;
    }

    const SIZE_T bytes = dllPath.size() + 1;
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::printf("VirtualAllocEx failed, error=%lu\n", GetLastError());
        CloseHandle(process);
        return 5;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, &written) || written != bytes) {
        std::printf("WriteProcessMemory failed, error=%lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 6;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLibraryA = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel32, "LoadLibraryA"));
    if (!loadLibraryA) {
        std::printf("LoadLibraryA address not found\n");
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 7;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryA, remote, 0, nullptr);
    if (!thread) {
        std::printf("CreateRemoteThread failed, error=%lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 8;
    }

    WaitForSingleObject(thread, 10000);
    DWORD remoteResult = 0;
    GetExitCodeThread(thread, &remoteResult);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    if (!remoteResult) {
        std::printf("LoadLibraryA failed in target process\n");
        return 9;
    }

    std::printf("injected ds_wasapi_trace.dll into pid %lu\n", static_cast<unsigned long>(pid));
    std::printf("log: %s\n", DefaultLogPath().c_str());
    std::printf("next: toggle Controller Speaker, reload, or trigger BB/controller-speaker audio\n");
    return 0;
}
