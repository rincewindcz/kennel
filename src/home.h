#pragma once

#include <string>
#include <vector>

#include "app.h"

class HomeScreen : public Screen {
public:
    HomeScreen();
    void rebuild();
    void restore(const std::string& state);
    std::string stateString() const;
    const Entry* selected() const;

    void input(const KeyEvent& ev) override;
    void draw() override;
    bool showDock() const override { return true; }
    std::vector<Hint> hints() const override;
    void resumed() override;

private:
    enum class Kind { Continue, Favorites, Category };
    struct Row {
        Kind kind = Kind::Category;
        std::string key, name, glyph, subtitle;
        uint32_t color = 0;
        const Category* category = nullptr;
        std::vector<int> entries;
        int index = 0;
    };

    void select(int index, bool animate = true);
    void moveRow(int dir);
    void jumpLetter(int dir);
    void drawRow(const Row& row, float scroll, float dy, float alpha);
    void drawHeader(const Row& row, float dy, float alpha);
    void drawInfo(const Row& row, float alpha);
    void drawBackdrop();
    void drawEmpty();
    const Entry* entryAt(const Row& row, int i) const;
    const Category* categoryOf(const Entry& e) const;

    std::vector<Row> rows_;
    int row_ = 0;
    anim::Tween scroll_;
    anim::Tween rowT_;
    int prevRow_ = -1;
    float prevScroll_ = 0;
    int rowDir_ = 0;
    anim::Tween info_;

    double changedAt_ = 0;
    std::string backdropId_;
    std::string prevBackdropId_;
    anim::Tween backdropFade_;
};
