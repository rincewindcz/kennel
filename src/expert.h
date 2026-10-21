#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app.h"
#include "procs.h"

// Monospaced text page: logs, reports. Optionally reloads periodically and follows the end.
class TextScreen : public Screen {
public:
    TextScreen(std::string title, std::string icon, std::function<std::string()> load, double refresh = 0,
               bool follow = false);
    void input(const KeyEvent& ev) override;
    void update() override;
    void draw() override;
    std::vector<Hint> hints() const override;
    std::string title() const override { return title_; }

private:
    void reload();
    void layout();
    int visibleLines() const;
    int maxTop() const;
    SDL_FRect area() const;

    std::string title_, icon_;
    std::function<std::string()> load_;
    double refresh_, nextLoad_ = 0;
    bool follow_, wrap_ = true;
    std::vector<std::string> raw_, lines_;
    int top_ = 0, hscroll_ = 0;
};

class ProcessScreen : public Screen {
public:
    ProcessScreen();
    void input(const KeyEvent& ev) override;
    void update() override;
    void draw() override;
    std::vector<Hint> hints() const override;
    std::string title() const override { return "Processes"; }

private:
    void sample();
    int visibleRows() const;

    ProcessList list_;
    double next_ = 0;
    int samples_ = 0;
    int selected_ = 0, top_ = 0;
    int selectedPid_ = 0;
};
