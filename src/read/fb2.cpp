#include "book.h"
#include "markup.h"
#include "util.h"

// FictionBook 2: sections with titles become chapters; notes bodies and binaries are skipped.
bool loadFb2(const std::string& path, Book& book, std::string& error) {
    auto data = readFile(path);
    if (!data) {
        error = "cannot read " + path;
        return false;
    }
    std::string xml = markup::sanitizeUtf8(*data);
    markup::Tokenizer tok(xml);
    markup::Token t;
    markup::ParagraphBuilder pb(book);

    std::string skip;
    int skipDepth = 0;
    bool inTitleInfo = false, inAuthor = false, inBody = false, inBookTitle = false;
    int sectionDepth = 0, titleDepth = 0;
    uint32_t titleStart = 0;
    std::string first, middle, last, *nameField = nullptr;
    bool authorDone = false;

    while (tok.next(t)) {
        using Type = markup::Token::Type;
        std::string n = t.local();
        if (!skip.empty()) {
            if (n == skip) {
                if (t.type == Type::Open) ++skipDepth;
                else if (t.type == Type::Close && --skipDepth == 0) skip.clear();
            }
            continue;
        }
        if (t.type == Type::Text) {
            if (nameField) *nameField += t.text;
            else if (inBookTitle) book.title += t.text;
            else if (inBody) pb.text(t.text);
            continue;
        }
        bool open = t.type == Type::Open, close = t.type == Type::Close;

        if (n == "binary" && open) {
            skip = n;
            skipDepth = 1;
        } else if (n == "title-info") {
            inTitleInfo = open;
        } else if (inTitleInfo && n == "book-title") {
            inBookTitle = open;
        } else if (inTitleInfo && n == "author" && !authorDone) {
            inAuthor = open;
            if (close) {
                std::string full = trim(first + " " + middle);
                full = trim(full + " " + last);
                if (!full.empty()) {
                    book.author = full;
                    authorDone = true;
                }
            }
        } else if (inAuthor && (n == "first-name" || n == "middle-name" || n == "last-name")) {
            nameField = open ? (n == "first-name" ? &first : n == "middle-name" ? &middle : &last) : nullptr;
        } else if (n == "body") {
            if (open) {
                const std::string* name = t.attr("name");
                if (name && (*name == "notes" || *name == "comments")) {
                    skip = n;
                    skipDepth = 1;
                    continue;
                }
                inBody = true;
            } else if (close) {
                pb.flush();
                inBody = false;
            }
        } else if (!inBody) {
            continue;
        } else if (n == "section") {
            pb.flush();
            if (open) ++sectionDepth;
            else if (close && sectionDepth > 0) --sectionDepth;
        } else if (n == "title") {
            if (open) {
                if (titleDepth++ == 0) {
                    pb.heading(true);
                    titleStart = pb.next();
                }
            } else if (close && titleDepth > 0 && --titleDepth == 0) {
                pb.heading(false);
                if (sectionDepth > 0) {
                    std::string title;
                    for (uint32_t i = titleStart; i < book.paragraphs.size(); ++i)
                        title += (title.empty() ? "" : " ") + book.paragraphs[i].text;
                    if (!trim(title).empty()) book.chapters.push_back({trim(title), titleStart});
                }
            }
        } else if (n == "subtitle") {
            pb.heading(open);
        } else if (n == "p" || n == "v" || n == "text-author" || n == "td" || n == "th") {
            pb.flush();
        } else if (n == "empty-line") {
            pb.brk();
        } else if (n == "stanza" || n == "poem" || n == "epigraph" || n == "cite" || n == "table" || n == "tr") {
            pb.flush();
            if (close && n == "stanza") pb.brk();
        } else if (n == "emphasis") {
            if (!(t.type == Type::SelfClose)) pb.italic(open ? 1 : -1);
        } else if (n == "strong") {
            if (!(t.type == Type::SelfClose)) pb.bold(open ? 1 : -1);
        }
    }
    pb.flush();
    book.format = "FB2";
    return true;
}
