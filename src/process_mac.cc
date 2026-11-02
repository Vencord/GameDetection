#include <libproc.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <cstring>
#include <iterator>
#include <vector>

#include "process_common.h"

namespace {

std::vector<std::string> readArgv(pid_t pid, std::vector<char>& buffer) {
    if (buffer.empty()) {
        int argMax = 0;
        size_t size = sizeof(argMax);
        int mib[] = {CTL_KERN, KERN_ARGMAX};
        if (sysctl(mib, std::size(mib), &argMax, &size, nullptr, 0) != 0 || argMax <= 0) return {};
        buffer.resize(argMax);
    }

    int mib[] = {CTL_KERN, KERN_PROCARGS2, pid};
    size_t size = buffer.size();
    if (sysctl(mib, std::size(mib), buffer.data(), &size, nullptr, 0) != 0 || size < sizeof(int)) return {};

    int argc;
    std::memcpy(&argc, buffer.data(), sizeof(argc));
    const char* pos = buffer.data() + sizeof(argc);
    const char* end = buffer.data() + size;

    // The kernel returns argc, the image path, NUL padding, then argv.
    while (pos < end && *pos) ++pos;
    while (pos < end && !*pos) ++pos;

    std::vector<std::string> argv;
    for (int i = 0; i < argc && pos < end; ++i) {
        const char* start = pos;
        while (pos < end && *pos) ++pos;
        argv.emplace_back(start, pos - start);
        // Only the image path has padding; argv can contain empty strings.
        if (pos < end) ++pos;
    }
    return argv;
}

std::string readCwd(pid_t pid) {
    proc_vnodepathinfo info{};
    int size = proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &info, sizeof(info));
    return size == (int)sizeof(info) ? std::string(info.pvi_cdir.vip_path) : std::string();
}

}  // namespace

std::vector<Process> listProcesses(const Database& db) {
    std::vector<Process> processes;

    int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes <= 0) return processes;
    std::vector<pid_t> pids(bytes / sizeof(pid_t));
    bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), (int)(pids.size() * sizeof(pid_t)));
    if (bytes <= 0) return processes;

    std::vector<char> argsBuffer;
    for (size_t i = 0; i < (size_t)bytes / sizeof(pid_t); ++i) {
        pid_t pid = pids[i];
        if (pid <= 0) continue;

        char image[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pid, image, sizeof(image)) <= 0) continue;
        std::string path = normalizePath(image);
        std::string_view name = baseName(path);
        bool wine = isWineLoader(name) || name.ends_with(".exe") || path.find("/winetemp-") != std::string_view::npos;
        if (wine) {
            std::vector<std::string> argv = readArgv(pid, argsBuffer);
            size_t index = skipWineLoaders(argv, 0);
            if (index >= argv.size()) continue;

            std::string exe = normalizePath(argv[index]);
            if (!exe.ends_with(".exe")) continue;
            if (!isAbsolute(exe)) {
                std::string cwd = readCwd(pid);
                if (!cwd.empty()) exe = normalizePath(cwd) + "/" + exe;
            }
            if (isWineSystemPath(exe) || !db.isCandidate(exe, true)) continue;
            processes.push_back({(uint32_t)pid, std::move(exe), joinArgs(argv, index + 1), true});
            continue;
        }

        std::string bundle;
        if (size_t app = path.rfind(".app/contents/"); app != std::string::npos)
            bundle = path.substr(0, app + 4);
        if (!db.isCandidate(path, false) && !db.isCandidate(bundle, false)) continue;
        processes.push_back({(uint32_t)pid, std::move(path), joinArgs(readArgv(pid, argsBuffer), 1),
                             false, {}, std::move(bundle)});
    }
    return processes;
}
