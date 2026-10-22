#include "positions.h"

#include <cstdlib>

#include "paths.h"
#include "util.h"

namespace {

std::string file() { return paths.data + "/read/positions.tsv"; }

}  // namespace

ReadPosition loadPosition(const std::string& book) {
    std::string text = readFile(file()).value_or("");
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        auto fields = split(line, '\t');
        if (fields.size() < 2 || fields[0] != book) continue;
        ReadPosition p;
        p.offset = strtoull(fields[1].c_str(), nullptr, 10);
        if (fields.size() > 4) p.seconds = strtod(fields[4].c_str(), nullptr);
        return p;
    }
    return {};
}

void savePosition(const std::string& book, const ReadPosition& position, uint64_t size) {
    makeDirs(paths.data + "/read");
    std::string text = readFile(file()).value_or("");
    std::string out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        size_t tab = line.find('\t');
        if (line.empty() || (tab != std::string::npos && line.substr(0, tab) == book)) continue;
        out += line + "\n";
    }
    out += strprintf("%s\t%llu\t%llu\t%.0f\t%.0f\n", book.c_str(), (unsigned long long)position.offset, (unsigned long long)size,
                     wallSeconds(), position.seconds);
    writeFileAtomic(file(), out);
}
