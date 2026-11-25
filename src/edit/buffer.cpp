#include "buffer.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

namespace edit {

namespace {

char fold(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

bool matchAt(const std::string& line, size_t at, std::string_view needle, bool matchCase) {
    if (at + needle.size() > line.size()) return false;
    for (size_t i = 0; i < needle.size(); ++i) {
        char a = line[at + i], b = needle[i];
        if (matchCase ? a != b : fold(a) != fold(b)) return false;
    }
    return true;
}

// First match in line with start in [from, to), or npos.
size_t findIn(const std::string& line, std::string_view needle, size_t from, size_t to, bool matchCase) {
    for (size_t i = from; i < to && i + needle.size() <= line.size(); ++i)
        if (matchAt(line, i, needle, matchCase)) return i;
    return std::string::npos;
}

// Last match in line with start in [0, before), or npos.
size_t rfindIn(const std::string& line, std::string_view needle, size_t before, bool matchCase) {
    if (needle.size() > line.size()) return std::string::npos;
    for (size_t i = std::min(before, line.size() - needle.size() + 1); i-- > 0;)
        if (matchAt(line, i, needle, matchCase)) return i;
    return std::string::npos;
}

bool writeAll(int fd, std::string_view data) {
    while (!data.empty()) {
        ssize_t n = write(fd, data.data(), data.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data.remove_prefix((size_t)n);
    }
    return true;
}

}  // namespace

int utf8Len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

bool validUtf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        auto c = (unsigned char)s[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        int n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        auto c1 = (unsigned char)s[i + 1];
        if ((c == 0xe0 && c1 < 0xa0) || (c == 0xed && c1 > 0x9f) || (c == 0xf0 && c1 < 0x90) || (c == 0xf4 && c1 > 0x8f))
            return false;
        for (int k = 1; k < n; ++k)
            if (((unsigned char)s[i + k] & 0xc0) != 0x80) return false;
        i += n;
    }
    return true;
}

Buffer::Load Buffer::load(const std::string& path, std::string& error) {
    *this = Buffer{};
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (errno == ENOENT) return Load::Missing;
        error = strerror(errno);
        return Load::Failed;
    }
    if (S_ISDIR(st.st_mode)) {
        error = "Is a folder";
        return Load::Failed;
    }
    if ((uint64_t)st.st_size > kMaxBytes) {
        error = "The file is larger than 1 MiB";
        return Load::TooLarge;
    }
    auto data = readFile(path);
    if (!data) {
        error = strerror(errno);
        return Load::Failed;
    }
    if (data->size() > kMaxBytes) {
        error = "The file is larger than 1 MiB";
        return Load::TooLarge;
    }
    setText(*data);
    if (data->find('\0') != std::string::npos) readOnlyReason = "Binary file: read-only";
    else if (!validUtf8(*data)) readOnlyReason = "Not UTF-8 text: read-only";
    readOnly = !readOnlyReason.empty();
    if (access(path.c_str(), W_OK) != 0 && !readOnly) {
        readOnly = true;
        readOnlyReason = "No write permission: read-only";
    }
    return Load::Ok;
}

void Buffer::setText(std::string_view text) {
    bom = text.substr(0, 3) == "\xEF\xBB\xBF";
    if (bom) text.remove_prefix(3);
    size_t lf = 0, crlfCount = 0;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n') (i > 0 && text[i - 1] == '\r' ? crlfCount : lf)++;
    crlf = crlfCount > lf;
    finalNewline = !text.empty() && text.back() == '\n';
    lines.clear();
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        size_t end = nl > start && text[nl - 1] == '\r' ? nl - 1 : nl;
        lines.emplace_back(text.substr(start, end - start));
        start = nl + 1;
    }
    if (finalNewline && lines.size() > 1) lines.pop_back();
}

std::string Buffer::text() const {
    const char* eol = crlf ? "\r\n" : "\n";
    std::string out = bom ? "\xEF\xBB\xBF" : "";
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size() || finalNewline) out += eol;
    }
    return out;
}

bool Buffer::save(const std::string& path, std::string& error) const {
    // Writes go to the file a symbolic link points to, so the link stays.
    char resolved[PATH_MAX];
    std::string target = realpath(path.c_str(), resolved) ? resolved : path;
    std::string dir = dirName(target), tmp = dir + "/." + baseName(target) + ".kennel-edit.tmp";
    struct stat st;
    bool existed = stat(target.c_str(), &st) == 0;
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        error = strerror(errno);
        return false;
    }
    if (existed) fchmod(fd, st.st_mode & 07777);  // fails on vfat, which has no mode bits
    std::string data = text();
    bool ok = writeAll(fd, data) && fsync(fd) == 0;
    int saved = errno;
    if (close(fd) != 0 && ok) {
        ok = false;
        saved = errno;
    }
    if (!ok || rename(tmp.c_str(), target.c_str()) != 0) {
        error = strerror(ok ? errno : saved);
        unlink(tmp.c_str());
        return false;
    }
    int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        fsync(dfd);
        close(dfd);
    }
    return true;
}

Pos Buffer::clamp(Pos p) const {
    p.line = std::clamp(p.line, 0, (int)lines.size() - 1);
    const std::string& l = lines[p.line];
    p.col = std::clamp(p.col, 0, (int)l.size());
    while (p.col > 0 && p.col < (int)l.size() && ((unsigned char)l[p.col] & 0xc0) == 0x80) --p.col;
    return p;
}

Pos Buffer::insert(Pos p, std::string_view s) {
    p = clamp(p);
    std::string tail = lines[p.line].substr(p.col);
    lines[p.line].erase(p.col);
    size_t start = 0;
    while (true) {
        size_t nl = s.find('\n', start);
        std::string_view part = s.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        if (!part.empty() && part.back() == '\r' && nl != std::string_view::npos) part.remove_suffix(1);
        lines[p.line] += part;
        if (nl == std::string_view::npos) break;
        lines.insert(lines.begin() + p.line + 1, std::string());
        ++p.line;
        start = nl + 1;
    }
    p.col = (int)lines[p.line].size();
    lines[p.line] += tail;
    return p;
}

Pos Buffer::left(Pos p) const {
    p = clamp(p);
    if (p.col == 0) return p.line > 0 ? Pos{p.line - 1, (int)lines[p.line - 1].size()} : p;
    const std::string& l = lines[p.line];
    do --p.col;
    while (p.col > 0 && ((unsigned char)l[p.col] & 0xc0) == 0x80);
    return p;
}

Pos Buffer::right(Pos p) const {
    p = clamp(p);
    const std::string& l = lines[p.line];
    if (p.col >= (int)l.size()) return p.line + 1 < (int)lines.size() ? Pos{p.line + 1, 0} : p;
    p.col = std::min((int)l.size(), p.col + utf8Len((unsigned char)l[p.col]));
    return p;
}

Pos Buffer::backspace(Pos p) {
    p = clamp(p);
    if (p.col == 0 && p.line == 0) return p;
    Pos from = left(p);
    if (from.line != p.line) {
        lines[from.line] += lines[p.line];
        lines.erase(lines.begin() + p.line);
    } else {
        lines[p.line].erase(from.col, p.col - from.col);
    }
    return from;
}

Pos Buffer::erase(Pos p) {
    p = clamp(p);
    Pos to = right(p);
    if (to == p) return p;
    if (to.line != p.line) {
        lines[p.line] += lines[to.line];
        lines.erase(lines.begin() + to.line);
    } else {
        lines[p.line].erase(p.col, to.col - p.col);
    }
    return p;
}

bool Buffer::find(std::string_view needle, Pos p, bool forward, bool matchCase, Pos& at) const {
    if (needle.empty()) return false;
    p = clamp(p);
    int n = (int)lines.size();
    if (forward) {
        for (int k = 0; k <= n; ++k) {
            int li = (p.line + k) % n;
            const std::string& l = lines[li];
            size_t from = k == 0 ? (size_t)p.col : 0, to = k == n ? (size_t)p.col : l.size();
            size_t i = findIn(l, needle, from, to, matchCase);
            if (i != std::string::npos) {
                at = {li, (int)i};
                return true;
            }
        }
    } else {
        for (int k = 0; k <= n; ++k) {
            int li = ((p.line - k) % n + n) % n;
            const std::string& l = lines[li];
            size_t before = k == 0 ? (size_t)p.col : l.size();
            size_t i = rfindIn(l, needle, before, matchCase);
            if (i != std::string::npos && (k < n || i >= (size_t)p.col)) {
                at = {li, (int)i};
                return true;
            }
        }
    }
    return false;
}

std::vector<int> Buffer::matches(int line, std::string_view needle, bool matchCase) const {
    std::vector<int> out;
    if (needle.empty() || line < 0 || line >= (int)lines.size()) return out;
    const std::string& l = lines[line];
    for (size_t i = 0; (i = findIn(l, needle, i, l.size(), matchCase)) != std::string::npos; i += needle.size())
        out.push_back((int)i);
    return out;
}

bool Buffer::replaceAt(Pos p, std::string_view needle, std::string_view with, bool matchCase) {
    if (needle.empty() || p.line < 0 || p.line >= (int)lines.size() || !matchAt(lines[p.line], p.col, needle, matchCase))
        return false;
    lines[p.line].replace(p.col, needle.size(), with);
    return true;
}

int Buffer::replaceAll(std::string_view needle, std::string_view with, bool matchCase) {
    if (needle.empty()) return 0;
    int count = 0;
    for (auto& l : lines) {
        size_t i = 0;
        while ((i = findIn(l, needle, i, l.size(), matchCase)) != std::string::npos) {
            l.replace(i, needle.size(), with);
            i += with.size();
            ++count;
        }
    }
    return count;
}

}  // namespace edit
