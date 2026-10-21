#include "zip.h"

#include <zlib.h>

#include "util.h"

namespace {

constexpr uint32_t kMaxEntry = 64u << 20;

uint16_t u16(const std::string& d, size_t at) { return (uint8_t)d[at] | (uint8_t)d[at + 1] << 8; }
uint32_t u32(const std::string& d, size_t at) { return u16(d, at) | (uint32_t)u16(d, at + 2) << 16; }

}  // namespace

bool ZipArchive::open(const std::string& path, std::string& error) {
    auto data = readFile(path);
    if (!data) {
        error = "cannot read " + path;
        return false;
    }
    data_ = std::move(*data);
    entries_.clear();
    const std::string& d = data_;
    if (d.size() < 22) {
        error = "not a zip archive";
        return false;
    }
    size_t eocd = std::string::npos;
    size_t lowest = d.size() > 65557 ? d.size() - 65557 : 0;
    for (size_t i = d.size() - 22 + 1; i-- > lowest;)
        if (u32(d, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd == std::string::npos) {
        error = "zip directory not found";
        return false;
    }
    uint16_t count = u16(d, eocd + 10);
    size_t pos = u32(d, eocd + 16);
    for (uint16_t i = 0; i < count; ++i) {
        if (pos + 46 > d.size() || u32(d, pos) != 0x02014b50) break;
        Entry e;
        e.method = u16(d, pos + 10);
        e.compressed = u32(d, pos + 20);
        e.size = u32(d, pos + 24);
        uint16_t nameLen = u16(d, pos + 28), extraLen = u16(d, pos + 30), commentLen = u16(d, pos + 32);
        e.localOffset = u32(d, pos + 42);
        if (pos + 46 + nameLen > d.size()) break;
        e.name = d.substr(pos + 46, nameLen);
        entries_.push_back(std::move(e));
        pos += 46 + (size_t)nameLen + extraLen + commentLen;
    }
    if (entries_.empty()) {
        error = "empty or damaged zip archive";
        return false;
    }
    return true;
}

const ZipArchive::Entry* ZipArchive::find(const std::string& name) const {
    for (const auto& e : entries_)
        if (e.name == name) return &e;
    for (const auto& e : entries_)
        if (iequals(e.name, name)) return &e;
    return nullptr;
}

std::optional<std::string> ZipArchive::read(const std::string& name) const {
    const Entry* e = find(name);
    if (!e) return std::nullopt;
    return read(*e);
}

std::optional<std::string> ZipArchive::read(const Entry& e) const {
    const std::string& d = data_;
    size_t at = e.localOffset;
    if (at + 30 > d.size() || u32(d, at) != 0x04034b50) return std::nullopt;
    size_t start = at + 30 + u16(d, at + 26) + u16(d, at + 28);
    if (start > d.size() || e.compressed > d.size() - start || e.size > kMaxEntry) return std::nullopt;
    if (e.method == 0) return d.substr(start, std::min(e.compressed, e.size));
    if (e.method != 8) return std::nullopt;

    std::string out(e.size, '\0');
    z_stream z{};
    if (inflateInit2(&z, -15) != Z_OK) return std::nullopt;
    z.next_in = (Bytef*)d.data() + start;
    z.avail_in = e.compressed;
    z.next_out = (Bytef*)out.data();
    z.avail_out = (uInt)out.size();
    int rc = inflate(&z, Z_FINISH);
    size_t produced = out.size() - z.avail_out;
    inflateEnd(&z);
    if (rc != Z_STREAM_END && rc != Z_OK && rc != Z_BUF_ERROR) return std::nullopt;
    out.resize(produced);
    return out;
}
