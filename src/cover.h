#pragma once

#include <string>

// Cover image embedded in an e-book (EPUB, FB2, MOBI/AZW/PRC): the encoded image file (JPEG, PNG).
bool extractCover(const std::string& bookPath, std::string& imageBytes);
bool hasEmbeddedCover(const std::string& path);  // by extension
