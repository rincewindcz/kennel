#include "util.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <glob.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(sep, start);
        if (end == std::string_view::npos) end = s.size();
        std::string item = trim(s.substr(start, end - start));
        if (!item.empty()) out.push_back(std::move(item));
        start = end + 1;
    }
    return out;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string shellQuote(std::string_view s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string strprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < (int)sizeof buf) return std::string(buf, n > 0 ? n : 0);
    std::string out(n, '\0');
    va_start(ap, fmt);
    vsnprintf(out.data(), n + 1, fmt, ap);
    va_end(ap);
    return out;
}

std::optional<std::string> readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

std::string readTail(const std::string& path, size_t maxBytes, bool* truncated) {
    if (truncated) *truncated = false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string out;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > (long)maxBytes) {
            fseek(f, size - (long)maxBytes, SEEK_SET);
            if (truncated) *truncated = true;
        } else {
            fseek(f, 0, SEEK_SET);
        }
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && out.size() < maxBytes) out.append(buf, n);
    fclose(f);
    if (truncated && *truncated) {
        size_t nl = out.find('\n');
        if (nl != std::string::npos) out.erase(0, nl + 1);
    }
    return out;
}

std::string readLine(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    char buf[512];
    std::string out;
    if (fgets(buf, sizeof buf, f)) out = trim(buf);
    fclose(f);
    return out;
}

bool readLong(const std::string& path, long& out) {
    std::string s = readLine(path);
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long v = strtol(s.c_str(), &end, 10);
    if (errno || end == s.c_str()) return false;
    out = v;
    return true;
}

static bool writeAll(int fd, std::string_view data) {
    while (!data.empty()) {
        ssize_t n = write(fd, data.data(), data.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data.remove_prefix((size_t)n);
    }
    return true;
}

bool writeFileAtomic(const std::string& path, std::string_view data) {
    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    bool ok = writeAll(fd, data) && fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool writeFile(const std::string& path, std::string_view data) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    bool ok = writeAll(fd, data);
    return close(fd) == 0 && ok;
}

bool appendFile(const std::string& path, std::string_view data) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    bool ok = writeAll(fd, data);
    return close(fd) == 0 && ok;
}

bool makeDirs(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty() && mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (i < path.size()) cur += path[i];
    }
    return isDir(path);
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool isDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string dirName(const std::string& path) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string baseName(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::vector<std::string> glob(const std::string& pattern) {
    std::vector<std::string> out;
    glob_t g{};
    if (::glob(pattern.c_str(), 0, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; ++i) out.emplace_back(g.gl_pathv[i]);
    globfree(&g);
    return out;
}

std::string runCapture(const std::string& command, int* status) {
    std::string out;
    FILE* p = popen((command + " 2>&1").c_str(), "r");
    if (!p) {
        if (status) *status = -1;
        return out;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0 && out.size() < (1u << 20)) out.append(buf, n);
    int rc = pclose(p);
    if (status) *status = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    return out;
}

bool spawnDetached(const std::string& command) {
    const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    pid_t pid;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    int rc = posix_spawn(&pid, "/bin/sh", nullptr, &attr, (char* const*)argv, environ);
    posix_spawnattr_destroy(&attr);
    return rc == 0;
}

uint64_t fnv1a(std::string_view s, uint64_t h) {
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

static uint32_t hsv(float h, float s, float v) {
    float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.f, 2.f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    auto ch = [&](float f) { return (uint32_t)std::lround((f + m) * 255); };
    return ch(r) << 16 | ch(g) << 8 | ch(b);
}

uint32_t colorFromName(std::string_view name) {
    return hsv((float)(fnv1a(lower(name)) % 360), 0.55f, 0.62f);
}

bool parseColor(std::string_view s, uint32_t& out) {
    std::string t = trim(s);
    if (!t.empty() && t[0] == '#') t.erase(0, 1);
    if (t.size() != 6) return false;
    char* end = nullptr;
    unsigned long v = strtoul(t.c_str(), &end, 16);
    if (*end) return false;
    out = (uint32_t)v;
    return true;
}

double uptimeSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

double wallSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

bool wallClockValid(double wall) { return wall > 1704067200.0; }  // 2024-01-01

std::string bootId() { return readLine("/proc/sys/kernel/random/boot_id"); }

std::string formatDuration(double seconds) {
    long s = (long)std::lround(seconds < 0 ? 0 : seconds);
    if (s < 60) return strprintf("%ld s", s);
    long m = s / 60;
    if (m < 60) return strprintf("%ld min", m);
    long h = m / 60;
    m %= 60;
    if (m == 0) return strprintf("%ld h", h);
    return strprintf("%ld h %ld min", h, m);
}

std::string formatClock(double seconds) {
    long s = (long)seconds;
    long h = s / 3600, m = (s / 60) % 60;
    return strprintf("%ld:%02ld", h, m);
}

std::string formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024 && u < 4) {
        v /= 1024;
        ++u;
    }
    return u == 0 ? strprintf("%llu B", (unsigned long long)bytes)
                  : strprintf(v < 10 ? "%.1f %s" : "%.0f %s", v, units[u]);
}

std::string formatAgo(double seconds) {
    if (seconds < 60) return "just now";
    long m = (long)(seconds / 60);
    if (m < 60) return strprintf("%ld min ago", m);
    long h = m / 60;
    if (h < 24) return strprintf("%ld h ago", h);
    long d = h / 24;
    if (d == 1) return "yesterday";
    if (d < 30) return strprintf("%ld days ago", d);
    long mo = d / 30;
    if (mo < 12) return mo == 1 ? "a month ago" : strprintf("%ld months ago", mo);
    return d / 365 == 1 ? "a year ago" : strprintf("%ld years ago", d / 365);
}

static FILE* gLogFile = nullptr;

void logToFile(const std::string& path) {
    if (gLogFile) fclose(gLogFile);
    gLogFile = fopen(path.c_str(), "w");
    if (gLogFile) setvbuf(gLogFile, nullptr, _IOLBF, 0);
}

void logf(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    double t = uptimeSeconds();
    fprintf(stderr, "[%9.3f] kennel: %s\n", t, msg);
    if (gLogFile) fprintf(gLogFile, "[%9.3f] %s\n", t, msg);
}
