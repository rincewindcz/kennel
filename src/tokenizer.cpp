#include "tokenizer.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "util.h"

namespace markup {

namespace {

struct NamedEntity {
    const char* name;
    uint32_t cp;
};

const NamedEntity kEntities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
    {"shy", 0xAD}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"lsquo", 0x2018},
    {"rsquo", 0x2019}, {"sbquo", 0x201A}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E},
    {"laquo", 0xAB}, {"raquo", 0xBB}, {"lsaquo", 0x2039}, {"rsaquo", 0x203A}, {"copy", 0xA9},
    {"reg", 0xAE}, {"trade", 0x2122}, {"deg", 0xB0}, {"middot", 0xB7}, {"bull", 0x2022},
    {"times", 0xD7}, {"divide", 0xF7}, {"para", 0xB6}, {"sect", 0xA7}, {"iexcl", 0xA1},
    {"iquest", 0xBF}, {"frac12", 0xBD}, {"frac14", 0xBC}, {"frac34", 0xBE}, {"sup1", 0xB9},
    {"sup2", 0xB2}, {"sup3", 0xB3}, {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5},
    {"cent", 0xA2}, {"curren", 0xA4}, {"brvbar", 0xA6}, {"uml", 0xA8}, {"ordf", 0xAA},
    {"not", 0xAC}, {"macr", 0xAF}, {"plusmn", 0xB1}, {"acute", 0xB4}, {"micro", 0xB5},
    {"cedil", 0xB8}, {"ordm", 0xBA}, {"dagger", 0x2020}, {"Dagger", 0x2021}, {"permil", 0x2030},
    {"prime", 0x2032}, {"Prime", 0x2033}, {"oline", 0x203E}, {"thinsp", 0x2009}, {"ensp", 0x2002},
    {"emsp", 0x2003}, {"zwnj", 0x200C}, {"zwj", 0x200D}, {"lrm", 0x200E}, {"rlm", 0x200F},
    {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193}, {"hearts", 0x2665},
    {"Agrave", 0xC0}, {"Aacute", 0xC1}, {"Acirc", 0xC2}, {"Atilde", 0xC3}, {"Auml", 0xC4},
    {"Aring", 0xC5}, {"AElig", 0xC6}, {"Ccedil", 0xC7}, {"Egrave", 0xC8}, {"Eacute", 0xC9},
    {"Ecirc", 0xCA}, {"Euml", 0xCB}, {"Igrave", 0xCC}, {"Iacute", 0xCD}, {"Icirc", 0xCE},
    {"Iuml", 0xCF}, {"ETH", 0xD0}, {"Ntilde", 0xD1}, {"Ograve", 0xD2}, {"Oacute", 0xD3},
    {"Ocirc", 0xD4}, {"Otilde", 0xD5}, {"Ouml", 0xD6}, {"Oslash", 0xD8}, {"Ugrave", 0xD9},
    {"Uacute", 0xDA}, {"Ucirc", 0xDB}, {"Uuml", 0xDC}, {"Yacute", 0xDD}, {"THORN", 0xDE},
    {"szlig", 0xDF}, {"agrave", 0xE0}, {"aacute", 0xE1}, {"acirc", 0xE2}, {"atilde", 0xE3},
    {"auml", 0xE4}, {"aring", 0xE5}, {"aelig", 0xE6}, {"ccedil", 0xE7}, {"egrave", 0xE8},
    {"eacute", 0xE9}, {"ecirc", 0xEA}, {"euml", 0xEB}, {"igrave", 0xEC}, {"iacute", 0xED},
    {"icirc", 0xEE}, {"iuml", 0xEF}, {"eth", 0xF0}, {"ntilde", 0xF1}, {"ograve", 0xF2},
    {"oacute", 0xF3}, {"ocirc", 0xF4}, {"otilde", 0xF5}, {"ouml", 0xF6}, {"oslash", 0xF8},
    {"ugrave", 0xF9}, {"uacute", 0xFA}, {"ucirc", 0xFB}, {"uuml", 0xFC}, {"yacute", 0xFD},
    {"thorn", 0xFE}, {"yuml", 0xFF}, {"OElig", 0x152}, {"oelig", 0x153}, {"Scaron", 0x160},
    {"scaron", 0x161}, {"Yuml", 0x178}, {"Zcaron", 0x17D}, {"zcaron", 0x17E}, {"fnof", 0x192},
    {"circ", 0x2C6}, {"tilde", 0x2DC}, {"alpha", 0x3B1}, {"beta", 0x3B2}, {"gamma", 0x3B3},
    {"delta", 0x3B4}, {"pi", 0x3C0}, {"mu", 0x3BC}, {"Omega", 0x3A9}, {"infin", 0x221E},
    {"minus", 0x2212}, {"le", 0x2264}, {"ge", 0x2265}, {"ne", 0x2260}, {"asymp", 0x2248},
};

// cp1252 0x80-0x9F; 0 marks undefined bytes (mapped to U+FFFD).
const uint16_t kCp1252[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

bool nameChar(char c) {
    return std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':' || c == '.' || (unsigned char)c >= 0x80;
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

bool startsWithNoCase(std::string_view s, size_t at, std::string_view what) {
    if (at + what.size() > s.size()) return false;
    for (size_t i = 0; i < what.size(); ++i)
        if (std::tolower((unsigned char)s[at + i]) != std::tolower((unsigned char)what[i])) return false;
    return true;
}

}  // namespace

const std::string* Token::attr(std::string_view key) const {
    for (const auto& [k, v] : attrs)
        if (k == key) return &v;
    for (const auto& [k, v] : attrs) {
        size_t colon = k.find(':');
        if (colon != std::string::npos && std::string_view(k).substr(colon + 1) == key) return &v;
    }
    return nullptr;
}

std::string Token::local() const {
    size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) {
        out += (char)(0xC0 | cp >> 6);
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | cp >> 12);
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | cp >> 18);
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// Length of the valid UTF-8 sequence at s[i], or 0.
static size_t utf8Length(std::string_view s, size_t i) {
    unsigned char c = s[i];
    if (c < 0x80) return 1;
    size_t n = c >= 0xF0 && c <= 0xF4 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c <= 0xDF ? 2 : 0;
    if (n == 0 || i + n > s.size()) return 0;
    for (size_t k = 1; k < n; ++k)
        if (((unsigned char)s[i + k] & 0xC0) != 0x80) return 0;
    unsigned char c1 = s[i + 1];
    if (c == 0xE0 && c1 < 0xA0) return 0;
    if (c == 0xED && c1 >= 0xA0) return 0;
    if (c == 0xF0 && c1 < 0x90) return 0;
    if (c == 0xF4 && c1 >= 0x90) return 0;
    return n;
}

bool validUtf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        uint64_t block;
        if (i + 8 <= s.size() && (memcpy(&block, s.data() + i, 8), (block & 0x8080808080808080ull) == 0)) {
            i += 8;
            continue;
        }
        size_t n = utf8Length(s, i);
        if (!n) return false;
        i += n;
    }
    return true;
}

std::string sanitizeUtf8(std::string_view s) {
    if (validUtf8(s)) return std::string(s);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        size_t n = utf8Length(s, i);
        if (n) {
            out.append(s.substr(i, n));
            i += n;
        } else {
            out += "\xEF\xBF\xBD";
            ++i;
        }
    }
    return out;
}

std::string cp1252ToUtf8(std::string_view s) {
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (unsigned char c : s) {
        if (c < 0x80) out += (char)c;
        else if (c < 0xA0) appendUtf8(out, kCp1252[c - 0x80] ? kCp1252[c - 0x80] : 0xFFFD);
        else appendUtf8(out, c);
    }
    return out;
}

std::string decodeEntities(std::string_view s) {
    if (s.find('&') == std::string_view::npos) return std::string(s);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 12) {
            out += '&';
            continue;
        }
        std::string_view name = s.substr(i + 1, semi - i - 1);
        uint32_t cp = 0;
        if (!name.empty() && name[0] == '#') {
            bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            std::string digits(name.substr(hex ? 2 : 1));
            char* end = nullptr;
            unsigned long v = digits.empty() ? 0 : strtoul(digits.c_str(), &end, hex ? 16 : 10);
            if (!digits.empty() && end && !*end) cp = v > 0x10FFFF ? 0xFFFD : (uint32_t)v;
        } else {
            for (const auto& e : kEntities)
                if (name == e.name) {
                    cp = e.cp;
                    break;
                }
        }
        if (!cp) {
            out += '&';
            continue;
        }
        if (cp >= 0x80 && cp < 0xA0) cp = kCp1252[cp - 0x80] ? kCp1252[cp - 0x80] : 0xFFFD;  // common mislabelled refs
        if (cp != 0xAD) appendUtf8(out, cp);
        i = semi;
    }
    return out;
}

bool Tokenizer::next(Token& t) {
    t.attrs.clear();
    t.name.clear();
    t.text.clear();
    const std::string_view s = s_;
    if (pos_ >= s.size()) return false;

    if (!rawEnd_.empty()) {
        size_t end = pos_;
        while (end < s.size() && !(s[end] == '<' && startsWithNoCase(s, end, rawEnd_))) ++end;
        t.type = Token::Type::Text;
        t.text = std::string(s.substr(pos_, end - pos_));
        pos_ = end;
        rawEnd_.clear();
        if (!t.text.empty()) return true;
        if (pos_ >= s.size()) return false;
    }

    while (pos_ < s.size()) {
        if (s[pos_] != '<') {
            size_t end = s.find('<', pos_);
            if (end == std::string_view::npos) end = s.size();
            t.type = Token::Type::Text;
            t.text = decodeEntities(s.substr(pos_, end - pos_));
            pos_ = end;
            return true;
        }
        if (s.compare(pos_, 4, "<!--") == 0) {
            size_t end = s.find("-->", pos_ + 4);
            pos_ = end == std::string_view::npos ? s.size() : end + 3;
            continue;
        }
        if (s.compare(pos_, 9, "<![CDATA[") == 0) {
            size_t end = s.find("]]>", pos_ + 9);
            size_t stop = end == std::string_view::npos ? s.size() : end;
            t.type = Token::Type::Text;
            t.text = std::string(s.substr(pos_ + 9, stop - pos_ - 9));
            pos_ = end == std::string_view::npos ? s.size() : end + 3;
            return true;
        }
        if (s.compare(pos_, 2, "<!") == 0 || s.compare(pos_, 2, "<?") == 0) {
            size_t end = s.find('>', pos_ + 2);
            pos_ = end == std::string_view::npos ? s.size() : end + 1;
            continue;
        }
        bool close = pos_ + 1 < s.size() && s[pos_ + 1] == '/';
        size_t p = pos_ + (close ? 2 : 1);
        if (p >= s.size() || !(std::isalpha((unsigned char)s[p]) || s[p] == '_')) {
            t.type = Token::Type::Text;
            t.text = "<";
            ++pos_;
            return true;
        }
        size_t nameStart = p;
        while (p < s.size() && nameChar(s[p])) ++p;
        t.name = lowerAscii(s.substr(nameStart, p - nameStart));
        bool self = false;
        while (p < s.size() && s[p] != '>') {
            if (s[p] == '/' && p + 1 < s.size() && s[p + 1] == '>') {
                self = true;
                ++p;
                break;
            }
            if (!nameChar(s[p])) {
                ++p;
                continue;
            }
            size_t a = p;
            while (p < s.size() && nameChar(s[p])) ++p;
            std::string key = lowerAscii(s.substr(a, p - a));
            while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
            std::string value;
            if (p < s.size() && s[p] == '=') {
                ++p;
                while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
                if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
                    char q = s[p++];
                    size_t e = s.find(q, p);
                    if (e == std::string_view::npos) e = s.size();
                    value = decodeEntities(s.substr(p, e - p));
                    p = std::min(e + 1, s.size());
                } else {
                    size_t e = p;
                    while (e < s.size() && !std::isspace((unsigned char)s[e]) && s[e] != '>') ++e;
                    value = decodeEntities(s.substr(p, e - p));
                    p = e;
                }
            }
            if (!close) t.attrs.emplace_back(std::move(key), std::move(value));
        }
        pos_ = std::min(p + 1, s.size());
        t.type = close ? Token::Type::Close : self ? Token::Type::SelfClose : Token::Type::Open;
        if (t.type == Token::Type::Open && (t.name == "script" || t.name == "style")) rawEnd_ = "</" + t.name;
        return true;
    }
    return false;
}

}  // namespace markup
