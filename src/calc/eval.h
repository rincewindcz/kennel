#pragma once

#include <string>
#include <string_view>
#include <vector>

// Calculator expressions: a list of tokens as entered on the keypad. Digits, the decimal point and
// the exponent marker are single tokens and form numbers at evaluation; functions include their
// opening parenthesis, so one backspace removes "sin(".
namespace calc {

enum class Kind { Digit, Point, Exp, Op, Func, LParen, RParen, Const, Postfix, Number };
enum Op { Add, Sub, Mul, Div, Mod, Pow, Root, Choose, Permute };
enum Fn { Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Asinh, Acosh, Atanh, Ln, Log, Log2, Exp, Exp10, Exp2, Sqrt, Cbrt, Abs, Floor, Ceil };
enum Post { Factorial, Percent, Square, Cube, Inverse };
enum Const { Pi, Euler };

struct Token {
    Kind kind = Kind::Digit;
    int id = 0;
    std::string text;
    double value = 0;  // Kind::Number
};

Token digit(char c);
Token point();
Token exponent();
Token op(Op o);
Token func(Fn f);
Token postfix(Post p);
Token constant(Const c);
Token lparen();
Token rparen();
Token number(double v, std::string label = {});

struct Result {
    bool ok = false;
    double value = 0;
    std::string error;  // empty for an empty expression
    bool syntax = false;  // incomplete or malformed rather than a math error
};

Result evaluate(const std::vector<Token>& tokens, bool degrees);

// Text as typed on a keyboard ("2pi", "sin(30)", "5 nCr 2", "1E-3") to tokens; false on an unknown word.
bool parse(std::string_view text, std::vector<Token>& out);

// 12 significant digits, trailing zeros dropped, exponent form from 1e12 and below 1e-9. With
// `pretty`, digit groups are separated by thin spaces and minus is U+2212.
std::string format(double v, bool pretty = true);
std::string display(const std::vector<Token>& tokens);

// True when the tokens are more than a single number (worth a preview).
bool hasOperation(const std::vector<Token>& tokens);

}  // namespace calc
