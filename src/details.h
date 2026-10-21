#pragma once

#include "app.h"
#include "widgets.h"

class DetailsScreen : public Screen {
public:
    explicit DetailsScreen(std::string id);
    void input(const KeyEvent& ev) override;
    void draw() override;
    std::vector<Hint> hints() const override;
    std::string title() const override { return "Details"; }
    void resumed() override { build(); }

private:
    void build();
    std::string id_;
    Menu menu_;
};
