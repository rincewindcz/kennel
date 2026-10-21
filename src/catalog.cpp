#include "catalog.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <dirent.h>
#include <map>
#include <sys/stat.h>
#include <unordered_map>

#include "cover.h"
#include "ini.h"
#include "paths.h"
#include "platform.h"
#include "util.h"

namespace {

struct Record {
    bool archive = false;
    std::string key;
    IniSection section;
    std::string file;
};

// Files of one directory: stem -> path, preferring png over jpg over jpeg.
using ImageIndex = std::unordered_map<std::string, std::string>;

const char* kImageExts[] = {"png", "jpg", "jpeg"};

int imageRank(const std::string& ext) {
    for (int i = 0; i < 3; ++i)
        if (ext == kImageExts[i]) return i;
    return -1;
}

void indexImages(const std::string& dir, ImageIndex& index, std::unordered_map<std::string, int>& rank) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* de = readdir(d)) {
        std::string name = de->d_name;
        if (name.empty() || name[0] == '.') continue;
        size_t dot = name.rfind('.');
        if (dot == std::string::npos) continue;
        int r = imageRank(lower(name.substr(dot + 1)));
        if (r < 0) continue;
        std::string stem = name.substr(0, dot);
        for (const std::string& key : {stem, "\x01" + lower(stem)}) {
            auto it = rank.find(key);
            if (it != rank.end() && it->second <= r) continue;
            rank[key] = r;
            index[key] = dir + "/" + name;
        }
    }
    closedir(d);
}

std::string lookupImage(const ImageIndex& index, const std::string& stem) {
    auto it = index.find(stem);
    if (it == index.end()) it = index.find("\x01" + lower(stem));
    return it == index.end() ? std::string() : it->second;
}

std::string guessGlyph(const std::string& category) {
    std::string c = lower(category);
    if (c.find("system") != std::string::npos) return "settings";
    if (c.find("terminal") != std::string::npos) return "terminal";
    if (c.find("emulator") != std::string::npos || c.find("frontend") != std::string::npos) return "layers";
    if (c.find("app") != std::string::npos || c.find("tool") != std::string::npos) return "wrench";
    if (c.find("book") != std::string::npos || c.find("read") != std::string::npos) return "book-open";
    return "gamepad-2";
}

}  // namespace

std::string cleanTitle(const std::string& stem, std::string* tags) {
    std::string title = replaceAll(stem, "_", " ");
    std::vector<std::string> found;
    while (true) {
        std::string t = trim(title);
        if (t.empty()) break;
        char close = t.back();
        char open = close == ')' ? '(' : close == ']' ? '[' : 0;
        if (!open) {
            title = t;
            break;
        }
        size_t at = t.rfind(open);
        if (at == std::string::npos || at == 0) {
            title = t;
            break;
        }
        found.insert(found.begin(), trim(t.substr(at + 1, t.size() - at - 2)));
        title = t.substr(0, at);
    }
    title = trim(title);
    if (title.empty()) title = stem;
    if (tags) {
        tags->clear();
        for (const auto& f : found)
            if (!f.empty()) *tags += (tags->empty() ? "" : " \xC2\xB7 ") + f;
    }
    return title;
}

namespace {
// Ids are stored in tab-separated statistics files.
std::string makeId(std::string id) {
    std::replace(id.begin(), id.end(), '\t', ' ');
    std::replace(id.begin(), id.end(), '\n', ' ');
    return id;
}

std::string fileCommand(const std::string& tmpl, const std::string& path) {
    std::string quoted = shellQuote(path);
    if (tmpl.empty()) return quoted;
    if (tmpl.find("%s") != std::string::npos) return replaceAll(tmpl, "%s", quoted);
    return tmpl + " " + quoted;
}

std::string extensionOf(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? "" : lower(name.substr(dot + 1));
}
}  // namespace

void Catalog::load() {
    uint64_t t0 = SDL_GetTicksNS();
    entries.clear();
    categories.clear();
    sources.clear();
    messages.clear();
    archives.clear();

    std::vector<Record> records;
    auto upsert = [&](Record rec) {
        for (auto& existing : records) {
            if (existing.key != rec.key) continue;
            messages.push_back({rec.file, rec.section.line,
                                strprintf("replaces the definition in %s:%d", existing.file.c_str(), existing.section.line)});
            existing = std::move(rec);
            return;
        }
        records.push_back(std::move(rec));
    };

    std::vector<std::string> files;
    for (const auto& pattern : platform.sources) {
        std::string expanded = paths.expand(pattern);
        if (expanded.find_first_of("*?[") != std::string::npos) {
            auto matches = glob(expanded);
            std::sort(matches.begin(), matches.end());
            files.insert(files.end(), matches.begin(), matches.end());
        } else {
            files.push_back(expanded);
        }
    }

    for (const auto& file : files) {
        CatalogSource src{file};
        IniFile ini;
        if (loadIni(file, ini)) {
            src.loaded = true;
            for (auto& sec : ini.sections) {
                if (sec.name.empty()) continue;
                Record rec;
                rec.file = file;
                if (iequals(sec.name, "ENTRY")) {
                    std::string name = sec.get("NAME"), command = sec.get("COMMAND");
                    if (name.empty() || command.empty()) {
                        messages.push_back({file, sec.line, "[ENTRY] without NAME or COMMAND, ignored"});
                        continue;
                    }
                    std::string cat = sec.get("CATEGORY");
                    rec.key = "E\x1f" + (cat.empty() ? "Applications" : cat) + "\x1f" + name;
                    ++src.entries;
                } else if (iequals(sec.name, "ARCHIVE")) {
                    std::string name = sec.get("NAME");
                    if (name.empty() || split(sec.get("ENTRY_DIRECTORIES"), ';').empty()) {
                        messages.push_back({file, sec.line, "[ARCHIVE] without NAME or ENTRY_DIRECTORIES, ignored"});
                        continue;
                    }
                    rec.archive = true;
                    rec.key = "A\x1f" + name;
                    ++src.archives;
                } else {
                    messages.push_back({file, sec.line, "unknown section [" + sec.name + "], ignored"});
                    continue;
                }
                rec.section = std::move(sec);
                upsert(std::move(rec));
            }
        }
        sources.push_back(src);
    }

    auto categoryFor = [&](const std::string& name) -> Category& {
        for (auto& c : categories)
            if (c.name == name) return c;
        Category c;
        c.name = name;
        c.color = colorFromName(name);
        c.glyph = guessGlyph(name);
        categories.push_back(std::move(c));
        return categories.back();
    };
    auto add = [&](Entry e) {
        Category& cat = categoryFor(e.category);
        for (int idx : cat.entries) {
            if (entries[idx].name == e.name) {
                entries[idx] = std::move(e);
                return;
            }
        }
        cat.entries.push_back((int)entries.size());
        entries.push_back(std::move(e));
    };

    for (const auto& rec : records) {
        const IniSection& s = rec.section;
        uint32_t color = 0;
        bool hasColor = parseColor(s.get("COLOR"), color);
        bool hidden = !s.get("HIDDEN").empty() && s.get("HIDDEN") != "0";

        if (!rec.archive) {
            Entry e;
            e.category = s.get("CATEGORY", "Applications");
            if (e.category.empty()) e.category = "Applications";
            e.name = s.get("NAME");
            e.title = e.name;
            e.description = s.get("DESCRIPTION");
            e.command = s.get("COMMAND");
            e.terminal = !s.get("TERMINAL").empty();
            e.art = paths.expand(s.get("ART"));
            if (e.art.empty()) e.art = paths.expand(s.get("ICON"));
            e.id = makeId("app:" + e.category + "/" + e.name);
            e.source = rec.file;
            e.line = s.line;
            e.hidden = hidden;
            Category& cat = categoryFor(e.category);
            if (hasColor) {
                e.color = color;
                cat.color = color;
            }
            add(std::move(e));
            continue;
        }

        std::string name = s.get("NAME");
        Category& cat = categoryFor(name);
        cat.archive = true;
        cat.description = s.get("DESCRIPTION");
        std::string defaultIcon = paths.expand(s.get("DEFAULT_ICON"));
        cat.image = defaultIcon;
        if (hasColor) cat.color = color;

        std::vector<std::string> exts;
        for (auto x : split(s.get("ENTRY_EXTENSIONS"), ';')) {
            if (!x.empty() && x[0] == '.') x.erase(0, 1);
            exts.push_back(lower(x));
        }
        ArchiveRule rule{name, exts, {}, s.get("COMMAND"), cat.color};
        for (const auto& dir : split(s.get("ENTRY_DIRECTORIES"), ';'))
            if (!dir.empty()) rule.directories.push_back(paths.expand(dir));
        archives.push_back(std::move(rule));

        ImageIndex art, icons;
        std::unordered_map<std::string, int> artRank, iconRank;
        for (const auto& d : split(s.get("ART_DIRECTORIES"), ';')) indexImages(paths.expand(d), art, artRank);
        for (const auto& d : split(s.get("ENTRY_ICONS_DIRECTORIES"), ';')) indexImages(paths.expand(d), icons, iconRank);

        std::vector<Entry> found;
        for (const auto& dir : split(s.get("ENTRY_DIRECTORIES"), ';')) {
            std::string full = paths.expand(dir);
            DIR* d = opendir(full.c_str());
            if (!d) continue;
            while (dirent* de = readdir(d)) {
                std::string fname = de->d_name;
                if (fname.empty() || fname[0] == '.') continue;
                std::string fpath = full + "/" + fname;
                if (de->d_type != DT_REG) {
                    if (de->d_type != DT_UNKNOWN && de->d_type != DT_LNK) continue;
                    struct stat st;
                    if (stat(fpath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                }
                size_t dot = fname.rfind('.');
                std::string ext = extensionOf(fname);
                if (!exts.empty() && std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;

                Entry e;
                e.category = name;
                e.name = dot == std::string::npos || dot == 0 ? fname : fname.substr(0, dot);
                e.title = cleanTitle(e.name, &e.tags);
                e.description = cat.description;
                e.path = dir + "/" + fname;
                e.command = fileCommand(s.get("COMMAND"), e.path);
                e.art = lookupImage(art, e.name);
                if (e.art.empty()) e.art = lookupImage(icons, e.name);
                if (e.art.empty() && hasEmbeddedCover(fname)) e.art = fpath;
                e.fallbackArt = defaultIcon;
                e.id = makeId("rom:" + e.path);
                e.source = rec.file;
                e.line = s.line;
                e.color = cat.color;
                e.fromArchive = true;
                e.hidden = hidden;
                found.push_back(std::move(e));
            }
            closedir(d);
        }
        std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) { return lower(a.name) < lower(b.name); });
        for (auto& e : found) add(std::move(e));
    }

    for (auto& c : categories) {
        if (!c.image.empty()) continue;
        if (!platform.systemIcons.empty()) {
            std::string guess = paths.expand(platform.systemIcons) + "/" + lower(c.name) + ".png";
            if (fileExists(guess)) c.image = guess;
        }
    }

    loadMs = (SDL_GetTicksNS() - t0) / 1e6;
    logf("catalog: %zu entries in %zu categories from %zu files, %.1f ms", entries.size(), categories.size(),
         files.size(), loadMs);
}

int Catalog::indexOf(const std::string& id) const {
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].id == id) return (int)i;
    return -1;
}

const Entry* Catalog::find(const std::string& id) const {
    int i = indexOf(id);
    return i < 0 ? nullptr : &entries[i];
}

const Category* Catalog::category(const std::string& name) const {
    for (const auto& c : categories)
        if (c.name == name) return &c;
    return nullptr;
}

std::vector<Entry> Catalog::entriesForFile(const std::string& path) const {
    for (const auto& e : entries)
        if (e.fromArchive && paths.expand(e.path) == path) return {e};
    std::string name = baseName(path), dir = dirName(path), ext = extensionOf(name);
    std::vector<Entry> local, other;
    for (const auto& r : archives) {
        bool inDir = std::find(r.directories.begin(), r.directories.end(), dir) != r.directories.end();
        bool takes = r.extensions.empty() ? inDir : std::find(r.extensions.begin(), r.extensions.end(), ext) != r.extensions.end();
        if (!takes) continue;
        Entry e;
        e.category = r.category;
        size_t dot = name.rfind('.');
        e.name = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
        e.title = cleanTitle(e.name, &e.tags);
        e.path = path;
        e.command = fileCommand(r.command, path);
        e.id = makeId("rom:" + path);
        e.color = r.color;
        e.fromArchive = true;
        (inDir ? local : other).push_back(std::move(e));
    }
    return local.empty() ? other : local;
}
