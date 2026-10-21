#include "backup.h"

#include <ctime>

#include "app.h"
#include "ini.h"
#include "paths.h"
#include "platform.h"
#include "system.h"
#include "util.h"

namespace {

bool run(const std::string& command, std::string& error) {
    int status = 0;
    std::string out = trim(runCapture(command, &status));
    if (status == 0) return true;
    error = out.empty() ? strprintf("exit status %d", status) : out;
    logf("backup: %s: %s", command.c_str(), error.c_str());
    return false;
}

}  // namespace

std::string backupDir() {
    std::string base = !platform.externalMount.empty() && sys.externalMounted ? platform.externalMount : paths.home;
    return base + "/kennel-backup";
}

std::string describeBackup(const std::string& dir) {
    IniFile ini;
    if (!loadIni(dir + "/backup.ini", ini)) return {};
    std::string when;
    double wall = atof(ini.get("backup", "time").c_str());
    if (wallClockValid(wall) && wallClockValid(wallSeconds())) {
        time_t t = (time_t)wall;
        tm lt{};
        localtime_r(&t, &lt);
        char buf[32];
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &lt);
        when = buf;
    } else if (ini.get("backup", "boot") == bootId()) {
        when = formatAgo(uptimeSeconds() - atof(ini.get("backup", "uptime").c_str()));
    } else {
        when = "an earlier boot";
    }
    return when + ", " + ini.get("backup", "sessions", "0") + " sessions";
}

bool makeBackup(const std::string& dir, std::string& error) {
    std::string tmp = dir + ".tmp", q = shellQuote(tmp);
    std::string cmd = "rm -rf " + q + " && mkdir -p " + q + " && cp -r " + shellQuote(paths.config) + " " +
                      shellQuote(tmp + "/config") + " && cp -r " + shellQuote(paths.data) + " " + shellQuote(tmp + "/data");
    if (!run(cmd, error)) return false;
    std::string info = strprintf("[backup]\nversion=%s\ntime=%.0f\nboot=%s\nuptime=%.0f\nsessions=%zu\n", KENNEL_VERSION,
                                 wallSeconds(), bootId().c_str(), uptimeSeconds(), app.stats.sessions().size());
    if (!writeFile(tmp + "/backup.ini", info)) {
        error = "cannot write " + tmp + "/backup.ini";
        return false;
    }
    if (!run("rm -rf " + shellQuote(dir) + " && mv " + q + " " + shellQuote(dir) + " && sync", error)) return false;
    logf("backup written to %s", dir.c_str());
    return true;
}

bool restoreBackup(const std::string& dir, std::string& error) {
    if (!isDir(dir + "/config") || !isDir(dir + "/data")) {
        error = "no backup in " + dir;
        return false;
    }
    std::string cmd;
    for (const auto& [from, to] : {std::pair{dir + "/config", paths.config}, std::pair{dir + "/data", paths.data}})
        cmd += (cmd.empty() ? "" : " && ") + std::string("rm -rf ") + shellQuote(to) + " && cp -r " + shellQuote(from) + " " +
               shellQuote(to);
    if (!run(cmd + " && sync", error)) return false;
    logf("restored backup from %s", dir.c_str());
    return true;
}
