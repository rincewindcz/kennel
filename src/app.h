#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "anim.h"
#include "catalog.h"
#include "gfx.h"
#include "input.h"
#include "stats.h"

struct Hint {
    Key key;
    std::string label;
};

// A screen on the stack. `shown` runs 0 -> 1 when it opens and back to 0 when it closes; the
// screen uses it for its transition and is removed when the close finishes.
class Screen {
public:
    Screen();
    virtual ~Screen() = default;
    virtual void input(const KeyEvent& ev) = 0;
    virtual void update() {}
    virtual void draw() = 0;
    virtual bool overlay() const { return false; }
    virtual bool showDock() const { return false; }
    virtual std::vector<Hint> hints() const { return {}; }
    virtual std::string title() const { return {}; }
    virtual void resumed() {}
    virtual bool live() const { return false; }  // changes on its own (update()): redrawn every poll

    void close();
    bool closing() const { return closing_; }
    bool finished() const { return closing_ && shown.value <= 0.f; }

    anim::Tween shown;

protected:
    bool closing_ = false;
};

struct DialogButton {
    std::string label;
    std::function<void()> action;
    bool danger = false;
};

class HomeScreen;

class App {
public:
    int run(int argc, char** argv);

    void push(std::unique_ptr<Screen> s);
    Screen* top() const;
    void dialog(const std::string& icon, const std::string& title, const std::string& message,
                std::vector<DialogButton> buttons, int selected = 0);
    void confirm(const std::string& icon, const std::string& title, const std::string& message,
                 const std::string& ok, std::function<void()> action);
    void toast(const std::string& text, const std::string& icon = "info", Color color = theme::text);

    void launch(const Entry& e, bool terminal = false);
    void setAutolaunch(const Entry* e);
    std::string autolaunchId() const;
    void reloadCatalog();
    void runSystem(const std::string& command, const std::string& failure);
    // Runs command on a worker thread; done(status, output) is called on the main loop.
    void runAsync(const std::string& command, std::function<void(int, const std::string&)> done);
    void quit(int code);
    void switchToPrevious();
    void restart();
    void setWallpaper(const std::string& path);
    std::string wrapCommand(const Entry& e) const;
    void saveState();

    void powerOff();
    void reboot();
    void screenOff();
    void eject();

    void openSettings();
    void openExpert();
    void openStatistics();
    void openLog(const std::string& title, const std::string& path);
    void openFiles(const std::string& dir = {}, const std::string& select = {});
    void openTerminal(const std::string& cwd = {}, const std::string& command = {});

    Catalog catalog;
    Stats stats;
    HomeScreen* home = nullptr;
    bool dirty = true;
    double startupMs = 0;
    int argc = 0;
    char** argv = nullptr;

private:
    struct DockItem {
        std::string icon, label;
        std::function<void()> action;
    };
    struct AsyncJob {
        std::thread thread;
        std::atomic<bool> finished{false};
        int status = 0;
        std::string output;
        std::function<void(int, const std::string&)> done;
    };
    struct Toast {
        std::string text, icon;
        Color color;
        double until = 0;
        anim::Tween t;
    };

    std::vector<DockItem> dockItems() const;
    void dispatch(const KeyEvent& ev);
    void dockInput(const KeyEvent& ev);
    void frame();
    void drawTopBar();
    void drawBottomBar();
    void drawToast();
    void drawLaunch();
    void drawPerf();
    void startLaunch();
    void checkBattery();
    void reportLastSession();
    void automate(std::vector<KeyEvent>& keys);
    void finishJobs();
    void setDark(bool dark);

    std::vector<std::unique_ptr<Screen>> stack_;
    std::vector<std::unique_ptr<Screen>> pending_;
    std::vector<std::unique_ptr<AsyncJob>> jobs_;
    int dock_ = -1;
    anim::Tween dockFocus_;
    Toast toast_;
    std::vector<Toast> toastQueue_;
    bool running_ = true;
    int exitCode_ = 0;
    double nextPoll_ = 0;
    double lastInput_ = 0;
    bool dark_ = false;      // backlight off: nothing is drawn
    long clockMinute_ = -1;  // minute shown by the top bar clock
    int warnedBattery_ = 101;
    std::vector<std::string> script_;
    double scriptAt_ = 0;
    std::string shot_;

    struct {
        bool active = false;
        bool started = false;
        double since = 0;
        std::string id, title, command;
        bool terminal = false;
        anim::Tween t;
    } launch_;
};

extern App app;
