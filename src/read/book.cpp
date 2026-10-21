#include "book.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "paths.h"
#include "util.h"

namespace {

bool isSpace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// Collapses runs of whitespace to single spaces and trims, remapping run ranges.
void normalize(Paragraph& p) {
    if (p.runs.empty()) {
        std::string& t = p.text;
        size_t w = 0;
        bool space = true;
        for (size_t r = 0; r < t.size(); ++r) {
            unsigned char c = t[r];
            if (isSpace(c)) {
                if (!space) t[w++] = ' ';
                space = true;
            } else {
                t[w++] = (char)c;
                space = false;
            }
        }
        if (w > 0 && t[w - 1] == ' ') --w;
        t.resize(w);
        return;
    }
    std::string out;
    out.reserve(p.text.size());
    std::vector<uint32_t> map(p.text.size() + 1, 0);
    bool space = true;
    for (size_t i = 0; i < p.text.size(); ++i) {
        map[i] = (uint32_t)out.size();
        unsigned char c = p.text[i];
        if (isSpace(c)) {
            if (!space) out += ' ';
            space = true;
        } else {
            out += (char)c;
            space = false;
        }
    }
    map[p.text.size()] = (uint32_t)out.size();
    while (!out.empty() && out.back() == ' ') out.pop_back();
    for (auto& r : p.runs) {
        uint32_t a = map[std::min<size_t>(r.start, p.text.size())];
        uint32_t b = map[std::min<size_t>((size_t)r.start + r.length, p.text.size())];
        b = std::min<uint32_t>(b, (uint32_t)out.size());
        r.start = a;
        r.length = b > a ? b - a : 0;
    }
    p.runs.erase(std::remove_if(p.runs.begin(), p.runs.end(), [](const Run& r) { return r.length == 0 || r.style == 0; }),
                 p.runs.end());
    p.text = std::move(out);
}

// ------------------------------------------------------------------ cache ---
// Parsed books are cached as a flat binary file keyed by path, size and mtime: reopening a large
// EPUB costs one file read instead of unzip, markup conversion and normalization.

constexpr char kMagic[8] = {'K', 'B', 'O', 'O', 'K', '0', '0', '1'};
// Bump when a loader changes its output, so cached books are parsed again.
constexpr int kParserVersion = 2;

std::string cachePath(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return {};
    std::string dir = paths.cache;
    if (dir.empty()) {
        const char* home = getenv("HOME");
        if (!home) return {};
        dir = std::string(home) + "/.cache/kennel";
    }
    dir += "/read";
    std::string id = strprintf("%s|%lld|%lld|%d", path.c_str(), (long long)st.st_size, (long long)st.st_mtime, kParserVersion);
    return strprintf("%s/%016llx.book", dir.c_str(), (unsigned long long)fnv1a(id));
}

void put32(std::string& out, uint32_t v) { out.append((const char*)&v, 4); }
void putStr(std::string& out, const std::string& s) {
    put32(out, (uint32_t)s.size());
    out += s;
}

struct Reader {
    const std::string& d;
    size_t pos = 0;
    bool ok = true;
    uint32_t u32() {
        uint32_t v = 0;
        if (pos + 4 > d.size()) return ok = false, 0;
        memcpy(&v, d.data() + pos, 4);
        pos += 4;
        return v;
    }
    std::string str() {
        uint32_t n = u32();
        if (!ok || n > d.size() - pos) return ok = false, std::string();
        std::string s = d.substr(pos, n);
        pos += n;
        return s;
    }
};

void saveCache(const std::string& file, const Book& b) {
    std::string out(kMagic, 8);
    putStr(out, b.title);
    putStr(out, b.author);
    putStr(out, b.format);
    put32(out, (uint32_t)b.paragraphs.size());
    for (const auto& p : b.paragraphs) {
        out += (char)p.kind;
        putStr(out, p.text);
        put32(out, (uint32_t)p.runs.size());
        for (const auto& r : p.runs) {
            put32(out, r.start);
            put32(out, r.length);
            out += (char)r.style;
        }
    }
    put32(out, (uint32_t)b.chapters.size());
    for (const auto& c : b.chapters) {
        putStr(out, c.title);
        put32(out, c.paragraph);
    }
    makeDirs(dirName(file));
    writeFileAtomic(file, out);
}

bool loadCache(const std::string& file, Book& b) {
    auto data = readFile(file);
    if (!data || data->size() < 8 || memcmp(data->data(), kMagic, 8) != 0) return false;
    Reader r{*data, 8};
    b.title = r.str();
    b.author = r.str();
    b.format = r.str();
    uint32_t n = r.u32();
    if (!r.ok || n > data->size()) return false;
    b.paragraphs.resize(n);
    for (auto& p : b.paragraphs) {
        if (r.pos >= data->size()) return false;
        p.kind = (Paragraph::Kind)(*data)[r.pos++];
        p.text = r.str();
        uint32_t runs = r.u32();
        if (!r.ok || runs > data->size()) return false;
        p.runs.resize(runs);
        for (auto& run : p.runs) {
            run.start = r.u32();
            run.length = r.u32();
            if (r.pos >= data->size()) return false;
            run.style = (uint8_t)(*data)[r.pos++];
            if (run.start > p.text.size() || run.length > p.text.size() - run.start) return false;
        }
    }
    uint32_t chapters = r.u32();
    if (!r.ok || chapters > data->size()) return false;
    b.chapters.resize(chapters);
    for (auto& c : b.chapters) {
        c.title = r.str();
        c.paragraph = r.u32();
        if (c.paragraph >= n) return false;
    }
    if (!r.ok || r.pos != data->size()) return false;
    b.offsets.resize(n);
    b.size = 0;
    for (size_t i = 0; i < n; ++i) {
        b.offsets[i] = b.size;
        b.size += b.paragraphs[i].text.size() + 1;
    }
    return n > 0;
}

}  // namespace

void Book::finish() {
    std::vector<int> remap(paragraphs.size() + 1, 0);
    std::vector<Paragraph> kept;
    kept.reserve(paragraphs.size());
    for (size_t i = 0; i < paragraphs.size(); ++i) {
        remap[i] = (int)kept.size();
        Paragraph& p = paragraphs[i];
        if (p.kind == Paragraph::Kind::Break) {
            p.text.clear();
            p.runs.clear();
            if (kept.empty() || kept.back().kind == Paragraph::Kind::Break) continue;
            kept.push_back(std::move(p));
            continue;
        }
        normalize(p);
        if (p.text.empty()) continue;
        kept.push_back(std::move(p));
    }
    remap[paragraphs.size()] = (int)kept.size();
    while (!kept.empty() && kept.back().kind == Paragraph::Kind::Break) kept.pop_back();
    paragraphs = std::move(kept);

    for (auto& c : chapters) c.paragraph = (uint32_t)std::min<int>(remap[std::min<size_t>(c.paragraph, remap.size() - 1)],
                                                                    std::max<int>(0, (int)paragraphs.size() - 1));
    chapters.erase(std::remove_if(chapters.begin(), chapters.end(), [](const Chapter& c) { return trim(c.title).empty(); }),
                   chapters.end());
    std::stable_sort(chapters.begin(), chapters.end(), [](const Chapter& a, const Chapter& b) { return a.paragraph < b.paragraph; });
    if (chapters.empty())
        for (size_t i = 0; i < paragraphs.size(); ++i)
            if (paragraphs[i].kind == Paragraph::Kind::Heading) chapters.push_back({paragraphs[i].text, (uint32_t)i});

    offsets.resize(paragraphs.size());
    size = 0;
    for (size_t i = 0; i < paragraphs.size(); ++i) {
        offsets[i] = size;
        size += paragraphs[i].text.size() + 1;
    }
    title = trim(title);
    author = trim(author);
}

int Book::paragraphAt(uint64_t offset) const {
    if (offsets.empty()) return 0;
    auto it = std::upper_bound(offsets.begin(), offsets.end(), offset);
    return std::max(0, (int)(it - offsets.begin()) - 1);
}

int Book::chapterAt(int paragraph) const {
    int found = -1;
    for (size_t i = 0; i < chapters.size(); ++i)
        if ((int)chapters[i].paragraph <= paragraph) found = (int)i;
    return found;
}

namespace {
std::string pendingCache;
const Book* pendingBook = nullptr;
}  // namespace

bool loadBook(const std::string& path, Book& book, std::string& error) {
    book = Book{};
    book.path = path;
    std::string ext = lower(path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.') + 1));
    std::string cache = cachePath(path);
    if (!cache.empty() && loadCache(cache, book)) return true;
    book = Book{};
    book.path = path;
    bool ok;
    if (ext == "txt") ok = loadTxt(path, book, error);
    else if (ext == "epub") ok = loadEpub(path, book, error);
    else if (ext == "html" || ext == "htm" || ext == "xhtml") ok = loadHtml(path, book, error);
    else if (ext == "fb2") ok = loadFb2(path, book, error);
    else if (ext == "pdb" || ext == "mobi" || ext == "prc") ok = loadPdb(path, book, error);
    else {
        error = "unsupported format: ." + ext;
        return false;
    }
    if (!ok) return false;
    if (book.title.empty()) {
        std::string name = baseName(path);
        book.title = name.substr(0, name.rfind('.'));
    }
    book.finish();
    if (book.paragraphs.empty()) {
        error = "no text found";
        return false;
    }
    if (!cache.empty()) {
        pendingCache = cache;
        pendingBook = &book;
    }
    return true;
}

void flushBookCache() {
    if (pendingBook && !pendingCache.empty()) saveCache(pendingCache, *pendingBook);
    pendingCache.clear();
    pendingBook = nullptr;
}
