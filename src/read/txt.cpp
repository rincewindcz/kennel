#include "book.h"

#include "util.h"

// Plain UTF-8 text. Blank lines separate paragraphs when the file has them (hard-wrapped text,
// e.g. Project Gutenberg); otherwise every line is a paragraph.
bool loadTxt(const std::string& path, Book& book, std::string& error) {
    auto data = readFile(path);
    if (!data) {
        error = "cannot read " + path;
        return false;
    }
    std::string text = std::move(*data);
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        pos = end + 1;
    }
    size_t blank = 0, nonBlank = 0;
    for (const auto& l : lines) (trim(l).empty() ? blank : nonBlank)++;
    bool blocks = blank > 0 && nonBlank > blank * 2;

    Paragraph cur;
    auto flush = [&] {
        if (!trim(cur.text).empty()) book.paragraphs.push_back(std::move(cur));
        cur = Paragraph{};
    };
    for (const auto& l : lines) {
        if (trim(l).empty()) {
            flush();
            continue;
        }
        if (blocks) {
            if (!cur.text.empty()) cur.text += ' ';
            cur.text += l;
        } else {
            cur.text = l;
            flush();
        }
    }
    flush();
    book.format = "TXT";
    return true;
}
