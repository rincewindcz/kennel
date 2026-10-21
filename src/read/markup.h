#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "book.h"
#include "tokenizer.h"

namespace markup {

// Accumulates text into Book paragraphs with bold/italic runs.
class ParagraphBuilder {
public:
    explicit ParagraphBuilder(Book& book) : book_(book) {}
    void text(std::string_view s);
    void flush();
    void heading(bool on);
    void brk();
    void bold(int delta) { bold_ += delta; }
    void italic(int delta) { italic_ += delta; }
    uint32_t next() const { return (uint32_t)book_.paragraphs.size(); }
    bool empty() const { return cur_.text.empty(); }
    std::string lastText() const;

private:
    Book& book_;
    Paragraph cur_;
    bool heading_ = false;
    int bold_ = 0, italic_ = 0;
};

// HTML/XHTML document to paragraphs. Element ids are recorded with the index of the paragraph
// they start (ids[prefix + id]).
struct HtmlOptions {
    std::unordered_map<std::string, uint32_t>* ids = nullptr;
    std::string idPrefix;
    std::string* title = nullptr;
};
void htmlToParagraphs(std::string_view html, Book& book, const HtmlOptions& opt = {});

}  // namespace markup
