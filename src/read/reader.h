#pragma once

#include <string>
#include <vector>

#include "book.h"
#include "glyphs.h"
#include "input.h"
#include "layout.h"
#include "menu.h"
#include "readsettings.h"

class Reader {
public:
    int run(int argc, char** argv);

private:
    enum class Mode { Reading, Chapters, Settings, Error };

    void opening(const std::string& name);
    void apply(bool fonts);
    void relayout();
    int visibleLines() const;
    float lineStep() const;
    float textTop() const;
    bool atEnd();
    void scroll(int lines);
    void jump(int paragraph);
    void chapterStep(int dir);
    void openChapters();
    void openSettings();
    void closePanel();
    void input(const KeyEvent& ev);
    void draw();
    void drawText();
    void drawStatus();
    void drawChapters();
    void drawSettings();
    void drawError();
    void drawHints(const std::vector<std::pair<Key, std::string>>& hints, Color bg);
    void savePos();
    void automate(std::vector<KeyEvent>& keys);
    int brightness() const;
    void setBrightness(int level);

    Mode mode_ = Mode::Reading;
    Book book_;
    std::string bookPath_;
    std::string error_;
    ReadSettings rs_;
    Glyphs glyphs_;
    Layout layout_;
    LinePos top_;
    Menu menu_;
    bool running_ = true;
    bool dirty_ = true;
    bool posDirty_ = false;
    double scrolledAt_ = 0;
    long maxBrightness_ = 0;
    bool rebuildSettings_ = false;

    std::vector<std::string> script_;
    double scriptAt_ = 0;
    std::string shot_;
};
