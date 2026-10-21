#pragma once

#include <cstdint>
#include <string>

// Reading positions in ~/.local/share/kennel/read/positions.tsv: path, offset, size, wall time.
uint64_t loadPosition(const std::string& book);
void savePosition(const std::string& book, uint64_t offset, uint64_t size);
