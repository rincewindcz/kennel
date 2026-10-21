#include <algorithm>

#include "book.h"
#include "markup.h"
#include "util.h"

namespace {

uint16_t be16(const std::string& d, size_t at) {
    return at + 2 <= d.size() ? (uint16_t)((uint8_t)d[at] << 8 | (uint8_t)d[at + 1]) : 0;
}
uint32_t be32(const std::string& d, size_t at) {
    return at + 4 <= d.size() ? (uint32_t)be16(d, at) << 16 | be16(d, at + 2) : 0;
}

std::string palmDocDecompress(std::string_view in) {
    std::string out;
    out.reserve(in.size() * 2);
    for (size_t i = 0; i < in.size();) {
        uint8_t c = in[i++];
        if (c >= 1 && c <= 8) {
            size_t n = std::min<size_t>(c, in.size() - i);
            out.append(in.substr(i, n));
            i += n;
        } else if (c < 0x80) {
            out += (char)c;
        } else if (c >= 0xC0) {
            out += ' ';
            out += (char)(c ^ 0x80);
        } else {
            if (i >= in.size()) break;
            uint16_t v = (uint16_t)((c << 8 | (uint8_t)in[i++]) & 0x3FFF);
            size_t dist = v >> 3, len = (v & 7) + 3;
            if (dist == 0 || dist > out.size()) continue;
            size_t from = out.size() - dist;
            for (size_t k = 0; k < len; ++k) out += out[from + k];
        }
    }
    return out;
}

// Size of one trailing entry, read as a variable-width integer backwards from the record end.
size_t trailingEntry(const std::string& r, size_t end) {
    size_t value = 0, shift = 0;
    while (end > 0) {
        uint8_t v = r[--end];
        value |= (size_t)(v & 0x7F) << shift;
        shift += 7;
        if ((v & 0x80) || shift >= 28) break;
    }
    return value;
}

size_t trailingSize(const std::string& r, uint16_t flags) {
    size_t num = 0;
    for (uint16_t f = flags >> 1; f; f >>= 1)
        if (f & 1) {
            if (num >= r.size()) return r.size();
            num += trailingEntry(r, r.size() - num);
        }
    if ((flags & 1) && num < r.size()) num += ((uint8_t)r[r.size() - num - 1] & 0x3) + 1;
    return std::min(num, r.size());
}

std::string plainName(std::string_view s) {
    std::string out(s.substr(0, s.find('\0')));
    return trim(replaceAll(out, "_", " "));
}

// Declared cp1252, or text that is not valid UTF-8, is decoded as cp1252.
std::string toUtf8(std::string_view s, bool cp1252) {
    if (!cp1252 && markup::validUtf8(s)) return std::string(s);
    return markup::cp1252ToUtf8(s);
}

}  // namespace

// Palm database ebooks: PalmDOC (TEXtREAd) and unencrypted MOBI (BOOKMOBI) without HUFF/CDIC.
bool loadPdb(const std::string& path, Book& book, std::string& error) {
    auto file = readFile(path);
    if (!file) {
        error = "cannot read " + path;
        return false;
    }
    const std::string& d = *file;
    if (d.size() < 78) {
        error = "not a Palm database";
        return false;
    }
    std::string type = d.substr(60, 8);
    bool mobi = type == "BOOKMOBI";
    if (!mobi && type != "TEXtREAd") {
        error = "unsupported Palm database type " + type;
        return false;
    }
    uint16_t count = be16(d, 76);
    std::vector<size_t> offsets;
    for (uint16_t i = 0; i < count; ++i) {
        size_t off = be32(d, 78 + (size_t)i * 8);
        if (78 + (size_t)i * 8 + 8 > d.size() || off > d.size()) break;
        offsets.push_back(off);
    }
    auto record = [&](size_t i) -> std::string {
        if (i >= offsets.size()) return {};
        size_t end = i + 1 < offsets.size() ? offsets[i + 1] : d.size();
        if (end < offsets[i]) return {};
        return d.substr(offsets[i], end - offsets[i]);
    };
    std::string r0 = record(0);
    if (r0.size() < 16) {
        error = "damaged Palm database";
        return false;
    }
    uint16_t compression = be16(r0, 0);
    uint32_t textLength = be32(r0, 4);
    uint16_t textRecords = be16(r0, 8);
    if (compression == 17480) {
        error = "HUFF/CDIC compressed MOBI is not supported, convert it to EPUB";
        return false;
    }
    if (compression != 1 && compression != 2) {
        error = strprintf("unknown compression %u", compression);
        return false;
    }

    uint16_t extraFlags = 0;
    bool cp1252 = false;
    book.title = plainName(d.substr(0, 32));
    if (mobi) {
        if (be16(r0, 12) != 0) {
            error = "DRM protected book";
            return false;
        }
        if (r0.compare(16, 4, "MOBI") == 0) {
            uint32_t headerLen = be32(r0, 20);
            cp1252 = be32(r0, 28) == 1252;
            uint32_t nameOff = be32(r0, 84), nameLen = be32(r0, 88);
            if (nameOff < r0.size() && nameLen <= r0.size() - nameOff) book.title = toUtf8(r0.substr(nameOff, nameLen), cp1252);
            if (headerLen >= 0xE4) extraFlags = be16(r0, 0xF2);
            if (be32(r0, 0x80) & 0x40) {
                size_t exth = 16 + (size_t)headerLen;
                if (exth + 12 <= r0.size() && r0.compare(exth, 4, "EXTH") == 0) {
                    uint32_t n = be32(r0, exth + 8);
                    size_t p = exth + 12;
                    for (uint32_t i = 0; i < n && p + 8 <= r0.size(); ++i) {
                        uint32_t rt = be32(r0, p), rl = be32(r0, p + 4);
                        if (rl < 8 || rl > r0.size() - p) break;
                        std::string value = toUtf8(r0.substr(p + 8, rl - 8), cp1252);
                        if (rt == 100 && book.author.empty()) book.author = value;
                        if (rt == 503) book.title = value;
                        p += rl;
                    }
                }
            }
        }
    }

    std::string text;
    for (size_t i = 1; i <= textRecords && i < offsets.size(); ++i) {
        std::string r = record(i);
        if (mobi && extraFlags) r.resize(r.size() - trailingSize(r, extraFlags));
        text += compression == 2 ? palmDocDecompress(r) : r;
        if (text.size() >= textLength && textLength) break;
    }
    if (textLength && text.size() > textLength) text.resize(textLength);
    text = toUtf8(text, cp1252);

    if (mobi) {
        markup::htmlToParagraphs(text, book);
        book.format = "MOBI";
        return true;
    }

    size_t blank = 0, nonBlank = 0;
    for (size_t pos = 0; pos <= text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        (trim(std::string_view(text).substr(pos, end - pos)).empty() ? blank : nonBlank)++;
        pos = end + 1;
    }
    bool blocks = blank > 0 && nonBlank > blank * 2;
    Paragraph cur;
    auto flush = [&] {
        if (!trim(cur.text).empty()) book.paragraphs.push_back(std::move(cur));
        cur = Paragraph{};
    };
    for (size_t pos = 0; pos <= text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string_view line = std::string_view(text).substr(pos, end - pos);
        pos = end + 1;
        if (trim(line).empty()) {
            flush();
            continue;
        }
        if (!cur.text.empty()) cur.text += ' ';
        cur.text.append(line);
        if (!blocks) flush();
    }
    flush();
    book.format = "PalmDOC";
    return true;
}
