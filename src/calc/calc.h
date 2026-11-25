#pragma once

#include <random>
#include <string>
#include <vector>

#include "eval.h"
#include "input.h"

class Calc {
public:
    int run(int argc, char** argv);

private:
    enum class Act { Token, Clear, Back, Equals, Negate, MemClear, MemRecall, MemAdd, MemSub, Second, Angle, Ans, Rand };
    enum class Style { Digit, Function, Equals };
    struct Pad {
        std::string label, label2, icon;  // label2: label with 2nd latched, empty when the key does not change
        Act act = Act::Token;
        calc::Token token, token2;
        Style style = Style::Function;
    };
    using Grid = std::vector<std::vector<Pad>>;

    void buildGrids();
    const Grid& grid() const { return scientific_ ? sci_ : basic_; }
    void loadSettings();
    void saveSettings();

    void press(const Pad& p);
    void insert(calc::Token t);
    void negate();
    void back();
    void clear();
    void equals();
    void refresh();
    bool currentValue(double& out) const;
    void setScientific(bool on);
    void move(int dr, int dc, bool repeat);

    void input(const KeyEvent& ev);
    void keyboard(const SDL_KeyboardEvent& k);
    void text(const char* s);
    void automate(std::vector<KeyEvent>& keys);

    void draw();
    void drawDisplay(const SDL_FRect& r);
    void drawGrid(const SDL_FRect& r);
    void drawBar();

    Grid basic_, sci_;
    bool scientific_ = false, degrees_ = true, second_ = false;
    double memory_ = 0, ans_ = 0;
    std::vector<calc::Token> tokens_;
    calc::Result preview_;
    bool evaluated_ = false;   // the display shows the result of the last =
    std::string history_;      // "12×3 =" of the last result
    std::string last_;         // "12×3 = 36", shown while the next expression is entered
    std::string error_;
    int row_ = 2, col_ = 0;

    std::string settingsPath_;
    bool running_ = true, dirty_ = true;
    std::mt19937 rng_{std::random_device{}()};

    std::vector<std::string> script_;
    double scriptAt_ = 0;
    std::string shot_;
};
