#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct Process {
    uint32_t pid;
    // Normalized with normalizePath
    std::string path;
    std::string args;
    // A Windows executable, so it is matched against the win32 entries. False for native Linux processes
    bool windowsBinary;
    // If set, the process is identified by this id instead of its path
    std::string steamAppId = {};
};

struct Game {
    std::string id;
    std::string name;
};

struct Rule {
    // Lowercase path suffix, or just the basename if exactName is set
    std::string path;
    // Entries prefixed with > only match on the basename
    bool exactName;
    std::string args;
    size_t game;
};

// Transparent so lookups do not need to allocate a string for every process
struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};

template <typename T>
using StringMap = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;
using RuleMap = StringMap<std::vector<Rule>>;

class Database {
public:
    void addGame(std::string id, std::string name);
    // Adds an executable to the last added game
    void addExecutable(std::string_view os, std::string_view name, std::string args);
    // Adds a Steam app id to the last added game
    void addSteamApp(std::string_view appId);

    bool isCandidate(std::string_view path, bool windowsBinary) const;
    bool isSteamApp(std::string_view appId) const;
    const Game* match(const Process& process) const;

private:
    void addRule(RuleMap& rules, std::string path, bool exactName, std::string args);
    void addMinecraftRules(RuleMap& rules, const std::string& jvm);

    std::vector<Game> games;
    RuleMap windowsRules;
    RuleMap linuxRules;
    StringMap<size_t> steamApps;
};

struct Detection {
    const Game* game;
    Process process;
};

// Lowercases and converts backslashes to forward slashes
std::string normalizePath(std::string_view path);
std::string_view baseName(std::string_view path);

std::vector<Detection> detect(const Database& db);

// Implemented per platform. Only returns processes that passed Database::isCandidate or Database::isSteamApp
std::vector<Process> listProcesses(const Database& db);
