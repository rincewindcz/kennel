#pragma once

#include <string>
#include <vector>

#include "app.h"

class StatsScreen : public Screen {
public:
    StatsScreen();
    void input(const KeyEvent& ev) override;
    void draw() override;
    std::vector<Hint> hints() const override;
    std::string title() const override { return "Statistics"; }
    void resumed() override { compute(); }

private:
    enum Tab { kOverview, kMostPlayed, kCategories, kRecent, kTabCount };
    struct Row {
        std::string id, title, right, sub;
        float bar = -1;
        int rank = 0;
    };
    struct Tile {
        std::string value, label;
    };

    void compute();
    void setTab(int tab);
    void drawTabs(float dx, float alpha);
    void drawOverview(const SDL_FRect& area, float alpha);
    void drawList(const SDL_FRect& area, float alpha);
    void drawEmpty(const SDL_FRect& area, float alpha);
    std::vector<Row>& rows() { return rows_[tab_]; }
    const std::vector<Row>& rows() const { return rows_[tab_]; }

    int tab_ = kOverview;
    int prevTab_ = kOverview;
    anim::Tween tabT_;
    std::vector<Row> rows_[kTabCount];
    int selected_[kTabCount] = {};
    anim::Tween scroll_, cursor_;
    bool placed_ = false;

    Tile tiles_[4];
    std::string bootTime_;
    std::vector<Row> recentEntries_;
    std::string subtitle_;
};
