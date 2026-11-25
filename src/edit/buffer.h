#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace edit {

// Cursor position: line index and byte offset into the line (always at a code point boundary).
struct Pos {
    int line = 0, col = 0;

    bool operator==(const Pos&) const = default;
    auto operator<=>(const Pos&) const = default;
};

// UTF-8 text as lines without their terminators. The line ending (LF or CRLF, the majority on load),
// a byte order mark and a missing final newline are kept on save.
class Buffer {
public:
    static constexpr size_t kMaxBytes = 1 << 20;
    enum class Load { Ok, Missing, TooLarge, Failed };

    // Missing leaves an empty buffer for a new file. A file with invalid UTF-8 or a NUL byte loads
    // read-only.
    Load load(const std::string& path, std::string& error);
    void setText(std::string_view text);
    std::string text() const;
    // Writes a temporary file next to path, syncs it and renames it over path; keeps the mode bits.
    bool save(const std::string& path, std::string& error) const;

    Pos insert(Pos p, std::string_view s);
    Pos backspace(Pos p);
    Pos erase(Pos p);  // the code point after p, or the line break at its end
    Pos left(Pos p) const;
    Pos right(Pos p) const;
    Pos clamp(Pos p) const;
    Pos end() const { return {(int)lines.size() - 1, (int)lines.back().size()}; }

    // Next (or previous) occurrence starting after (before) p; ASCII case-insensitive unless matchCase.
    bool find(std::string_view needle, Pos p, bool forward, bool matchCase, Pos& at) const;
    // Start offsets of the non-overlapping matches in one line.
    std::vector<int> matches(int line, std::string_view needle, bool matchCase) const;
    // Replaces needle (no line breaks) at p when it matches there.
    bool replaceAt(Pos p, std::string_view needle, std::string_view with, bool matchCase);
    int replaceAll(std::string_view needle, std::string_view with, bool matchCase);

    std::vector<std::string> lines{""};
    bool crlf = false, bom = false, finalNewline = true;
    bool readOnly = false;
    std::string readOnlyReason;
};

bool validUtf8(std::string_view s);
// Byte length of the code point starting with lead byte c.
int utf8Len(unsigned char c);

}  // namespace edit
