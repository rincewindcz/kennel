#pragma once

#include <string>
#include <vector>

#include "complete.h"
#include "glyphs.h"
#include "input.h"
#include "menu.h"
#include "osk.h"
#include "pty.h"
#include "vt.h"

class Term {
public:
    int run(int argc, char** argv);

private:
    // Type: on-screen keyboard and suggestions. Keys: buttons send keys to full screen programs.
    enum class Mode { Type, Keys, Scroll, Menu, Help, Exited };

    void loadSettings();
    void saveSettings();
    bool openFont();
    void layout();
    void setMode(Mode m);

    void send(std::string_view bytes);
    void type(const std::string& text, bool ctrl);
    void sendAction(Keyboard::Action a, bool ctrl);
    void accept();
    void checkForeground();
    void output();

    void input(const KeyEvent& ev);
    void inputType(const KeyEvent& ev);
    void inputKeys(const KeyEvent& ev);
    void inputScroll(const KeyEvent& ev);
    void keyboard(const SDL_KeyboardEvent& k);
    void openMenu();
    void scrollBy(int lines);

    void draw();
    void drawGrid();
    void drawStrip(const SDL_FRect& r);
    void drawBar(const std::vector<std::pair<std::string, std::string>>& hints, const std::string& right);
    void drawMenu();
    void drawHelp();
    void automate(std::vector<KeyEvent>& keys);

    Vt vt_;
    Pty pty_;
    Glyphs glyphs_;
    Keyboard keyboard_{true};
    Completer completer_;
    Menu menu_;
    Mode mode_ = Mode::Type, under_ = Mode::Type;

    std::string settingsPath_;
    int fontSize_ = 13;
    int cellW_ = 8, cellH_ = 16;
    SDL_FRect grid_{};
    int scroll_ = 0;

    std::string command_;
    std::string line_;  // typed at the shell prompt since the last Enter
    bool lineKnown_ = true;
    bool shellForeground_ = true;
    std::vector<Suggestion> suggestions_;
    int suggestion_ = 0;
    bool suggestionsDirty_ = true;

    bool running_ = true, dirty_ = true;
    int exitCode_ = 0;
    int stickY_ = 0;
    double nextStick_ = 0;

    std::vector<std::string> script_;
    double scriptAt_ = 0;
    std::string shot_;
};
