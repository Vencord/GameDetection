#include <libproc.h>
#include <sys/sysctl.h>

#include <algorithm>
#include <cstring>

#include "matcher.h"

namespace {

// KERN_PROCARGS2 is laid out as argc, the executable path, NUL padding, then the NUL separated argv
std::string readArgs(pid_t pid) {
    static const size_t argMax = [] {
        int mib[] = {CTL_KERN, KERN_ARGMAX};
        int max = 0;
        size_t size = sizeof(max);
        return sysctl(mib, 2, &max, &size, nullptr, 0) == 0 ? (size_t)max : 0;
    }();

    std::string buffer(argMax, '\0');
    size_t size = buffer.size();
    int mib[] = {CTL_KERN, KERN_PROCARGS2, pid};
    if (sysctl(mib, 3, buffer.data(), &size, nullptr, 0) != 0 || size < sizeof(int)) return {};
    buffer.resize(size);

    int argc;
    std::memcpy(&argc, buffer.data(), sizeof(argc));
    size_t pos = buffer.find_first_not_of('\0', buffer.find('\0', sizeof(argc)));

    std::string args;
    for (int i = 0; i < argc && pos < buffer.size(); i++) {
        size_t end = std::min(buffer.find('\0', pos), buffer.size());
        if (i > 1) args += ' ';
        if (i > 0) args.append(buffer, pos, end - pos);
        pos = end + 1;
    }
    return args;
}

}  // namespace

std::vector<Process> listProcesses(const Database& db) {
    std::vector<Process> processes;

    int count = proc_listallpids(nullptr, 0);
    if (count <= 0) return processes;
    // Leaves room for processes started in between
    std::vector<pid_t> pids(count + 64);
    count = proc_listallpids(pids.data(), pids.size() * sizeof(pid_t));

    for (int i = 0; i < count; i++) {
        pid_t pid = pids[i];
        char buffer[PROC_PIDPATHINFO_MAXSIZE];
        int size = proc_pidpath(pid, buffer, sizeof(buffer));
        if (size <= 0) continue;

        std::string path = normalizePath(std::string_view(buffer, size));

        // Most darwin entries name the app bundle, like "factorio.app", rather than the executable inside it
        if (!db.isCandidate(path, false)) {
            size_t bundle = path.rfind(".app/");
            if (bundle == std::string::npos) continue;
            path.resize(bundle + 4);
            if (!db.isCandidate(path, false)) continue;
        }

        processes.push_back({(uint32_t)pid, std::move(path), readArgs(pid), false});
    }
    return processes;
}
