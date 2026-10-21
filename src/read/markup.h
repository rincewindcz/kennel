#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "book.h"

namespace markup {

struct Token {
    enum class Type { Text, Open, Close, SelfClose };
    Type type = Type::Text;
    std::string name;  // lowercased, namespace prefix kept (dc:title, epub:type)
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;  // decoded text (Text tokens)

    const std::string* attr(std::string_view key) const;
    std::string local() const;  // name without namespace prefix
};

// Tolerant HTML/XHTML/XML tokenizer: never fails, malformed markup becomes text or is skipped.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view src) : s_(src) {}
    bool next(Token& t);

private:
    std::string_view s_;
    size_t pos_ = 0;
    std::string rawEnd_;  // set after <script>/<style>: their content is raw text
};

std::string decodeEntities(std::string_view s);
void appendUtf8(std::string& out, uint32_t cp);
bool validUtf8(std::string_view s);
std::string sanitizeUtf8(std::string_view s);  // invalid sequences -> U+FFFD
std::string cp1252ToUtf8(std::string_view s);

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
