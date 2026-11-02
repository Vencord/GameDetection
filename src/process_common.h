#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "matcher.h"

inline bool isWineLoader(std::string_view name) {
    constexpr std::string_view preloader = "-preloader";
    if (name.ends_with(preloader)) name.remove_suffix(preloader.size());
    constexpr std::string_view loaders[] = {"wine", "wine64", "wineloader", "wineloader64"};
    for (std::string_view loader : loaders)
        if (name == loader) return true;
    return false;
}

inline bool isWineSystemPath(std::string_view path) {
    return path.starts_with("c:/windows/") || path.find("/drive_c/windows/") != std::string_view::npos;
}

inline bool isAbsolute(std::string_view path) {
    return path.starts_with('/') || (path.size() >= 2 && path[1] == ':');
}

inline size_t skipWineLoaders(const std::vector<std::string>& argv, size_t index) {
    while (index < argv.size() && isWineLoader(baseName(normalizePath(argv[index])))) ++index;
    return index;
}

inline std::string joinArgs(const std::vector<std::string>& argv, size_t start) {
    std::string args;
    for (size_t i = start; i < argv.size(); i++) {
        if (i > start) args += ' ';
        args += argv[i];
    }
    return args;
}
