#include "book.h"

#include <algorithm>

#include "util.h"

namespace {

// Collapses runs of whitespace to single spaces and trims, remapping run ranges.
void normalize(Paragraph& p) {
    std::string out;
    out.reserve(p.text.size());
    std::vector<uint32_t> map(p.text.size() + 1, 0);
    bool space = true;
    for (size_t i = 0; i < p.text.size(); ++i) {
        map[i] = (uint32_t)out.size();
        unsigned char c = p.text[i];
        bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        if (ws) {
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

bool loadBook(const std::string& path, Book& book, std::string& error) {
    book = Book{};
    book.path = path;
    std::string ext = lower(path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.') + 1));
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
    return true;
}
