#include "markup.h"

#include <algorithm>

#include "util.h"

namespace markup {

// ------------------------------------------------------------ paragraphs ---

void ParagraphBuilder::text(std::string_view s) {
    if (s.empty()) return;
    uint8_t style = (bold_ > 0 ? Run::Bold : 0) | (italic_ > 0 ? Run::Italic : 0);
    uint32_t at = (uint32_t)cur_.text.size();
    cur_.text.append(s);
    if (!style) return;
    if (!cur_.runs.empty()) {
        Run& last = cur_.runs.back();
        if (last.style == style && last.start + last.length == at) {
            last.length += (uint32_t)s.size();
            return;
        }
    }
    cur_.runs.push_back({at, (uint32_t)s.size(), style});
}

void ParagraphBuilder::flush() {
    bool blank = cur_.text.find_first_not_of(" \t\r\n\f\v") == std::string::npos;
    if (!blank) {
        cur_.kind = heading_ ? Paragraph::Kind::Heading : Paragraph::Kind::Body;
        book_.paragraphs.push_back(std::move(cur_));
    }
    cur_ = Paragraph{};
}

void ParagraphBuilder::heading(bool on) {
    flush();
    heading_ = on;
}

void ParagraphBuilder::brk() {
    flush();
    Paragraph p;
    p.kind = Paragraph::Kind::Break;
    book_.paragraphs.push_back(std::move(p));
}

std::string ParagraphBuilder::lastText() const {
    for (auto it = book_.paragraphs.rbegin(); it != book_.paragraphs.rend(); ++it)
        if (it->kind != Paragraph::Kind::Break) return it->text;
    return {};
}

namespace {

bool isBlock(const std::string& n) {
    static const char* kBlocks[] = {"p",     "div",     "li",      "ul",     "ol",      "dl",    "dd",     "dt",
                                    "blockquote", "section", "article", "header", "footer", "aside", "nav",
                                    "table", "tr",      "td",      "th",     "caption", "figure", "figcaption",
                                    "address", "center", "main",   "body",   "tbody",   "thead", "tfoot",
                                    "details", "summary", "hgroup", "fieldset", "legend", "form"};
    for (const char* b : kBlocks)
        if (n == b) return true;
    return false;
}

bool isHeading(const std::string& n) { return n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6'; }

bool isSkipped(const std::string& n) {
    return n == "script" || n == "style" || n == "head" || n == "svg" || n == "math" || n == "object" ||
           n == "noscript" || n == "template" || n == "iframe" || n == "select" || n == "button";
}

bool isVoid(const std::string& n) {
    return n == "br" || n == "hr" || n == "img" || n == "meta" || n == "link" || n == "input" || n == "col" ||
           n == "area" || n == "base" || n == "wbr" || n == "source" || n == "embed" || n == "param" ||
           n == "track" || n == "mbp:pagebreak";
}

}  // namespace

void htmlToParagraphs(std::string_view html, Book& book, const HtmlOptions& opt) {
    Tokenizer tok(html);
    Token t;
    ParagraphBuilder pb(book);
    std::string skip;
    int skipDepth = 0;
    bool inTitle = false, pre = false;
    int headingDepth = 0;

    while (tok.next(t)) {
        if (!skip.empty()) {
            if (t.name == "title" && opt.title && skip == "head") {
                if (t.type == Token::Type::Open) inTitle = true;
                else if (t.type == Token::Type::Close) inTitle = false;
            } else if (t.type == Token::Type::Text && inTitle && opt.title) {
                *opt.title += t.text;
            } else if (t.name == skip) {
                if (t.type == Token::Type::Open) ++skipDepth;
                else if (t.type == Token::Type::Close && --skipDepth == 0) skip.clear();
            }
            continue;
        }
        if (t.type == Token::Type::Text) {
            if (inTitle) {
                *opt.title += t.text;
                continue;
            }
            if (!pre) {
                pb.text(t.text);
                continue;
            }
            size_t start = 0;
            while (true) {
                size_t nl = t.text.find('\n', start);
                pb.text(std::string_view(t.text).substr(start, nl == std::string::npos ? std::string::npos : nl - start));
                if (nl == std::string::npos) break;
                pb.flush();
                start = nl + 1;
            }
            continue;
        }

        const std::string& n = t.name;
        bool open = t.type != Token::Type::Close;
        bool selfClose = t.type == Token::Type::SelfClose || (open && isVoid(n));

        if (open && opt.ids) {
            const std::string* id = t.attr("id");
            if (!id && n == "a") id = t.attr("name");
            if (id && !id->empty()) opt.ids->emplace(opt.idPrefix + *id, pb.next());
        }
        if (open && isSkipped(n)) {
            if (!selfClose) {
                skip = n;
                skipDepth = 1;
            }
            continue;
        }
        if (n == "title" && opt.title) {
            inTitle = t.type == Token::Type::Open;
            continue;
        }

        if (isHeading(n)) {
            if (t.type == Token::Type::Open) {
                if (headingDepth++ == 0) pb.heading(true);
            } else if (t.type == Token::Type::Close && headingDepth > 0) {
                if (--headingDepth == 0) pb.heading(false);
            }
        } else if (n == "br") {
            if (headingDepth) pb.text(" ");
            else pb.flush();
        } else if (n == "hr" || n == "mbp:pagebreak") {
            pb.brk();
        } else if (n == "pre") {
            pb.flush();
            pre = open && !selfClose;
        } else if (isBlock(n)) {
            if (headingDepth) pb.text(" ");
            else pb.flush();
        } else if (n == "b" || n == "strong") {
            if (!selfClose) pb.bold(open ? 1 : -1);
        } else if (n == "i" || n == "em" || n == "cite" || n == "var" || n == "dfn") {
            if (!selfClose) pb.italic(open ? 1 : -1);
        }
    }
    pb.flush();
}

}  // namespace markup
