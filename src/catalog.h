#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Entry {
    std::string id;           // "rom:<path>" for archive files, "app:<category>/<name>" for entries
    std::string category;
    std::string name;         // Puppy name: entry NAME or file stem
    std::string title;        // display title: stem without (...) and [...] tags
    std::string tags;
    std::string description;
    std::string command;
    std::string path;         // archive file
    std::string art;          // own image: ART, ICON, or a file in the art/icon directories
    std::string fallbackArt;  // DEFAULT_ICON of the archive
    std::string source;
    int line = 0;
    uint32_t color = 0;
    bool terminal = false;
    bool fromArchive = false;
    bool hidden = false;
};

struct Category {
    std::string name;
    std::string description;
    std::string image;   // DEFAULT_ICON or a system icon
    std::string glyph;   // icon name for lists and headers
    uint32_t color = 0;
    bool archive = false;
    std::vector<int> entries;
};

// An [ARCHIVE] section: the files it lists and the command that opens them.
struct ArchiveRule {
    std::string category;
    std::vector<std::string> extensions;   // lower case, no dot; empty matches every file
    std::vector<std::string> directories;  // expanded
    std::string command;                   // COMMAND, %s is the quoted file
    uint32_t color = 0;
};

struct CatalogSource {
    std::string path;
    bool loaded = false;
    int entries = 0, archives = 0;
};

struct CatalogMessage {
    std::string file;
    int line = 0;
    std::string text;
};

class Catalog {
public:
    std::vector<Entry> entries;
    std::vector<Category> categories;
    std::vector<CatalogSource> sources;
    std::vector<CatalogMessage> messages;
    std::vector<ArchiveRule> archives;
    double loadMs = 0;

    void load();
    int indexOf(const std::string& id) const;
    const Entry* find(const std::string& id) const;
    const Category* category(const std::string& name) const;
    // Entries that open the file at an absolute path: its library entry, else one per archive that
    // takes its extension, preferring archives whose directories hold the file.
    std::vector<Entry> entriesForFile(const std::string& path) const;
};

std::string cleanTitle(const std::string& stem, std::string* tags);
