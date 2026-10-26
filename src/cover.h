#pragma once

#include <string>

// Cover image embedded in an e-book (EPUB, FB2, MOBI/AZW/PRC): the encoded image file (JPEG, PNG).
bool extractCover(const std::string& bookPath, std::string& imageBytes);
bool hasEmbeddedCover(const std::string& path);  // by extension

// Title and author stored in the same formats, UTF-8 with single spaces; empty when missing.
void extractTitle(const std::string& bookPath, std::string& title, std::string& author);
bool hasEmbeddedTitle(const std::string& path);  // by extension
