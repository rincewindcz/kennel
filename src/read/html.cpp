#include "book.h"
#include "markup.h"
#include "util.h"

bool loadHtml(const std::string& path, Book& book, std::string& error) {
    auto data = readFile(path);
    if (!data) {
        error = "cannot read " + path;
        return false;
    }
    std::string html = markup::sanitizeUtf8(*data);
    std::string title;
    markup::htmlToParagraphs(html, book, {nullptr, {}, &title});
    book.title = title;
    book.format = "HTML";
    return true;
}
