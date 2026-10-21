// Placeholder loaders, replaced by the real EPUB, HTML, FB2 and PDB implementations.
#include "book.h"

bool loadEpub(const std::string&, Book&, std::string& e) { return (e = "EPUB not implemented yet"), false; }
bool loadHtml(const std::string&, Book&, std::string& e) { return (e = "HTML not implemented yet"), false; }
bool loadFb2(const std::string&, Book&, std::string& e) { return (e = "FB2 not implemented yet"), false; }
bool loadPdb(const std::string&, Book&, std::string& e) { return (e = "PDB not implemented yet"), false; }
