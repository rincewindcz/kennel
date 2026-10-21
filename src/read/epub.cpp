#include <cctype>
#include <unordered_map>

#include "book.h"
#include "markup.h"
#include "util.h"
#include "zip.h"

namespace {

struct Item {
    std::string href, type, properties;
};

std::string percentDecode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            out += (char)strtol(std::string(s.substr(i + 1, 2)).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Joins an href to the directory of the referencing file and normalizes "." and ".." segments.
// The fragment, if any, is returned separately.
std::string resolve(const std::string& baseDir, const std::string& href, std::string* fragment = nullptr) {
    std::string h = href;
    size_t hash = h.find('#');
    if (fragment) *fragment = hash == std::string::npos ? "" : h.substr(hash + 1);
    if (hash != std::string::npos) h.erase(hash);
    h = percentDecode(h);
    std::string full = h.empty() || h[0] == '/' ? h.substr(h.empty() ? 0 : 1) : (baseDir.empty() ? h : baseDir + "/" + h);
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= full.size()) {
        size_t slash = full.find('/', pos);
        if (slash == std::string::npos) slash = full.size();
        std::string seg = full.substr(pos, slash - pos);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        pos = slash + 1;
    }
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

std::string dirOf(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

struct TocEntry {
    std::string title, target, fragment;
};

void collectText(markup::Tokenizer& tok, const std::string& endTag, std::string& out) {
    markup::Token t;
    int depth = 1;
    while (tok.next(t)) {
        if (t.type == markup::Token::Type::Text) out += t.text;
        else if (t.name == endTag && t.type == markup::Token::Type::Open) ++depth;
        else if (t.name == endTag && t.type == markup::Token::Type::Close && --depth == 0) return;
    }
}

std::vector<TocEntry> navToc(const std::string& doc, const std::string& dir) {
    std::vector<TocEntry> out, any;
    markup::Tokenizer tok(doc);
    markup::Token t;
    bool inNav = false, isToc = false;
    int navDepth = 0;
    while (tok.next(t)) {
        if (t.local() == "nav") {
            if (t.type == markup::Token::Type::Open) {
                if (navDepth++ == 0) {
                    const std::string* type = t.attr("type");
                    isToc = type && type->find("toc") != std::string::npos;
                    inNav = true;
                }
            } else if (t.type == markup::Token::Type::Close && navDepth > 0 && --navDepth == 0) {
                inNav = false;
                if (isToc && !out.empty()) return out;
            }
            continue;
        }
        if (inNav && t.name == "a" && t.type == markup::Token::Type::Open) {
            TocEntry e;
            const std::string* href = t.attr("href");
            collectText(tok, "a", e.title);
            if (!href) continue;
            e.target = resolve(dir, *href, &e.fragment);
            e.title = trim(e.title);
            (isToc ? out : any).push_back(std::move(e));
        }
    }
    return out.empty() ? any : out;
}

std::vector<TocEntry> ncxToc(const std::string& doc, const std::string& dir) {
    std::vector<TocEntry> out;
    markup::Tokenizer tok(doc);
    markup::Token t;
    std::string label;
    bool inLabel = false;
    while (tok.next(t)) {
        std::string n = t.local();
        if (n == "navlabel") inLabel = t.type == markup::Token::Type::Open;
        else if (n == "text" && inLabel && t.type == markup::Token::Type::Open) {
            label.clear();
            collectText(tok, t.name, label);
        } else if (n == "content" && t.type != markup::Token::Type::Close) {
            if (const std::string* src = t.attr("src")) {
                TocEntry e;
                e.title = trim(label);
                e.target = resolve(dir, *src, &e.fragment);
                out.push_back(std::move(e));
            }
        }
    }
    return out;
}

}  // namespace

bool loadEpub(const std::string& path, Book& book, std::string& error) {
    ZipArchive zip;
    if (!zip.open(path, error)) return false;

    auto container = zip.read("META-INF/container.xml");
    if (!container) {
        error = "EPUB without META-INF/container.xml";
        return false;
    }
    std::string opfPath;
    {
        markup::Tokenizer tok(*container);
        markup::Token t;
        while (tok.next(t) && opfPath.empty())
            if (t.local() == "rootfile" && t.type != markup::Token::Type::Close)
                if (const std::string* p = t.attr("full-path")) opfPath = resolve("", *p);
    }
    auto opf = opfPath.empty() ? std::nullopt : zip.read(opfPath);
    if (!opf) {
        error = "EPUB package file missing";
        return false;
    }
    std::string opfDir = dirOf(opfPath);

    std::unordered_map<std::string, Item> manifest;
    std::vector<std::string> spine;
    std::string tocId, navHref;
    {
        markup::Tokenizer tok(*opf);
        markup::Token t;
        while (tok.next(t)) {
            if (t.type == markup::Token::Type::Close) continue;
            std::string n = t.local();
            if (n == "title" && book.title.empty() && t.type == markup::Token::Type::Open) collectText(tok, t.name, book.title);
            else if (n == "creator" && book.author.empty() && t.type == markup::Token::Type::Open)
                collectText(tok, t.name, book.author);
            else if (n == "item") {
                const std::string *id = t.attr("id"), *href = t.attr("href");
                if (!id || !href) continue;
                Item it{resolve(opfDir, *href), t.attr("media-type") ? *t.attr("media-type") : "",
                        t.attr("properties") ? *t.attr("properties") : ""};
                if (it.properties.find("nav") != std::string::npos) navHref = it.href;
                manifest[*id] = std::move(it);
            } else if (n == "spine") {
                if (const std::string* toc = t.attr("toc")) tocId = *toc;
            } else if (n == "itemref") {
                if (const std::string* idref = t.attr("idref")) spine.push_back(*idref);
            }
        }
    }
    book.title = trim(book.title);
    book.author = trim(book.author);

    std::unordered_map<std::string, uint32_t> starts, ids;
    for (const auto& idref : spine) {
        auto it = manifest.find(idref);
        if (it == manifest.end()) continue;
        const Item& item = it->second;
        if (item.type.find("html") == std::string::npos && item.type.find("xml") == std::string::npos) continue;
        auto doc = zip.read(item.href);
        if (!doc) continue;
        starts.emplace(item.href, (uint32_t)book.paragraphs.size());
        markup::htmlToParagraphs(markup::sanitizeUtf8(*doc), book, {&ids, item.href + "#", nullptr});
        book.paragraphs.push_back({Paragraph::Kind::Break, {}, {}});
    }
    if (book.paragraphs.empty()) {
        error = "EPUB without readable text";
        return false;
    }

    std::vector<TocEntry> toc;
    if (!navHref.empty())
        if (auto nav = zip.read(navHref)) toc = navToc(*nav, dirOf(navHref));
    if (toc.empty() && !tocId.empty()) {
        auto it = manifest.find(tocId);
        if (it != manifest.end())
            if (auto ncx = zip.read(it->second.href)) toc = ncxToc(*ncx, dirOf(it->second.href));
    }
    for (const auto& e : toc) {
        auto fragment = e.fragment.empty() ? ids.end() : ids.find(e.target + "#" + e.fragment);
        if (fragment != ids.end()) {
            book.chapters.push_back({e.title, fragment->second});
            continue;
        }
        auto start = starts.find(e.target);
        if (start != starts.end()) book.chapters.push_back({e.title, start->second});
    }
    book.format = "EPUB";
    return true;
}
