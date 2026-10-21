#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Suggestions for the line being typed: earlier commands that start with it, then completions of
// its last word (a command from PATH for the first word, a file or folder after that).
struct Suggestion {
    std::string label;   // shown in the strip
    std::string append;  // sent after what is typed
    bool history = false;
};

class Completer {
public:
    void load(const std::string& historyPath);
    void remember(const std::string& line);
    std::vector<Suggestion> suggest(const std::string& line, const std::string& cwd) const;

private:
    void scanPath();
    std::vector<Suggestion> commands(const std::string& prefix) const;
    std::vector<Suggestion> files(const std::string& word, const std::string& cwd, bool dirsOnly) const;

    std::string historyPath_;
    std::vector<std::string> history_;  // oldest first, unique
    std::vector<std::string> commands_;
    std::unordered_map<std::string, int> uses_;  // first words of the history
};
