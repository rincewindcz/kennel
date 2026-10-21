#include "positions.h"

#include <cstdlib>

#include "paths.h"
#include "util.h"

namespace {

std::string file() { return paths.data + "/read/positions.tsv"; }

}  // namespace

uint64_t loadPosition(const std::string& book) {
    std::string text = readFile(file()).value_or("");
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        size_t tab = line.find('\t');
        if (tab != std::string::npos && line.substr(0, tab) == book) return strtoull(line.c_str() + tab + 1, nullptr, 10);
    }
    return 0;
}

void savePosition(const std::string& book, uint64_t offset, uint64_t size) {
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
    out += strprintf("%s\t%llu\t%llu\t%.0f\n", book.c_str(), (unsigned long long)offset, (unsigned long long)size, wallSeconds());
    writeFileAtomic(file(), out);
}
