#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Read-only ZIP archive: stored and deflated entries, central directory driven.
class ZipArchive {
public:
    struct Entry {
        std::string name;
        uint16_t method = 0;
        uint32_t compressed = 0, size = 0, localOffset = 0;
    };

    bool open(const std::string& path, std::string& error);
    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(const std::string& name) const;
    std::optional<std::string> read(const std::string& name) const;
    std::optional<std::string> read(const Entry& e) const;

private:
    std::string data_;
    std::vector<Entry> entries_;
};
