#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "app.h"

// Single pane file browser: open files with their library entry, an archive's command, the reader,
// the text viewer or the image viewer; copy, move, rename and delete. Copies, moves and deletes run
// on a worker thread.
class FileScreen : public Screen {
public:
    explicit FileScreen(std::string dir, std::string select = {});
    ~FileScreen() override;
    void input(const KeyEvent& ev) override;
    void update() override;
    void draw() override;
    std::vector<Hint> hints() const override;
    std::string title() const override { return "Files"; }
    void resumed() override { reload(); }
    std::string state() const;

private:
    struct Item {
        std::string name;
        bool dir = false, link = false, exec = false, parent = false;
        uint64_t size = 0;
    };

    void open(const std::string& dir, const std::string& select = {});
    void reload();
    const Item* current() const;
    std::string pathOf(const Item& it) const;
    void activate();
    void openFile(const std::string& path);
    void showActions();
    void paste();
    void rename();
    void makeFolder();
    void remove();
    void info();
    void runJob(const std::string& label, const std::string& command, const std::string& select);
    int visibleRows() const;

    std::string dir_;
    std::vector<Item> items_;
    int selected_ = 0;
    bool hidden_ = false;
    std::string error_;
    anim::Tween scroll_, cursor_;
    bool placed_ = false;

    std::thread job_;
    std::atomic<bool> jobDone_{false};
    std::string jobLabel_, jobOutput_, jobSelect_;
    int jobStatus_ = 0;
};
