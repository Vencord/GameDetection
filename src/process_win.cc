#include <windows.h>
#include <winternl.h>

#include <cwchar>
#include <unordered_set>

#include "matcher.h"

namespace {

// Not part of the public PROCESSINFOCLASS enum. Available since Windows 8.1
const auto ProcessCommandLineInformation = (PROCESSINFOCLASS)60;

using PidSet = std::unordered_set<DWORD>;

std::string toUtf8(const wchar_t* text, int length) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}

BOOL CALLBACK collectChildWindow(HWND window, LPARAM pids) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    reinterpret_cast<PidSet*>(pids)->insert(pid);
    return TRUE;
}

BOOL CALLBACK collectWindow(HWND window, LPARAM pids) {
    if (!IsWindowVisible(window)) return TRUE;
    collectChildWindow(window, pids);

    // UWP apps draw into a child of a frame window that is owned by ApplicationFrameHost
    wchar_t className[32];
    if (GetClassNameW(window, className, ARRAYSIZE(className)) && wcscmp(className, L"ApplicationFrameWindow") == 0)
        EnumChildWindows(window, collectChildWindow, pids);
    return TRUE;
}

std::string getImagePath(HANDLE process) {
    wchar_t path[MAX_PATH * 4];
    DWORD size = ARRAYSIZE(path);
    if (!QueryFullProcessImageNameW(process, 0, path, &size)) return "";
    return normalizePath(toUtf8(path, size));
}

std::string getCommandLine(HANDLE process) {
    ULONG size = 0;
    NtQueryInformationProcess(process, ProcessCommandLineInformation, nullptr, 0, &size);
    if (size == 0) return "";

    std::vector<char> buffer(size);
    if (NtQueryInformationProcess(process, ProcessCommandLineInformation, buffer.data(), size, &size) < 0) return "";

    auto* commandLine = reinterpret_cast<UNICODE_STRING*>(buffer.data());
    return toUtf8(commandLine->Buffer, commandLine->Length / sizeof(wchar_t));
}

std::string getWindowsDirectory() {
    wchar_t path[MAX_PATH];
    UINT size = GetWindowsDirectoryW(path, ARRAYSIZE(path));
    return normalizePath(toUtf8(path, size)) + "/";
}

}  // namespace

// Only looks at processes with a visible window. Besides being much cheaper than inspecting every process,
// this rules out background services whose name collides with a game, like Intel's LMS.exe. EnumWindows is
// limited to our own desktop, so session 0 services can never show up.
std::vector<Process> listProcesses(const Database& db) {
    std::vector<Process> processes;

    PidSet pids;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&pids));

    static const std::string windowsDirectory = getWindowsDirectory();

    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) continue;

        std::string path = getImagePath(process);
        if (db.isCandidate(path, true) && !path.starts_with(windowsDirectory))
            processes.push_back({(uint32_t)pid, std::move(path), getCommandLine(process), true});

        CloseHandle(process);
    }
    return processes;
}
