#include "cover.h"

#include <algorithm>
#include <cstdio>
#include <optional>
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

// The package document (OPF) named by META-INF/container.xml.
std::optional<std::string> epubPackage(const ZipArchive& zip, std::string& opfPath) {
    auto container = zip.read("META-INF/container.xml");
    if (!container) return std::nullopt;
    markup::Tokenizer tz(*container);
    markup::Token t;
    while (tz.next(t))
        if (t.type != markup::Token::Type::Close && t.local() == "rootfile")
            if (const std::string* p = t.attr("full-path")) {
                opfPath = percentDecode(*p);
                break;
            }
    return opfPath.empty() ? std::nullopt : zip.read(opfPath);
}

bool epubCover(const std::string& path, std::string& out) {
    ZipArchive zip;
    std::string error;
    if (!zip.open(path, error)) return false;
    std::string opfPath;
    auto opf = epubPackage(zip, opfPath);
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

std::string readHead(const std::string& path, size_t maxBytes) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string out(maxBytes, '\0');
    out.resize(fread(out.data(), 1, maxBytes, f));
    fclose(f);
    return out;
}

// Text up to the close of `name`, whitespace runs collapsed.
std::string elementText(markup::Tokenizer& tz, const std::string& name) {
    std::string out;
    markup::Token t;
    while (tz.next(t)) {
        if (t.type == markup::Token::Type::Close && t.name == name) break;
        if (t.type == markup::Token::Type::Text) out += t.text;
    }
    return out;
}

std::string collapseSpaces(std::string_view s) {
    std::string out;
    for (char c : s) {
        bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (space && (out.empty() || out.back() == ' ')) continue;
        out += space ? ' ' : c;
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool isMobi(const std::string& e) { return e == "mobi" || e == "azw" || e == "azw3" || e == "prc"; }
bool isEbook(const std::string& e) { return e == "epub" || e == "fb2" || isMobi(e); }

void epubTitle(const std::string& path, std::string& title, std::string& author) {
    ZipArchive zip;
    std::string error, opfPath;
    if (!zip.open(path, error)) return;
    auto opf = epubPackage(zip, opfPath);
    if (!opf) return;
    markup::Tokenizer tz(*opf);
    markup::Token t;
    while (tz.next(t) && (title.empty() || author.empty())) {
        if (t.type != markup::Token::Type::Open) continue;
        std::string n = t.local();
        if (n == "title" && title.empty()) title = elementText(tz, t.name);
        else if (n == "creator" && author.empty()) author = elementText(tz, t.name);
        else if (n == "manifest") break;
    }
}

// title-info is at the start of the file, before the body and the binaries.
void fb2Title(const std::string& path, std::string& title, std::string& author) {
    std::string head = readHead(path, 64 * 1024);
    markup::Tokenizer tz(head);
    markup::Token t;
    bool inTitleInfo = false, inAuthor = false;
    std::string first, middle, last;
    while (tz.next(t)) {
        if (t.type == markup::Token::Type::Text) continue;
        std::string n = t.local();
        bool open = t.type == markup::Token::Type::Open;
        if (n == "title-info") {
            if (!open) break;
            inTitleInfo = true;
        } else if (!inTitleInfo) {
            continue;
        } else if (n == "book-title" && open) {
            title = elementText(tz, t.name);
        } else if (n == "author" && author.empty()) {
            inAuthor = open;
            if (!open) author = collapseSpaces(first + " " + middle + " " + last);
        } else if (inAuthor && open && (n == "first-name" || n == "middle-name" || n == "last-name")) {
            (n == "first-name" ? first : n == "middle-name" ? middle : last) = elementText(tz, t.name);
        }
    }
}

// MOBI header full name, EXTH 503 (updated title) and 100 (author) of record 0.
void mobiTitle(const std::string& path, std::string& title, std::string& author) {
    std::string d = readHead(path, 78);
    if (d.size() < 78 || d.compare(60, 8, "BOOKMOBI") != 0) return;
    uint16_t count = be16(d, 76);
    if (count == 0) return;
    d = readHead(path, 78 + (size_t)count * 8);
    if (d.size() < 78 + (size_t)count * 8) return;
    size_t r0 = be32(d, 78), r0end = count > 1 ? be32(d, 86) : r0 + 64 * 1024;
    if (r0end <= r0 || r0end - r0 > 1024 * 1024) return;
    d = readHead(path, r0end);
    r0end = std::min(r0end, d.size());
    if (r0 + 0x84 > r0end || d.compare(r0 + 16, 4, "MOBI") != 0) return;
    bool cp1252 = be32(d, r0 + 28) == 1252;
    auto text = [&](size_t at, size_t len) {
        std::string_view s(d.data() + at, len);
        return cp1252 || !markup::validUtf8(s) ? markup::cp1252ToUtf8(s) : std::string(s);
    };
    uint32_t headerLength = be32(d, r0 + 20), nameOff = be32(d, r0 + 84), nameLen = be32(d, r0 + 88);
    if (nameOff < r0end - r0 && nameLen <= r0end - r0 - nameOff) title = text(r0 + nameOff, nameLen);
    if (!(be32(d, r0 + 0x80) & 0x40)) return;
    size_t exth = r0 + 16 + headerLength;
    if (exth + 12 > r0end || d.compare(exth, 4, "EXTH") != 0) return;
    uint32_t exthCount = be32(d, exth + 8);
    size_t pos = exth + 12;
    for (uint32_t i = 0; i < exthCount && pos + 8 <= r0end; ++i) {
        uint32_t type = be32(d, pos), len = be32(d, pos + 4);
        if (len < 8 || len > r0end - pos) break;
        if (type == 503) title = text(pos + 8, len - 8);
        if (type == 100 && author.empty()) author = text(pos + 8, len - 8);
        pos += len;
    }
}

}  // namespace

bool hasEmbeddedCover(const std::string& path) { return isEbook(extension(path)); }

bool extractCover(const std::string& bookPath, std::string& imageBytes) {
    std::string e = extension(bookPath);
    imageBytes.clear();
    if (e == "epub") return epubCover(bookPath, imageBytes);
    if (e == "fb2") return fb2Cover(bookPath, imageBytes);
    if (isMobi(e)) return mobiCover(bookPath, imageBytes);
    return false;
}

bool hasEmbeddedTitle(const std::string& path) { return isEbook(extension(path)); }

void extractTitle(const std::string& bookPath, std::string& title, std::string& author) {
    std::string e = extension(bookPath);
    title.clear();
    author.clear();
    if (e == "epub") epubTitle(bookPath, title, author);
    else if (e == "fb2") fb2Title(bookPath, title, author);
    else if (isMobi(e)) mobiTitle(bookPath, title, author);
    title = collapseSpaces(title);
    author = collapseSpaces(author);
    if (!markup::validUtf8(title)) title.clear();
    if (!markup::validUtf8(author)) author.clear();
}
