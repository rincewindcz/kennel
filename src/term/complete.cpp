#include "complete.h"

#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

namespace {

constexpr size_t kHistoryMax = 500, kMax = 40;

const char* const kBuiltins[] = {"alias", "bg",   "cd",     "command", "echo",  "eval",   "exec",  "exit",    "export",
                                 "fg",    "hash", "history", "jobs",   "kill",  "local", "printf", "pwd",   "read",
                                 "return", "set", "shift",  "source",  "test",  "trap",  "type",   "ulimit", "umask",
                                 "unalias", "unset", "wait"};

std::string firstWord(const std::string& line) {
    size_t start = line.find_first_not_of(' ');
    if (start == std::string::npos) return {};
    size_t end = line.find(' ', start);
    return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Backslash escapes for the characters the shell would split or expand.
std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::string_view(" \t'\"\\$`&|;<>()*?[]#!{}").find(c) != std::string_view::npos) out += '\\';
        out += c;
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) ++i;
        out += s[i];
    }
    return out;
}

}  // namespace

void Completer::load(const std::string& historyPath) {
    historyPath_ = historyPath;
    for (const auto& l : split(readFile(historyPath).value_or(""), '\n'))
        if (!trim(l).empty()) {
            history_.erase(std::remove(history_.begin(), history_.end(), l), history_.end());
            history_.push_back(l);
        }
    if (history_.size() > kHistoryMax) history_.erase(history_.begin(), history_.end() - kHistoryMax);
    for (const auto& l : history_) ++uses_[firstWord(l)];
    scanPath();
}

void Completer::scanPath() {
    const char* path = getenv("PATH");
    std::vector<std::string> out(std::begin(kBuiltins), std::end(kBuiltins));
    for (const auto& dir : split(path ? path : "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", ':')) {
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        while (dirent* de = readdir(d)) {
            if (de->d_name[0] == '.') continue;
            std::string full = dir + "/" + de->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(full.c_str(), X_OK) == 0)
                out.push_back(de->d_name);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    commands_ = std::move(out);
}

void Completer::remember(const std::string& line) {
    std::string l = trim(line);
    if (l.empty()) return;
    history_.erase(std::remove(history_.begin(), history_.end(), l), history_.end());
    history_.push_back(l);
    ++uses_[firstWord(l)];
    if (history_.size() > kHistoryMax) history_.erase(history_.begin());
    std::string text;
    for (const auto& h : history_) text += h + "\n";
    writeFileAtomic(historyPath_, text);
}

std::vector<Suggestion> Completer::commands(const std::string& prefix) const {
    std::vector<const std::string*> found;
    auto it = std::lower_bound(commands_.begin(), commands_.end(), prefix);
    for (; it != commands_.end() && it->compare(0, prefix.size(), prefix) == 0; ++it)
        if (it->size() > prefix.size()) found.push_back(&*it);
    auto uses = [&](const std::string* c) {
        auto u = uses_.find(*c);
        return u == uses_.end() ? 0 : u->second;
    };
    std::stable_sort(found.begin(), found.end(), [&](const std::string* a, const std::string* b) {
        int ua = uses(a), ub = uses(b);
        if (ua != ub) return ua > ub;
        return a->size() < b->size();
    });
    std::vector<Suggestion> out;
    for (const auto* c : found) {
        if (out.size() >= kMax) break;
        out.push_back({*c, c->substr(prefix.size()) + " "});
    }
    return out;
}

std::vector<Suggestion> Completer::files(const std::string& word, const std::string& cwd, bool dirsOnly) const {
    std::string w = unescape(word);
    size_t slash = w.rfind('/');
    std::string dirPart = slash == std::string::npos ? "" : w.substr(0, slash + 1);
    std::string prefix = slash == std::string::npos ? w : w.substr(slash + 1);
    std::string dir = dirPart;
    if (dir.rfind("~/", 0) == 0 || dir == "~") {
        const char* home = getenv("HOME");
        dir = std::string(home ? home : "") + dir.substr(1);
    }
    if (dir.empty()) dir = cwd;
    else if (dir[0] != '/') dir = cwd + "/" + dir;

    struct Found {
        std::string name;
        bool dir;
    };
    std::vector<Found> found;
    DIR* d = opendir(dir.c_str());
    if (!d) return {};
    while (dirent* de = readdir(d)) {
        std::string name = de->d_name;
        if (name == "." || name == "..") continue;
        if (name[0] == '.' && (prefix.empty() || prefix[0] != '.')) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        bool isDir = de->d_type == DT_DIR;
        if (de->d_type == DT_LNK || de->d_type == DT_UNKNOWN) {
            struct stat st;
            isDir = stat((dir + "/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }
        if (dirsOnly && !isDir) continue;
        found.push_back({name, isDir});
    }
    closedir(d);
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        if (a.dir != b.dir) return a.dir;
        return lower(a.name) < lower(b.name);
    });
    std::vector<Suggestion> out;
    for (const auto& f : found) {
        if (out.size() >= kMax) break;
        std::string rest = escape(f.name.substr(prefix.size())) + (f.dir ? "/" : " ");
        out.push_back({f.name + (f.dir ? "/" : ""), rest});
    }
    return out;
}

std::vector<Suggestion> Completer::suggest(const std::string& line, const std::string& cwd) const {
    std::vector<Suggestion> out;
    // Earlier commands: with nothing typed the most recent ones, else those that continue the line.
    for (auto it = history_.rbegin(); it != history_.rend() && out.size() < (line.empty() ? kMax : 3); ++it)
        if (it->size() > line.size() && it->compare(0, line.size(), line) == 0)
            out.push_back({*it, it->substr(line.size()), true});
    if (line.empty()) return out;

    size_t space = std::string::npos;
    for (size_t i = line.size(); i-- > 0;)
        if (line[i] == ' ' && (i == 0 || line[i - 1] != '\\')) {
            space = i;
            break;
        }
    std::string word = space == std::string::npos ? line : line.substr(space + 1);
    bool first = space == std::string::npos || trim(line.substr(0, space)).empty();
    if (first && word.find('/') == std::string::npos) {
        if (!word.empty())
            for (auto& s : commands(word)) out.push_back(std::move(s));
    } else {
        bool cd = !first && firstWord(line) == "cd";
        for (auto& s : files(word, cwd, cd)) out.push_back(std::move(s));
    }
    // A history line can repeat a completion: keep the first.
    std::vector<Suggestion> unique;
    for (auto& s : out)
        if (std::none_of(unique.begin(), unique.end(), [&](const Suggestion& u) { return u.append == s.append; }))
            unique.push_back(std::move(s));
    return unique;
}
