#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace markup {

struct Token {
    enum class Type { Text, Open, Close, SelfClose };
    Type type = Type::Text;
    std::string name;  // lowercased, namespace prefix kept (dc:title, epub:type)
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;  // decoded text (Text tokens)

    const std::string* attr(std::string_view key) const;
    std::string local() const;  // name without namespace prefix
};

// Tolerant HTML/XHTML/XML tokenizer: never fails, malformed markup becomes text or is skipped.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view src) : s_(src) {}
    bool next(Token& t);

private:
    std::string_view s_;
    size_t pos_ = 0;
    std::string rawEnd_;  // set after <script>/<style>: their content is raw text
};

std::string decodeEntities(std::string_view s);
void appendUtf8(std::string& out, uint32_t cp);
bool validUtf8(std::string_view s);
std::string sanitizeUtf8(std::string_view s);  // invalid sequences -> U+FFFD
std::string cp1252ToUtf8(std::string_view s);

}  // namespace markup
