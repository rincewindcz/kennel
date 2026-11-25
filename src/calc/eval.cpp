#include "eval.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace calc {

namespace {

constexpr double kPi = 3.14159265358979323846;

const char* const kOpText[] = {"+", "\xE2\x88\x92", "\xC3\x97", "\xC3\xB7", " mod ", "^", "\xCA\xB8\xE2\x88\x9A", " nCr ", " nPr "};
const char* const kFnText[] = {
    "sin(", "cos(", "tan(", "sin\xE2\x81\xBB\xC2\xB9(", "cos\xE2\x81\xBB\xC2\xB9(", "tan\xE2\x81\xBB\xC2\xB9(",
    "sinh(", "cosh(", "tanh(", "sinh\xE2\x81\xBB\xC2\xB9(", "cosh\xE2\x81\xBB\xC2\xB9(", "tanh\xE2\x81\xBB\xC2\xB9(",
    "ln(", "log(", "log\xE2\x82\x82(", "e^(", "10^(", "2^(", "\xE2\x88\x9A(", "\xC2\xB3\xE2\x88\x9A(", "abs(", "floor(", "ceil("};
const char* const kFnWord[] = {"sin",   "cos",   "tan", "asin", "acos", "atan", "sinh", "cosh",
                               "tanh",  "asinh", "acosh", "atanh", "ln", "log", "log2", "exp",
                               "exp10", "exp2",  "sqrt", "cbrt", "abs", "floor", "ceil"};
const char* const kPostText[] = {"!", "%", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xBB\xC2\xB9"};
const char* const kConstText[] = {"\xCF\x80", "e"};

struct Fail {
    const char* message;
    bool syntax;
};

[[noreturn]] void syntax() { throw Fail{"Syntax error", true}; }
[[noreturn]] void domain() { throw Fail{"Domain error", false}; }
[[noreturn]] void divZero() { throw Fail{"Division by zero", false}; }

double check(double v) {
    if (std::isnan(v)) domain();
    if (std::isinf(v)) throw Fail{"Overflow", false};
    return v;
}

struct Lexeme {
    enum Type { Num, Op, Func, LParen, RParen, Const, Post } type;
    int id = 0;
    double value = 0;
};

bool endsValue(const Lexeme& l) {
    return l.type == Lexeme::Num || l.type == Lexeme::Const || l.type == Lexeme::RParen || l.type == Lexeme::Post;
}

bool startsValue(const Lexeme& l) {
    return l.type == Lexeme::Num || l.type == Lexeme::Const || l.type == Lexeme::Func || l.type == Lexeme::LParen;
}

bool numberPart(const std::vector<Token>& t, size_t i) {
    Kind k = t[i].kind;
    if (k == Kind::Digit || k == Kind::Point || k == Kind::Exp) return true;
    // the sign of an exponent: 1E-5
    return k == Kind::Op && (t[i].id == Add || t[i].id == Sub) && i > 0 && t[i - 1].kind == Kind::Exp &&
           i + 1 < t.size() && t[i + 1].kind == Kind::Digit;
}

std::vector<Lexeme> lex(const std::vector<Token>& tokens) {
    std::vector<Lexeme> out;
    auto push = [&](Lexeme l) {
        if (!out.empty() && endsValue(out.back()) && startsValue(l)) out.push_back({Lexeme::Op, Mul});
        out.push_back(l);
    };
    for (size_t i = 0; i < tokens.size();) {
        const Token& t = tokens[i];
        if (numberPart(tokens, i)) {
            std::string s;
            while (i < tokens.size() && numberPart(tokens, i)) {
                const Token& p = tokens[i++];
                s += p.kind == Kind::Exp ? "e" : p.kind == Kind::Op ? (p.id == Sub ? "-" : "+") : p.text;
            }
            char* end = nullptr;
            double v = strtod(s.c_str(), &end);
            if (end != s.c_str() + s.size()) syntax();
            push({Lexeme::Num, 0, v});
            continue;
        }
        switch (t.kind) {
            case Kind::Number: push({Lexeme::Num, 0, t.value}); break;
            case Kind::Op: push({Lexeme::Op, t.id}); break;
            case Kind::Func: push({Lexeme::Func, t.id}); break;
            case Kind::LParen: push({Lexeme::LParen}); break;
            case Kind::RParen: push({Lexeme::RParen}); break;
            case Kind::Const: push({Lexeme::Const, t.id}); break;
            case Kind::Postfix: push({Lexeme::Post, t.id}); break;
            default: syntax();
        }
        ++i;
    }
    return out;
}

bool integral(double v) { return std::floor(v) == v; }

double factorial(double v) {
    if (v < 0 || !integral(v)) domain();
    if (v > 170) throw Fail{"Overflow", false};
    double r = 1;
    for (int i = 2; i <= (int)v; ++i) r *= i;
    return r;
}

double choose(double n, double r, bool permute) {
    if (n < 0 || r < 0 || !integral(n) || !integral(r) || r > n) domain();
    double out = 1;
    for (int i = 0; i < (int)r; ++i) out = permute ? out * (n - i) : out * (n - r + 1 + i) / (i + 1);
    return check(permute ? out : std::round(out));
}

// Sine and cosine of multiples of 90 degrees are exact; results within rounding of zero are zero.
double snap(double r, double x) { return std::fabs(r) < 1e-15 * std::fabs(x) ? 0 : r; }

double degreeTrig(int fn, double x) {
    double r = std::fmod(x, 360);
    if (r < 0) r += 360;
    switch (fn) {
        case Sin:
            if (r == 0 || r == 180) return 0;
            if (r == 90) return 1;
            if (r == 270) return -1;
            return std::sin(r * kPi / 180);
        case Cos:
            if (r == 90 || r == 270) return 0;
            if (r == 0) return 1;
            if (r == 180) return -1;
            return std::cos(r * kPi / 180);
        default:
            if (std::fmod(r, 180) == 0) return 0;
            if (std::fmod(r, 180) == 90) domain();
            return std::tan(r * kPi / 180);
    }
}

double apply(int fn, double x, bool degrees) {
    double toDeg = degrees ? 180 / kPi : 1;
    switch (fn) {
        case Sin:
        case Cos:
        case Tan:
            if (degrees) return degreeTrig(fn, x);
            return snap(fn == Sin ? std::sin(x) : fn == Cos ? std::cos(x) : std::tan(x), x);
        case Asin:
        case Acos:
            if (x < -1 || x > 1) domain();
            return (fn == Asin ? std::asin(x) : std::acos(x)) * toDeg;
        case Atan: return std::atan(x) * toDeg;
        case Sinh: return std::sinh(x);
        case Cosh: return std::cosh(x);
        case Tanh: return std::tanh(x);
        case Asinh: return std::asinh(x);
        case Acosh:
            if (x < 1) domain();
            return std::acosh(x);
        case Atanh:
            if (x <= -1 || x >= 1) domain();
            return std::atanh(x);
        case Ln:
        case Log:
        case Log2:
            if (x <= 0) domain();
            return fn == Ln ? std::log(x) : fn == Log ? std::log10(x) : std::log2(x);
        case Exp: return std::exp(x);
        case Exp10: return std::pow(10, x);
        case Exp2: return std::exp2(x);
        case Sqrt:
            if (x < 0) domain();
            return std::sqrt(x);
        case Cbrt: return std::cbrt(x);
        case Abs: return std::fabs(x);
        case Floor: return std::floor(x);
        case Ceil: return std::ceil(x);
        default: syntax();
    }
}

double power(double a, double b) {
    if (a == 0 && b < 0) divZero();
    return std::pow(a, b);
}

double root(double a, double n) {
    if (n == 0) domain();
    if (a < 0) {
        if (!integral(n) || std::fmod(std::fabs(n), 2) != 1) domain();
        return -std::pow(-a, 1 / n);
    }
    return std::pow(a, 1 / n);
}

// Recursive descent, lowest precedence first. `percent` marks a term whose outermost operation is %,
// so that a + b% adds b percent of a.
class Parser {
public:
    Parser(const std::vector<Lexeme>& l, bool degrees) : l_(l), degrees_(degrees) {}

    double run() {
        if (l_.empty()) throw Fail{"", true};
        double v = expr().v;
        if (i_ != l_.size()) syntax();
        return check(v);
    }

private:
    struct Val {
        double v = 0;
        bool percent = false;
        double raw = 0;
    };

    bool atOp(int id) const { return i_ < l_.size() && l_[i_].type == Lexeme::Op && l_[i_].id == id; }

    Val expr() {
        Val a = term();
        while (atOp(Add) || atOp(Sub)) {
            bool add = l_[i_++].id == Add;
            Val b = term();
            if (b.percent) b.v = a.v * b.raw / 100;
            a = {check(add ? a.v + b.v : a.v - b.v)};
        }
        return a;
    }

    Val term() {
        Val a = unary();
        while (atOp(Mul) || atOp(Div) || atOp(Mod) || atOp(Choose) || atOp(Permute)) {
            int o = l_[i_++].id;
            Val b = unary();
            double r;
            switch (o) {
                case Mul: r = a.v * b.v; break;
                case Div:
                    if (b.v == 0) divZero();
                    r = a.v / b.v;
                    break;
                case Mod:
                    if (b.v == 0) divZero();
                    r = std::fmod(a.v, b.v);
                    break;
                default: r = choose(a.v, b.v, o == Permute);
            }
            a = {check(r)};
        }
        return a;
    }

    Val unary() {
        if (atOp(Sub) || atOp(Add)) {
            bool neg = l_[i_++].id == Sub;
            Val v = unary();
            if (neg) {
                v.v = -v.v;
                v.raw = -v.raw;
            }
            return v;
        }
        return pow();
    }

    Val pow() {
        Val b = post();
        if (atOp(Pow) || atOp(Root)) {
            bool isRoot = l_[i_++].id == Root;
            Val e = unary();
            return {check(isRoot ? root(b.v, e.v) : power(b.v, e.v))};
        }
        return b;
    }

    Val post() {
        Val v = primary();
        while (i_ < l_.size() && l_[i_].type == Lexeme::Post) {
            int p = l_[i_++].id;
            double x = v.v;
            v = {};
            switch (p) {
                case Factorial: v.v = factorial(x); break;
                case Percent:
                    v.v = x / 100;
                    v.percent = true;
                    v.raw = x;
                    break;
                case Square: v.v = check(x * x); break;
                case Cube: v.v = check(x * x * x); break;
                default:
                    if (x == 0) divZero();
                    v.v = 1 / x;
            }
        }
        return v;
    }

    Val primary() {
        if (i_ >= l_.size()) syntax();
        const Lexeme& l = l_[i_++];
        switch (l.type) {
            case Lexeme::Num: return {l.value};
            case Lexeme::Const: return {l.id == Pi ? kPi : std::exp(1.0)};
            case Lexeme::LParen: return {group()};
            case Lexeme::Func: return {check(apply(l.id, group(), degrees_))};
            default: syntax();
        }
    }

    // After an opening parenthesis; a missing closing one at the end is implied.
    double group() {
        double v = expr().v;
        if (i_ < l_.size()) {
            if (l_[i_].type != Lexeme::RParen) syntax();
            ++i_;
        }
        return v;
    }

    const std::vector<Lexeme>& l_;
    bool degrees_;
    size_t i_ = 0;
};

void stripZeros(std::string& s) {
    if (s.find('.') == std::string::npos) return;
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
}

std::string group(const std::string& digits) {
    if (digits.size() <= 3) return digits;
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += "\xE2\x80\x89";
        out += digits[i];
    }
    return out;
}

}  // namespace

Token digit(char c) { return {Kind::Digit, 0, std::string(1, c)}; }
Token point() { return {Kind::Point, 0, "."}; }
Token exponent() { return {Kind::Exp, 0, "E"}; }
Token op(Op o) { return {Kind::Op, o, kOpText[o]}; }
Token func(Fn f) { return {Kind::Func, f, kFnText[f]}; }
Token postfix(Post p) { return {Kind::Postfix, p, kPostText[p]}; }
Token constant(Const c) { return {Kind::Const, c, kConstText[c]}; }
Token lparen() { return {Kind::LParen, 0, "("}; }
Token rparen() { return {Kind::RParen, 0, ")"}; }
Token number(double v, std::string label) { return {Kind::Number, 0, label.empty() ? format(v) : std::move(label), v}; }

Result evaluate(const std::vector<Token>& tokens, bool degrees) {
    Result r;
    try {
        r.value = Parser(lex(tokens), degrees).run();
        if (r.value == 0) r.value = 0;  // no negative zero
        r.ok = true;
    } catch (const Fail& f) {
        r.error = f.message;
        r.syntax = f.syntax;
    }
    return r;
}

bool parse(std::string_view s, std::vector<Token>& out) {
    auto starts = [&](size_t i, std::string_view p) { return s.substr(i, p.size()) == p; };
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == ' ') {
            ++i;
            continue;
        }
        if (c >= '0' && c <= '9') {
            out.push_back(digit(c));
            ++i;
            continue;
        }
        static const std::pair<std::string_view, Token (*)()> symbols[] = {
            {".", point},
            {",", point},
            {"(", lparen},
            {")", rparen},
            {"+", [] { return op(Add); }},
            {"-", [] { return op(Sub); }},
            {"\xE2\x88\x92", [] { return op(Sub); }},
            {"*", [] { return op(Mul); }},
            {"\xC3\x97", [] { return op(Mul); }},
            {"/", [] { return op(Div); }},
            {"\xC3\xB7", [] { return op(Div); }},
            {"^", [] { return op(Pow); }},
            {"%", [] { return postfix(Percent); }},
            {"!", [] { return postfix(Factorial); }},
            {"\xC2\xB2", [] { return postfix(Square); }},
            {"\xC2\xB3", [] { return postfix(Cube); }},
            {"\xCF\x80", [] { return constant(Pi); }},
            {"\xE2\x88\x9A", [] { return func(Sqrt); }},
        };
        bool matched = false;
        for (const auto& [text, make] : symbols) {
            if (!starts(i, text)) continue;
            out.push_back(make());
            i += text.size();
            matched = true;
            break;
        }
        if (matched) {
            if (out.back().kind == Kind::Func && i < s.size() && s[i] == '(') ++i;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
        size_t j = i;
        while (j < s.size() && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) ++j;
        std::string word(s.substr(i, j - i));
        if (word == "log" && j < s.size() && s[j] == '2') word = "log2", ++j;
        i = j;
        if (word == "E") {
            out.push_back(exponent());
            continue;
        }
        for (auto& ch : word) ch = (char)std::tolower((unsigned char)ch);
        if (word == "pi") out.push_back(constant(Pi));
        else if (word == "e") out.push_back(constant(Euler));
        else if (word == "mod") out.push_back(op(Mod));
        else if (word == "ncr") out.push_back(op(Choose));
        else if (word == "npr") out.push_back(op(Permute));
        else {
            auto f = std::find_if(std::begin(kFnWord), std::end(kFnWord), [&](const char* w) { return word == w; });
            if (f == std::end(kFnWord)) return false;
            out.push_back(func((Fn)(f - std::begin(kFnWord))));
            while (i < s.size() && s[i] == ' ') ++i;
            if (i < s.size() && s[i] == '(') ++i;
        }
    }
    return true;
}

std::string format(double v, bool pretty) {
    if (v == 0) return "0";
    double a = std::fabs(v);
    char buf[64];
    std::string s, exp;
    if (a >= 1e12 || a < 1e-9) {
        snprintf(buf, sizeof buf, "%.11e", v);
        s = buf;
        size_t e = s.find('e');
        exp = "e" + std::to_string(atoi(s.c_str() + e + 1));
        s.resize(e);
    } else {
        int mag = (int)std::floor(std::log10(a));
        snprintf(buf, sizeof buf, "%.*f", std::clamp(11 - mag, 0, 20), v);
        s = buf;
    }
    stripZeros(s);
    s += exp;
    if (!pretty) return s;
    bool neg = s[0] == '-';
    size_t start = neg ? 1 : 0, end = s.find_first_of(".e", start);
    if (end == std::string::npos) end = s.size();
    std::string out = (neg ? "\xE2\x88\x92" : "") + group(s.substr(start, end - start)) + s.substr(end);
    size_t minus = out.find("e-");
    if (minus != std::string::npos) out.replace(minus + 1, 1, "\xE2\x88\x92");
    return out;
}

std::string display(const std::vector<Token>& tokens) {
    std::string out, run;
    bool fraction = false;  // digits after a point or an exponent marker are not grouped
    auto flush = [&] {
        out += fraction ? run : group(run);
        run.clear();
    };
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.kind == Kind::Digit) {
            run += t.text;
            continue;
        }
        flush();
        if (t.kind == Kind::Point || t.kind == Kind::Exp) fraction = true;
        else if (!numberPart(tokens, i)) fraction = false;
        out += t.text;
    }
    flush();
    return out;
}

bool hasOperation(const std::vector<Token>& tokens) {
    for (size_t i = 0; i < tokens.size(); ++i) {
        Kind k = tokens[i].kind;
        if (k == Kind::Digit || k == Kind::Point || k == Kind::Exp || k == Kind::Number || numberPart(tokens, i)) continue;
        return true;
    }
    return false;
}

}  // namespace calc
