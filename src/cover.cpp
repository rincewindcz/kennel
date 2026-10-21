#include "cover.h"

#include <vector>

#include "tokenizer.h"
#include "util.h"
#include "zip.h"

namespace {

std::string extension(const std::string& path) {
    size_t dot = path.rfind('.');
    size_t slash = path.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return lower(path.substr(dot + 1));
}

std::string percentDecode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            out += (char)std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// href relative to the directory of `base` (a path inside the archive), without fragment.
std::string resolve(const std::string& base, std::string href) {
    size_t hash = href.find('#');
    if (hash != std::string::npos) href.erase(hash);
    href = percentDecode(href);
    if (href.empty()) return {};
    std::vector<std::string> parts;
    if (href[0] != '/') {
        size_t slash = base.rfind('/');
        if (slash != std::string::npos)
            for (auto& p : split(base.substr(0, slash), '/')) parts.push_back(p);
    }
    for (auto& p : split(href, '/')) {
        if (p == ".") continue;
        if (p == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(p);
    }
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

bool isImageType(const std::string& mediaType) { return lower(mediaType).rfind("image/", 0) == 0; }

bool isImagePath(const std::string& path) {
    std::string e = extension(path);
    return e == "jpg" || e == "jpeg" || e == "png" || e == "gif" || e == "bmp";
}

// First <img src> or <image xlink:href> of an XHTML page.
std::string firstImage(const std::string& page) {
    markup::Tokenizer tz(page);
    markup::Token t;
    while (tz.next(t)) {
        if (t.type == markup::Token::Type::Text || t.type == markup::Token::Type::Close) continue;
        std::string n = t.local();
        if (n == "img") {
            if (const std::string* src = t.attr("src")) return *src;
        } else if (n == "image") {
            for (const auto& [k, v] : t.attrs)
                if (k == "href" || (k.size() > 5 && k.compare(k.size() - 5, 5, ":href") == 0)) return v;
        }
    }
    return {};
}

bool epubCover(const std::string& path, std::string& out) {
    ZipArchive zip;
    std::string error;
    if (!zip.open(path, error)) return false;
    auto container = zip.read("META-INF/container.xml");
    if (!container) return false;
    std::string opfPath;
    {
        markup::Tokenizer tz(*container);
        markup::Token t;
        while (tz.next(t))
            if (t.type != markup::Token::Type::Close && t.local() == "rootfile")
                if (const std::string* p = t.attr("full-path")) {
                    opfPath = percentDecode(*p);
                    break;
                }
    }
    auto opf = opfPath.empty() ? std::nullopt : zip.read(opfPath);
    if (!opf) return false;

    struct Item {
        std::string id, href, type, properties;
    };
    std::vector<Item> items;
    std::string metaCover, guideCover;
    markup::Tokenizer tz(*opf);
    markup::Token t;
    while (tz.next(t)) {
        if (t.type == markup::Token::Type::Text || t.type == markup::Token::Type::Close) continue;
        std::string n = t.local();
        auto attr = [&](const char* k) {
            const std::string* v = t.attr(k);
            return v ? *v : std::string();
        };
        if (n == "item") items.push_back({attr("id"), resolve(opfPath, attr("href")), attr("media-type"), attr("properties")});
        else if (n == "meta" && lower(attr("name")) == "cover") metaCover = attr("content");
        else if (n == "reference" && lower(attr("type")) == "cover") guideCover = resolve(opfPath, attr("href"));
    }

    auto readImage = [&](const std::string& p) {
        if (p.empty()) return false;
        auto data = zip.read(p);
        if (!data || data->empty()) return false;
        out = std::move(*data);
        return true;
    };
    auto fromPage = [&](const std::string& page) {
        auto html = zip.read(page);
        if (!html) return false;
        std::string img = firstImage(*html);
        return !img.empty() && readImage(resolve(page, img));
    };

    for (const auto& i : items)
        for (const auto& p : split(i.properties, ' '))
            if (p == "cover-image" && readImage(i.href)) return true;
    if (!metaCover.empty()) {
        for (const auto& i : items) {
            if (i.id != metaCover) continue;
            if ((isImageType(i.type) || isImagePath(i.href)) && readImage(i.href)) return true;
            if (!isImageType(i.type) && fromPage(i.href)) return true;
        }
        // Some books put the image path itself in the meta content.
        if (readImage(resolve(opfPath, metaCover))) return true;
    }
    if (!guideCover.empty()) {
        if (isImagePath(guideCover) ? readImage(guideCover) : fromPage(guideCover)) return true;
    }
    for (const auto& i : items)
        if (isImageType(i.type) && lower(i.id + " " + i.href).find("cover") != std::string::npos && readImage(i.href))
            return true;
    for (const auto& i : items)
        if (!isImageType(i.type) && lower(i.id + " " + i.href).find("cover") != std::string::npos && fromPage(i.href))
            return true;
    return false;
}

std::string base64Decode(std::string_view s) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') break;
        else continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((acc >> bits) & 0xff);
        }
    }
    return out;
}

bool fb2Cover(const std::string& path, std::string& out) {
    auto data = readFile(path);
    if (!data) return false;
    std::string id;
    markup::Tokenizer tz(*data);
    markup::Token t;
    bool inCover = false, inBinary = false;
    std::string binary;
    while (tz.next(t)) {
        std::string n = t.local();
        if (t.type == markup::Token::Type::Close) {
            if (n == "coverpage") inCover = false;
            if (n == "binary" && inBinary) break;
            continue;
        }
        if (t.type == markup::Token::Type::Text) {
            if (inBinary) binary += t.text;
            continue;
        }
        if (n == "coverpage") inCover = t.type == markup::Token::Type::Open;
        else if (n == "image" && inCover && id.empty()) {
            for (const auto& [k, v] : t.attrs)
                if ((k == "href" || (k.size() > 5 && k.compare(k.size() - 5, 5, ":href") == 0)) && !v.empty())
                    id = v[0] == '#' ? v.substr(1) : v;
        } else if (n == "binary" && !id.empty() && t.type == markup::Token::Type::Open) {
            const std::string* bid = t.attr("id");
            inBinary = bid && *bid == id;
        }
    }
    if (binary.empty()) return false;
    out = base64Decode(binary);
    return !out.empty();
}

uint32_t be32(const std::string& d, size_t at) {
    return (uint32_t)(uint8_t)d[at] << 24 | (uint32_t)(uint8_t)d[at + 1] << 16 | (uint32_t)(uint8_t)d[at + 2] << 8 |
           (uint8_t)d[at + 3];
}
uint16_t be16(const std::string& d, size_t at) { return (uint16_t)((uint8_t)d[at] << 8 | (uint8_t)d[at + 1]); }

bool mobiCover(const std::string& path, std::string& out) {
    auto file = readFile(path);
    if (!file || file->size() < 78) return false;
    const std::string& d = *file;
    uint16_t count = be16(d, 76);
    if (count == 0 || 78 + (size_t)count * 8 > d.size()) return false;
    auto recordStart = [&](size_t i) { return (size_t)be32(d, 78 + i * 8); };
    auto recordEnd = [&](size_t i) { return i + 1 < count ? recordStart(i + 1) : d.size(); };

    size_t r0 = recordStart(0), r0end = recordEnd(0);
    if (r0end > d.size() || r0 + 0x84 > r0end || d.compare(r0 + 16, 4, "MOBI") != 0) return false;
    uint32_t headerLength = be32(d, r0 + 20);
    uint32_t firstImage = be32(d, r0 + 0x6C);
    uint32_t exthFlags = be32(d, r0 + 0x80);
    if (!(exthFlags & 0x40)) return false;
    size_t exth = r0 + 16 + headerLength;
    if (exth + 12 > r0end || d.compare(exth, 4, "EXTH") != 0) return false;
    uint32_t exthCount = be32(d, exth + 8);
    size_t pos = exth + 12;
    int64_t cover = -1, thumb = -1;
    for (uint32_t i = 0; i < exthCount && pos + 8 <= r0end; ++i) {
        uint32_t type = be32(d, pos), len = be32(d, pos + 4);
        if (len < 8 || pos + len > r0end) break;
        if (len >= 12 && type == 201) cover = be32(d, pos + 8);
        if (len >= 12 && type == 202) thumb = be32(d, pos + 8);
        pos += len;
    }
    for (int64_t offset : {cover, thumb}) {
        if (offset < 0 || offset == 0xffffffff) continue;
        uint64_t index = (uint64_t)firstImage + (uint64_t)offset;
        if (index >= count) continue;
        size_t a = recordStart((size_t)index), b = recordEnd((size_t)index);
        if (a >= b || b > d.size()) continue;
        out = d.substr(a, b - a);
        return true;
    }
    return false;
}

}  // namespace

bool hasEmbeddedCover(const std::string& path) {
    std::string e = extension(path);
    return e == "epub" || e == "fb2" || e == "mobi" || e == "azw" || e == "azw3" || e == "prc";
}

bool extractCover(const std::string& bookPath, std::string& imageBytes) {
    std::string e = extension(bookPath);
    imageBytes.clear();
    if (e == "epub") return epubCover(bookPath, imageBytes);
    if (e == "fb2") return fb2Cover(bookPath, imageBytes);
    if (e == "mobi" || e == "azw" || e == "azw3" || e == "prc") return mobiCover(bookPath, imageBytes);
    return false;
}
