#pragma once

#include <cstdint>
#include <string>

// Reading positions in ~/.local/share/kennel/read/positions.tsv: path, offset, size, wall time and
// active reading time in seconds.
struct ReadPosition {
    uint64_t offset = 0;
    double seconds = 0;
};

ReadPosition loadPosition(const std::string& book);
void savePosition(const std::string& book, const ReadPosition& position, uint64_t size);
