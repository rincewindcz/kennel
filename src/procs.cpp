#include "procs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <pwd.h>
#include <unistd.h>

#include "util.h"

std::string userName(int uid) {
    static std::unordered_map<int, std::string> cache;
    auto it = cache.find(uid);
    if (it != cache.end()) return it->second;
    passwd* pw = getpwuid((uid_t)uid);
    return cache[uid] = pw ? pw->pw_name : std::to_string(uid);
}

void ProcessList::sample() {
    std::string stat = readLine("/proc/stat");
    unsigned long long total = 0;
    {
        const char* p = stat.c_str() + 3;
        char* end;
        while (true) {
            unsigned long long v = strtoull(p, &end, 10);
            if (end == p) break;
            total += v;
            p = end;
        }
    }
    long cores = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
    unsigned long long dt = lastTotal_ ? total - lastTotal_ : 0;

    std::vector<Process> out;
    std::unordered_map<int, unsigned long long> ticks;
    DIR* d = opendir("/proc");
    while (d) {
        dirent* de = readdir(d);
        if (!de) break;
        int pid = atoi(de->d_name);
        if (pid <= 0) continue;
        std::string base = strprintf("/proc/%d", pid);
        std::string st = readFile(base + "/stat").value_or("");
        size_t open = st.find('('), close = st.rfind(')');
        if (open == std::string::npos || close == std::string::npos) continue;
        Process p;
        p.pid = pid;
        p.name = st.substr(open + 1, close - open - 1);
        std::vector<std::string> f = split(st.substr(close + 2), ' ');
        if (f.size() < 22) continue;
        p.state = f[0][0];
        unsigned long long t = strtoull(f[11].c_str(), nullptr, 10) + strtoull(f[12].c_str(), nullptr, 10);
        ticks[pid] = t;
        auto prev = lastTicks_.find(pid);
        if (dt > 0 && prev != lastTicks_.end()) p.cpu = (float)((t - prev->second) * 100.0 * cores / dt);
        p.rssKb = atol(f[21].c_str()) * (sysconf(_SC_PAGESIZE) / 1024);
        std::string status = readFile(base + "/status").value_or("");
        size_t uidAt = status.find("\nUid:");
        if (uidAt != std::string::npos) p.uid = atoi(status.c_str() + uidAt + 5);
        p.user = userName(p.uid);
        std::string cmd = readFile(base + "/cmdline").value_or("");
        std::replace(cmd.begin(), cmd.end(), '\0', ' ');
        p.cmdline = trim(cmd);
        if (p.cmdline.empty()) p.cmdline = "[" + p.name + "]";
        out.push_back(std::move(p));
    }
    if (d) closedir(d);

    cpuTotal = 0;
    for (const auto& p : out) cpuTotal += p.cpu;
    std::sort(out.begin(), out.end(), [](const Process& a, const Process& b) {
        if (a.cpu != b.cpu) return a.cpu > b.cpu;
        return a.rssKb > b.rssKb;
    });
    processes = std::move(out);
    lastTicks_ = std::move(ticks);
    lastTotal_ = total;
}
