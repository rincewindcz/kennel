#pragma once

#include <string>
#include <string_view>

// File locations. KENNEL_ROOT prefixes absolute library paths (sources, ROM and art
// directories) so a test tree can stand in for a device; /proc, /sys and /dev are never prefixed.
struct Paths {
    std::string root;
    std::string home;
    std::string exe;
    std::string exeDir;
    std::string assets;
    std::string config;
    std::string data;
    std::string cache;
    std::string runtime;

    void init(const char* argv0);
    std::string expand(std::string_view path) const;
    std::string asset(std::string_view rel) const { return assets + "/" + std::string(rel); }
};

extern Paths paths;
