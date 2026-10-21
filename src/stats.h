#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Play statistics. kennel-run appends start and end records to the journal; on start kennel folds the
// journal into sessions.tsv, inferring the end of sessions whose wrapper was killed.
struct Session {
    long seq = 0;
    int boot = 0;
    double startWall = 0;
    double startUptime = 0;
    double duration = 0;
    int rc = 0;
    bool killed = false;    // ended without an end record (kill-application, crash of the wrapper)
    bool unknown = false;   // ended in an earlier boot: duration unknown
    std::string id;
};

struct EntryStats {
    int launches = 0;
    double total = 0;
    double firstWall = 0;
    const Session* last = nullptr;
};

class Stats {
public:
    void load(const std::string& dataDir);
    void ingest();

    const EntryStats* get(const std::string& id) const;
    const std::vector<Session>& sessions() const { return sessions_; }
    std::vector<std::string> recent(size_t max) const;
    std::string describeLast(const Session& s) const;
    const Session* newSession() const { return newest_; }

    bool isFavorite(const std::string& id) const { return favorites_.count(id) > 0; }
    void toggleFavorite(const std::string& id);
    const std::vector<std::string>& favorites() const { return favoriteOrder_; }

    void reset(const std::string& id);
    std::string journal() const { return dir_ + "/journal"; }
    int boot() const { return boot_; }

private:
    void rebuild();
    void saveSessions();

    std::string dir_;
    std::string bootId_;
    int boot_ = 0;
    std::vector<Session> sessions_;
    std::unordered_map<std::string, EntryStats> stats_;
    std::unordered_set<std::string> favorites_;
    std::vector<std::string> favoriteOrder_;
    const Session* newest_ = nullptr;
    long newestSeq_ = -1;
};
