#include <cstdio>
#include <cstring>

#include "book.h"
#include "reader.h"

// kennel-read BOOK              reads a book
// kennel-read --dump BOOK       prints the parsed book (development)
int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--dump")) {
        Book b;
        std::string err;
        bool ok = loadBook(argv[2], b, err);
        flushBookCache();
        if (!ok) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        printf("title: %s\nauthor: %s\nformat: %s\nparagraphs: %zu\nchapters: %zu\nsize: %llu\n", b.title.c_str(),
               b.author.c_str(), b.format.c_str(), b.paragraphs.size(), b.chapters.size(), (unsigned long long)b.size);
        for (const auto& c : b.chapters) printf("  [%u] %s\n", c.paragraph, c.title.c_str());
        for (const auto& p : b.paragraphs) {
            const char* k = p.kind == Paragraph::Kind::Heading ? "H" : p.kind == Paragraph::Kind::Break ? "-" : "P";
            printf("%s %s\n", k, p.text.c_str());
        }
        return 0;
    }
    if (argc != 2 || argv[1][0] == '-') {
        fprintf(stderr, "usage: kennel-read BOOK | --dump BOOK\n");
        return 2;
    }
    static Reader reader;
    return reader.run(argc, argv);
}
