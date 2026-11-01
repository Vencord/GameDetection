#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cstdlib>

#include "matcher.h"

namespace {

std::string readLink(int proc, const std::string& path) {
    char buffer[PATH_MAX];
    ssize_t size = readlinkat(proc, path.c_str(), buffer, sizeof(buffer));
    return size > 0 ? std::string(buffer, size) : std::string();
}

std::vector<std::string> readArgv(int proc, const std::string& pid) {
    std::vector<std::string> argv;
    int fd = openat(proc, (pid + "/cmdline").c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return argv;

    std::string content;
    char buffer[4096];
    ssize_t size;
    while ((size = read(fd, buffer, sizeof(buffer))) > 0) content.append(buffer, size);
    close(fd);

    // Wine pads the cmdline with NULs after moving argv around, so empty items are skipped
    for (size_t start = 0; start < content.size();) {
        size_t end = content.find('\0', start);
        if (end == std::string::npos) end = content.size();
        if (end > start) argv.push_back(content.substr(start, end - start));
        start = end + 1;
    }
    return argv;
}

bool isWineLoader(std::string_view name) {
    constexpr std::array loaders = {
        std::string_view("wine"),
        std::string_view("wine64"),
        std::string_view("wine-preloader"),
        std::string_view("wine64-preloader"),
    };
    for (std::string_view loader : loaders)
        if (name == loader) return true;
    return false;
}

// x86 emulators used on arm64. /proc/pid/exe is the emulator, the actual program is in argv
bool isEmulator(std::string_view name) {
    constexpr std::array emulators = {
        std::string_view("box64"),
        std::string_view("fexinterpreter"),
        std::string_view("fexloader"),
        std::string_view("qemu-x86_64"),
        std::string_view("qemu-x86_64-static"),
    };
    for (std::string_view emulator : emulators)
        if (name == emulator) return true;
    return false;
}

bool isAbsolute(std::string_view path) {
    return path.starts_with('/') || (path.size() >= 2 && path[1] == ':');
}

// Wine's own services, like C:\windows\system32\explorer.exe, or Proton's steam.exe stub
bool isWineSystemPath(std::string_view path) {
    return path.starts_with("c:/windows/") || path.find("/drive_c/windows/") != std::string_view::npos;
}

// Steam starts every game through its reaper wrapper: reaper SteamLaunch AppId=400 -- ./game
// Other programs can be called reaper too, like the DAW, hence the check for SteamLaunch
std::string_view getSteamAppId(const std::vector<std::string>& argv, size_t& commandIndex) {
    constexpr std::string_view prefix = "AppId=";
    if (argv.size() < 2 || argv[1] != "SteamLaunch") return {};

    std::string_view appId;
    for (size_t i = 2; i < argv.size(); i++) {
        if (argv[i] == "--") {
            commandIndex = i + 1;
            return commandIndex < argv.size() ? appId : std::string_view();
        }
        if (argv[i].starts_with(prefix)) appId = std::string_view(argv[i]).substr(prefix.size());
    }
    return {};
}

std::string joinArgs(const std::vector<std::string>& argv, size_t start) {
    std::string args;
    for (size_t i = start; i < argv.size(); i++) {
        if (i > start) args += ' ';
        args += argv[i];
    }
    return args;
}

}  // namespace

std::vector<Process> listProcesses(const Database& db) {
    std::vector<Process> processes;

    DIR* dir = opendir("/proc");
    if (!dir) return processes;
    int proc = dirfd(dir);

    while (dirent* entry = readdir(dir)) {
        if (!std::isdigit((unsigned char)entry->d_name[0])) continue;
        std::string pid = entry->d_name;
        uint32_t pidNumber = std::strtoul(pid.c_str(), nullptr, 10);

        std::string exe = readLink(proc, pid + "/exe");
        if (exe.empty()) continue;
        if (exe.ends_with(" (deleted)")) exe.resize(exe.size() - 10);

        std::string exePath = normalizePath(exe);
        std::string_view exeName = baseName(exePath);

        if (exeName == "reaper") {
            std::vector<std::string> argv = readArgv(proc, pid);
            size_t commandIndex = 0;
            std::string_view appId = getSteamAppId(argv, commandIndex);
            if (db.isSteamApp(appId))
                processes.push_back({pidNumber, normalizePath(argv[commandIndex]), "", false, std::string(appId)});
            continue;
        }

        bool wine = isWineLoader(exeName);
        bool emulated = isEmulator(exeName);

        if (!wine && !emulated) {
            if (db.isCandidate(exePath, false))
                processes.push_back({pidNumber, std::move(exePath), joinArgs(readArgv(proc, pid), 1), false});
            continue;
        }

        std::vector<std::string> argv = readArgv(proc, pid);

        // Wine replaces argv with the path of the Windows executable, so loaders are usually gone by now.
        // That does not reach /proc under an emulator, where argv is "box64 wine game.exe"
        size_t index = emulated ? 1 : 0;
        while (index < argv.size() && isWineLoader(baseName(normalizePath(argv[index])))) {
            wine = true;
            index++;
        }
        if (index >= argv.size()) continue;

        std::string path = normalizePath(argv[index]);
        if (wine && !isAbsolute(path)) path = normalizePath(readLink(proc, pid + "/cwd")) + "/" + path;
        if (!db.isCandidate(path, wine) || (wine && isWineSystemPath(path))) continue;

        processes.push_back({pidNumber, std::move(path), joinArgs(argv, index + 1), wine});
    }

    closedir(dir);
    return processes;
}
