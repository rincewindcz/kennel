#pragma once

#include <functional>
#include <string>
#include <vector>

#include "buffer.h"
#include "glyphs.h"
#include "input.h"
#include "menu.h"
#include "osk.h"

class Edit {
public:
    int run(int argc, char** argv);

private:
    // Type: on-screen keyboard. Navigate: full-height text, buttons move the cursor. Find: find (and
    // replace) fields over the keyboard. Review: replace match by match. Menu, Help and Dialog are
    // drawn over the mode below (under_).
    enum class Mode { Type, Navigate, Find, Review, SaveAs, Menu, Help, Dialog, Message };
    struct Row {
        int start, end, startCol;  // byte range of a visual line, display column of its start
    };
    struct Dialog {
        std::string title, message;
        std::vector<std::string> buttons;
        int selected = 0, cancel = 0;
        std::function<void(int)> done;
    };

    void loadSettings();
    void saveSettings();
    bool openFont();
    bool open(const std::string& path);
    void layout();
    void setMode(Mode m);
    bool overlay(Mode m) const { return m == Mode::Menu || m == Mode::Help || m == Mode::Dialog; }
    void notice(const std::string& text, Color c);
    void dialog(Dialog d);

    // Layout of the text into visual rows (word wrap) and cursor movement over them.
    int colOf(int line, int byte) const;
    std::vector<Row> rowsOf(int line) const;
    int rowIndex(const std::vector<Row>& rows, int byte) const;
    int byteAt(int line, const std::vector<Row>& rows, int r, int col) const;
    bool prevRow(int& line, int& sub) const;
    bool nextRow(int& line, int& sub) const;
    void ensureVisible(bool center);
    void moveTo(edit::Pos p, bool keepGoal = false);
    void moveRows(int n);
    void page(int dir);

    bool editable();
    void edited(edit::Pos p);
    void typeText(const std::string& text);
    void action(Keyboard::Action a);
    void ctrlKey(char c);
    void toggleWrap();

    void openFind(bool replace);
    void closeFind();
    bool findStep(bool forward);
    void replaceCurrent();
    void replaceAll();
    void fieldAction(Keyboard::Action a, const std::string& text);

    void save();
    void openSaveAs();
    void saveAs();
    bool writeTo(const std::string& target);
    void requestClose();
    std::string recoveryPath(const std::string& path) const;
    void writeRecovery();
    void checkRecovery();

    void input(const KeyEvent& ev);
    void inputType(const KeyEvent& ev);
    void inputNavigate(const KeyEvent& ev);
    void inputField(const KeyEvent& ev);
    void inputReview(const KeyEvent& ev);
    void inputMenu(const KeyEvent& ev);
    void inputDialog(const KeyEvent& ev);
    void keyboard(const SDL_KeyboardEvent& k);
    void text(const char* s);
    void openMenu();
    void automate(std::vector<KeyEvent>& keys);

    void draw();
    void drawStatus();
    void drawText();
    void drawField(const SDL_FRect& r, const std::string& label, const std::string& value, bool focused);
    void drawFieldBar(const SDL_FRect& r);
    void drawBar(const std::vector<std::pair<std::string, std::string>>& hints);
    void drawMenu();
    void drawHelp();
    void drawDialog();
    void drawMessage();

    edit::Buffer buf_;
    std::string path_;  // absolute; empty for an untitled buffer
    edit::Pos cur_;
    int goalCol_ = -1;  // display column kept by up/down; -1: the cursor's
    bool modified_ = false;

    Glyphs glyphs_;
    Keyboard keyboard_{true};
    Menu menu_;
    Mode mode_ = Mode::Type, under_ = Mode::Type, back_ = Mode::Type;  // back_: mode Find and Save as return to
    Dialog dialog_;

    std::string settingsPath_;
    bool wrap_ = true;
    int fontSize_ = 13;
    int cellW_ = 8, cellH_ = 16;
    int cols_ = 80, rows_ = 10;
    SDL_FRect text_{};
    int topLine_ = 0, topRow_ = 0, hscroll_ = 0;

    std::string find_, replace_, saveName_;
    int field_ = 0;  // 0: find (or the Save as name), 1: replace
    bool replacing_ = false, matchCase_ = false;
    bool hasMatch_ = false;
    edit::Pos match_;
    bool closeAfterSave_ = false;

    std::string notice_, message_;
    Color noticeColor_;
    double noticeUntil_ = 0;

    bool running_ = true, dirty_ = true;
    int exitCode_ = 0;
    std::vector<std::string> script_;
    double scriptAt_ = 0;
    std::string shot_;
};
