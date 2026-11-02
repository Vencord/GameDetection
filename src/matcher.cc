#include "matcher.h"

#include <array>
#include <cctype>
#include <unordered_set>

namespace {

#ifdef __APPLE__
constexpr std::string_view nativeOs = "darwin";
#else
constexpr std::string_view nativeOs = "linux";
#endif

constexpr std::string_view vanillaMinecraft = "net.minecraft.client.main.Main";

// Discord's database only contains the main class of vanilla Minecraft. These identify everything else
constexpr std::array<std::string_view, 13> moddedMinecraft = {
    // Forge and NeoForge. Their main classes are shared with dedicated servers, the launch targets are not
    "--launchTarget fmlclient",
    "--launchTarget forgeclient",
    "--launchTarget forge_client",
    "--launchTarget neoforgeclient",
    "net.neoforged.fml.startup.Client",
    // Fabric, Quilt, and Forge or LiteLoader before Minecraft 1.13
    "net.fabricmc.loader.impl.launch.knot.KnotClient",
    "net.fabricmc.loader.launch.knot.KnotClient",
    "org.quiltmc.loader.impl.launch.knot.KnotClient",
    "net.minecraft.launchwrapper.Launch",
    // Launchers that wrap the game and pass the real main class via stdin or IPC
    "org.prismlauncher.EntryPoint",
    "org.polymc.EntryPoint",
    "org.multimc.EntryPoint",
    "com.modrinth.theseus.MinecraftLaunch",
};

bool hasPathSuffix(std::string_view path, std::string_view suffix) {
    if (!path.ends_with(suffix)) return false;
    return path.size() == suffix.size() || path[path.size() - suffix.size() - 1] == '/';
}

// Whole word search so that "-game hl2" does not match "-game hl2mp"
bool hasArgument(std::string_view args, std::string_view wanted) {
    for (size_t pos = args.find(wanted); pos != std::string_view::npos; pos = args.find(wanted, pos + 1)) {
        size_t end = pos + wanted.size();
        bool startsWord = pos == 0 || std::isspace((unsigned char)args[pos - 1]);
        bool endsWord = end == args.size() || std::isspace((unsigned char)args[end]);
        if (startsWord && endsWord) return true;
    }
    return false;
}

}  // namespace

std::string normalizePath(std::string_view path) {
    std::string result(path);
    for (char& c : result) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return result;
}

std::string_view baseName(std::string_view path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

void Database::addGame(std::string id, std::string name) {
    games.push_back({std::move(id), std::move(name)});
}

void Database::addExecutable(std::string_view os, std::string_view name, std::string args) {
    // The arguments identify the game for these, not the JVM, so macOS entries work on Linux as well
    if (os == "darwin" && name == ">java" && !args.empty()) os = nativeOs;

    RuleMap* rules = os == "win32" ? &windowsRules : os == nativeOs ? &nativeRules : nullptr;
    if (!rules || name.empty()) return;

    bool exactName = name[0] == '>';
    if (exactName) name.remove_prefix(1);

    std::string path = normalizePath(name);
    if (exactName && args == vanillaMinecraft) addMinecraftRules(*rules, path);
    addRule(*rules, std::move(path), exactName, std::move(args));
}

void Database::addRule(RuleMap& rules, std::string path, bool exactName, std::string args) {
    std::string key(baseName(path));
    rules[std::move(key)].push_back({std::move(path), exactName, std::move(args), games.size() - 1});
}

void Database::addMinecraftRules(RuleMap& rules, const std::string& jvm) {
    for (std::string_view args : moddedMinecraft) addRule(rules, jvm, true, std::string(args));

    // Unlike the official launcher, third party ones may use java.exe on Windows
    if (jvm == "javaw.exe") {
        addRule(rules, "java.exe", true, std::string(vanillaMinecraft));
        for (std::string_view args : moddedMinecraft) addRule(rules, "java.exe", true, std::string(args));
    }
}

void Database::addSteamApp(std::string_view appId) {
    // Some ids in the database have stray whitespace
    size_t end = appId.find_last_not_of(" \t");
    if (end != std::string_view::npos) steamApps.emplace(appId.substr(0, end + 1), games.size() - 1);
}

bool Database::isSteamApp(std::string_view appId) const {
    return steamApps.find(appId) != steamApps.end();
}

bool Database::isCandidate(std::string_view path, bool windowsBinary) const {
    const RuleMap& rules = windowsBinary ? windowsRules : nativeRules;
    return rules.find(baseName(path)) != rules.end();
}

const Game* Database::match(const Process& process) const {
    if (!process.steamAppId.empty()) {
        auto it = steamApps.find(process.steamAppId);
        return it == steamApps.end() ? nullptr : &games[it->second];
    }

    const RuleMap& rules = process.windowsBinary ? windowsRules : nativeRules;
    auto it = rules.find(baseName(process.path));
    if (it == rules.end()) return nullptr;

    // Several games can share an executable name, like hl2.exe. Prefer the most specific rule
    const Rule* best = nullptr;
    size_t bestScore = 0;
    for (const Rule& rule : it->second) {
        if (!rule.exactName && !hasPathSuffix(process.path, rule.path)) continue;
        if (!rule.args.empty() && !hasArgument(process.args, rule.args)) continue;

        size_t score = rule.path.size() + (rule.args.empty() ? 0 : 0x10000);
        if (score > bestScore) {
            best = &rule;
            bestScore = score;
        }
    }
    return best ? &games[best->game] : nullptr;
}

std::vector<Detection> detect(const Database& db) {
    std::vector<Detection> detections;
    std::unordered_set<const Game*> seen;
    for (Process& process : listProcesses(db)) {
        const Game* game = db.match(process);
        if (game && seen.insert(game).second)
            detections.push_back({game, std::move(process)});
    }
    return detections;
}
