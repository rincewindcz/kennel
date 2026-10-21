#include "stats.h"

#include <algorithm>
#include <cstdlib>
#include <map>

#include "util.h"

namespace {

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return out;
}

template <typename F>
void eachLine(const std::string& text, F f) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        if (end > pos) f(text.substr(pos, end - pos));
        pos = end + 1;
    }
}

std::string sessionLine(const Session& s) {
    return strprintf("%ld\t%d\t%.0f\t%.2f\t%.2f\t%d\t%s%s\t", s.seq, s.boot, s.startWall, s.startUptime, s.duration, s.rc,
                     s.killed ? "k" : "", s.unknown ? "u" : "") + s.id + "\n";
}

}  // namespace

void Stats::load(const std::string& dataDir) {
    dir_ = dataDir;
    sessions_.clear();
    eachLine(readFile(dir_ + "/sessions.tsv").value_or(""), [&](const std::string& line) {
        auto f = fields(line);
        if (f.size() < 8) return;
        Session s;
        s.seq = atol(f[0].c_str());
        s.boot = atoi(f[1].c_str());
        s.startWall = atof(f[2].c_str());
        s.startUptime = atof(f[3].c_str());
        s.duration = atof(f[4].c_str());
        s.rc = atoi(f[5].c_str());
        s.killed = f[6].find('k') != std::string::npos;
        s.unknown = f[6].find('u') != std::string::npos;
        s.id = f[7];
        sessions_.push_back(std::move(s));
    });

    favorites_.clear();
    favoriteOrder_.clear();
    eachLine(readFile(dir_ + "/favorites").value_or(""), [&](const std::string& line) {
        if (favorites_.insert(line).second) favoriteOrder_.push_back(line);
    });

    bootId_ = bootId();
    std::string bootFile = readLine(dir_ + "/boot");
    auto parts = fields(bootFile);
    boot_ = parts.size() == 2 ? atoi(parts[0].c_str()) : 0;
    if (parts.size() != 2 || parts[1] != bootId_) {
        ++boot_;
        writeFileAtomic(dir_ + "/boot", strprintf("%d\t%s\n", boot_, bootId_.c_str()));
    }
    ingest();
}

void Stats::ingest() {
    std::string text = readFile(journal()).value_or("");
    newest_ = nullptr;
    newestSeq_ = -1;
    if (text.empty()) {
        rebuild();
        return;
    }

    struct Start {
        std::string boot;
        double uptime = 0, wall = 0;
        std::string id;
        bool ended = false;
        double endUptime = 0;
        int rc = 0;
    };
    std::map<std::string, Start> starts;
    std::vector<std::string> order;
    eachLine(text, [&](const std::string& line) {
        auto f = fields(line);
        if (f.size() >= 6 && f[0] == "S") {
            Start& s = starts[f[1]];
            s.boot = f[2];
            s.uptime = atof(f[3].c_str());
            s.wall = atof(f[4].c_str());
            s.id = f[5];
            order.push_back(f[1]);
        } else if (f.size() >= 5 && f[0] == "E") {
            auto it = starts.find(f[1]);
            if (it == starts.end()) return;
            it->second.ended = true;
            it->second.endUptime = atof(f[2].c_str());
            it->second.rc = atoi(f[4].c_str());
        }
    });

    long seq = sessions_.empty() ? 0 : sessions_.back().seq;
    double now = uptimeSeconds();
    for (const auto& token : order) {
        const Start& st = starts[token];
        Session s;
        s.seq = ++seq;
        s.id = st.id;
        s.startWall = st.wall;
        s.startUptime = st.uptime;
        bool sameBoot = st.boot == bootId_;
        s.boot = sameBoot ? boot_ : std::max(boot_ - 1, 0);
        if (st.ended) {
            s.duration = std::max(0.0, st.endUptime - st.uptime);
            s.rc = st.rc;
        } else if (sameBoot) {
            s.duration = std::max(0.0, now - st.uptime);
            s.killed = true;
            s.rc = 137;
        } else {
            s.unknown = true;
            s.rc = -1;
        }
        appendFile(dir_ + "/sessions.tsv", sessionLine(s));
        sessions_.push_back(std::move(s));
        newestSeq_ = seq;
    }
    writeFile(journal(), "");
    rebuild();
}

void Stats::rebuild() {
    stats_.clear();
    newest_ = nullptr;
    for (const auto& s : sessions_) {
        EntryStats& e = stats_[s.id];
        ++e.launches;
        e.total += s.duration;
        if (e.firstWall == 0 && wallClockValid(s.startWall)) e.firstWall = s.startWall;
        e.last = &s;
        if (s.seq == newestSeq_) newest_ = &s;
    }
}

const EntryStats* Stats::get(const std::string& id) const {
    auto it = stats_.find(id);
    return it == stats_.end() ? nullptr : &it->second;
}

std::vector<std::string> Stats::recent(size_t max) const {
    std::vector<std::string> out;
    for (auto it = sessions_.rbegin(); it != sessions_.rend() && out.size() < max; ++it)
        if (std::find(out.begin(), out.end(), it->id) == out.end()) out.push_back(it->id);
    return out;
}

std::string Stats::describeLast(const Session& s) const {
    double nowWall = wallSeconds();
    double end = s.startWall + s.duration;
    if (wallClockValid(s.startWall) && wallClockValid(nowWall)) return formatAgo(nowWall - end);
    if (s.boot == boot_ && !s.unknown) return formatAgo(uptimeSeconds() - (s.startUptime + s.duration));
    int boots = boot_ - s.boot;
    if (boots <= 1) return "previous boot";
    return strprintf("%d boots ago", boots);
}

void Stats::toggleFavorite(const std::string& id) {
    if (favorites_.erase(id)) favoriteOrder_.erase(std::find(favoriteOrder_.begin(), favoriteOrder_.end(), id));
    else {
        favorites_.insert(id);
        favoriteOrder_.push_back(id);
    }
    std::string out;
    for (const auto& f : favoriteOrder_) out += f + "\n";
    writeFileAtomic(dir_ + "/favorites", out);
}

void Stats::reset(const std::string& id) {
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(), [&](const Session& s) { return s.id == id; }),
                    sessions_.end());
    saveSessions();
    newestSeq_ = -1;
    rebuild();
}

void Stats::saveSessions() {
    std::string out;
    for (const auto& s : sessions_) out += sessionLine(s);
    writeFileAtomic(dir_ + "/sessions.tsv", out);
}
