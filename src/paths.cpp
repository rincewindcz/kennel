#include "paths.h"

#include <climits>
#include <cstdlib>
#include <unistd.h>

#include "util.h"

Paths paths;

static std::string env(const char* name) {
    const char* v = getenv(name);
    return v ? v : "";
}

void Paths::init(const char* argv0) {
    root = env("KENNEL_ROOT");
    while (!root.empty() && root.back() == '/') root.pop_back();
    home = env("HOME");
    if (home.empty()) home = "/tmp";

    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    exe = n > 0 ? std::string(buf, n) : std::string(argv0);
    exeDir = dirName(exe);

    assets = env("KENNEL_ASSETS");
    if (assets.empty()) {
        for (const std::string& c : {exeDir + "/assets", exeDir + "/../share/kennel/assets"})
            if (isDir(c)) {
                assets = c;
                break;
            }
    }

    std::string xdgConfig = env("XDG_CONFIG_HOME"), xdgData = env("XDG_DATA_HOME"), xdgCache = env("XDG_CACHE_HOME");
    config = (xdgConfig.empty() ? home + "/.config" : xdgConfig) + "/kennel";
    data = (xdgData.empty() ? home + "/.local/share" : xdgData) + "/kennel";
    cache = (xdgCache.empty() ? home + "/.cache" : xdgCache) + "/kennel";

    std::string xdgRuntime = env("XDG_RUNTIME_DIR");
    if (!xdgRuntime.empty()) runtime = xdgRuntime + "/kennel";
    else if (isDir("/dev/shm")) runtime = strprintf("/dev/shm/kennel-%d", (int)getuid());
    else runtime = strprintf("/tmp/kennel-%d", (int)getuid());

    for (const std::string& d : {config, data, cache, runtime}) makeDirs(d);
}

std::string Paths::expand(std::string_view path) const {
    if (path.empty()) return {};
    if (path == "~") return home;
    if (path.substr(0, 2) == "~/") return home + std::string(path.substr(1));
    if (path[0] != '/' || root.empty()) return std::string(path);
    for (std::string_view raw : {"/proc/", "/sys/", "/dev/"})
        if (path.substr(0, raw.size()) == raw) return std::string(path);
    if (path.substr(0, root.size() + 1) == root + "/") return std::string(path);
    return root + std::string(path);
}
