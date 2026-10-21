#include "app.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <sys/wait.h>
#include <unistd.h>

#include "art.h"
#include "home.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "system.h"
#include "util.h"
#include "widgets.h"

App app;

namespace anim {
namespace {
double gNow = 0;
float gScale = 1;
bool gAlive = false;
}  // namespace
void tick() {
    gNow = SDL_GetTicksNS() / 1e9;
    gAlive = false;
}
double now() { return gNow; }
float scale() { return gScale; }
void setScale(float s) { gScale = s; }
void keepAlive() { gAlive = true; }
bool alive() { return gAlive; }
}  // namespace anim

namespace {
constexpr float kTopBar = 32, kBottomBar = 36;
constexpr double kPollInterval = 1.0;
constexpr double kLaunchTimeout = 8.0;
}  // namespace

Screen::Screen() { shown.go(1.f, 0.2f); }

void Screen::close() {
    if (closing_) return;
    closing_ = true;
    shown.go(0.f, 0.16f);
}

// ---------------------------------------------------------------- stack ---

void App::push(std::unique_ptr<Screen> s) {
    input.releaseAll();
    dock_ = -1;
    pending_.push_back(std::move(s));
    dirty = true;
}

Screen* App::top() const {
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
        if (!(*it)->closing()) return it->get();
    return nullptr;
}

void App::dialog(const std::string& icon, const std::string& title, const std::string& message,
                 std::vector<DialogButton> buttons, int selected) {
    push(std::make_unique<DialogScreen>(icon, title, message, std::move(buttons), selected));
}

void App::confirm(const std::string& icon, const std::string& title, const std::string& message, const std::string& ok,
                  std::function<void()> action) {
    dialog(icon, title, message, {{"Cancel", nullptr}, {ok, std::move(action), true}}, 0);
}

void App::toast(const std::string& text, const std::string& icon, Color color) {
    Toast t;
    t.text = text;
    t.icon = icon;
    t.color = color;
    toastQueue_.push_back(std::move(t));
    dirty = true;
}

// --------------------------------------------------------------- system ---

void App::runSystem(const std::string& command, const std::string& failure) {
    int status = 0;
    std::string out = runCapture(command, &status);
    if (status != 0) {
        logf("%s: exit %d: %s", command.c_str(), status, trim(out).c_str());
        toast(failure + (trim(out).empty() ? "" : ": " + trim(out)), "triangle-alert", theme::bad);
    }
}

void App::powerOff() {
    confirm("power", "Power off?", "The device shuts down.", "Power off", [this] {
        saveState();
        toast("Powering off", "power");
        runSystem(platform.poweroff, "Power off failed");
    });
}

void App::reboot() {
    confirm("rotate-ccw", "Restart?", "The device reboots.", "Restart", [this] {
        saveState();
        toast("Restarting", "rotate-ccw");
        runSystem(platform.reboot, "Restart failed");
    });
}

void App::screenOff() {
    sys.screenOff();
    input.releaseAll();
}

void App::eject() {
    confirm("card", "Eject the card?", "Games on the external card disappear from the library.", "Eject", [this] {
        int status = 0;
        std::string out = runCapture(platform.eject, &status);
        if (status == 0) {
            toast("Card ejected, safe to remove", "check", theme::good);
            reloadCatalog();
        } else {
            toast("Eject failed: " + trim(out), "triangle-alert", theme::bad);
        }
        sys.poll();
    });
}

void App::quit(int code) {
    exitCode_ = code;
    running_ = false;
}

void App::switchToPrevious() {
    if (platform.previousLauncher.empty()) return;
    saveState();
    art.stop();
    sys.shutdown();
    gfx.shutdown();
    SDL_Quit();
    logf("switching to %s", platform.previousLauncher.c_str());
    execl("/bin/sh", "sh", "-c", ("exec " + platform.previousLauncher).c_str(), (char*)nullptr);
    _exit(127);
}

void App::reloadCatalog() {
    catalog.load();
    if (home) home->rebuild();
    dirty = true;
}

// --------------------------------------------------------------- launch ---

std::string App::wrapCommand(const Entry& e) const {
    std::string id = e.id;
    std::replace(id.begin(), id.end(), '\t', ' ');
    std::replace(id.begin(), id.end(), '\n', ' ');
    std::string cmd = e.command;
    if (settings.hudNextLaunch && !platform.hudEnv.empty()) cmd = "export " + platform.hudEnv + "; " + cmd;
    return shellQuote(paths.exeDir + "/kennel-run") + " " + shellQuote(stats.journal()) + " " + shellQuote(id) + " " +
           shellQuote(cmd);
}

void App::launch(const Entry& e, bool terminal) {
    if (launch_.active) return;
    launch_.active = true;
    launch_.started = false;
    launch_.id = e.id;
    launch_.title = e.title;
    launch_.command = wrapCommand(e);
    launch_.terminal = terminal || e.terminal;
    launch_.t.set(0);
    launch_.t.go(1.f, 0.18f);
    settings.hudNextLaunch = false;
    dock_ = -1;
    input.releaseAll();
    dirty = true;
}

void App::startLaunch() {
    launch_.started = true;
    launch_.since = anim::now();
    saveState();
    logf("launch %s: %s", launch_.id.c_str(), launch_.command.c_str());

    if (platform.launch == Platform::Launch::Command) {
        std::string cmd = replaceAll(platform.launchCommand, "%c", shellQuote(launch_.command));
        cmd = replaceAll(cmd, "%t", launch_.terminal ? platform.terminalFlag : "");
        if (!spawnDetached(cmd)) {
            toast("Could not start the launch command", "triangle-alert", theme::bad);
            launch_.active = false;
        }
        return;
    }

    // Exec: give the display back, run the app, then start a fresh copy of kennel.
    art.stop();
    sys.shutdown();
    gfx.shutdown();
    SDL_Quit();
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", launch_.command.c_str(), (char*)nullptr);
        _exit(127);
    }
    int status = 0;
    if (pid > 0)
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    execv(paths.exe.c_str(), argv);
    _exit(127);
}

std::string App::autolaunchId() const {
    if (platform.autolaunch.empty()) return {};
    std::string first = readLine(paths.expand(platform.autolaunch));
    if (first.rfind("# ", 0) != 0) return {};
    size_t tab = first.find('\t');
    if (tab == std::string::npos) return {};
    std::string category = first.substr(2, tab - 2), name = first.substr(tab + 1);
    for (const auto& e : catalog.entries)
        if (e.category == category && e.name == name) return e.id;
    return {};
}

void App::setAutolaunch(const Entry* e) {
    std::string path = paths.expand(platform.autolaunch);
    if (!e) {
        unlink(path.c_str());
        toast("Autolaunch off", "check");
        return;
    }
    // Same layout as Puppy: an id comment, then the command appd runs once at boot.
    if (writeFileAtomic(path, "# " + e->category + "\t" + e->name + "\n" + wrapCommand(*e) + "\n"))
        toast(e->title + " starts at boot", "check", theme::good);
    else
        toast("Could not write " + path, "triangle-alert", theme::bad);
}

void App::saveState() {
    if (home) writeFile(paths.runtime + "/state", home->stateString());
}

void App::reportLastSession() {
    const Session* s = stats.newSession();
    if (!s || !settings.reportErrors || s->unknown || s->killed) return;
    if (s->rc == 0 || s->rc == 130 || s->rc == 143) return;
    const Entry* e = catalog.find(s->id);
    std::string name = e ? e->title : s->id;
    std::string reason = s->rc > 128 && s->rc < 160 ? strprintf("signal %d", s->rc - 128) : strprintf("code %d", s->rc);
    std::vector<DialogButton> buttons = {{"OK", nullptr}};
    if (!platform.appLog.empty())
        buttons.push_back({"View log", [this] { openLog("Last app log", paths.expand(platform.appLog)); }});
    int selected = (int)buttons.size() - 1;
    dialog("triangle-alert", name + " exited with " + reason,
           strprintf("The session lasted %s. The log of the last app is kept until the next launch.",
                     formatDuration(s->duration).c_str()),
           std::move(buttons), selected);
}

void App::checkBattery() {
    const Battery& b = sys.battery;
    if (b.capacity < 0 || settings.lowBattery == 0) return;
    if (b.charging() || b.externalPower) {
        warnedBattery_ = 101;
        return;
    }
    int level = b.capacity <= 5 ? 5 : b.capacity <= settings.lowBattery ? settings.lowBattery : 101;
    if (level < warnedBattery_) {
        warnedBattery_ = level;
        toast(strprintf("Battery low: %d%%", b.capacity), "battery-warning", level <= 5 ? theme::bad : theme::warn);
    }
}

// ----------------------------------------------------------------- dock ---

std::vector<App::DockItem> App::dockItems() const {
    std::vector<DockItem> out;
    out.push_back({"settings", "Settings", [] { app.openSettings(); }});
    if (settings.expert) out.push_back({"terminal", "Expert", [] { app.openExpert(); }});
    out.push_back({"chart-column", "Statistics", [] { app.openStatistics(); }});
    if (sys.hasBacklight()) out.push_back({"monitor-off", "Screen off", [] { app.screenOff(); }});
    if (!platform.eject.empty() && sys.externalMounted) out.push_back({"card", "Eject card", [] { app.eject(); }});
    if (!platform.reboot.empty()) out.push_back({"rotate-ccw", "Restart", [] { app.reboot(); }});
    if (!platform.poweroff.empty()) out.push_back({"power", "Power off", [] { app.powerOff(); }});
    return out;
}

void App::dockInput(const KeyEvent& ev) {
    auto items = dockItems();
    switch (ev.key) {
        case Key::Left: dock_ = std::max(0, dock_ - 1); break;
        case Key::Right: dock_ = std::min((int)items.size() - 1, dock_ + 1); break;
        case Key::Confirm: {
            auto action = items[dock_].action;
            dock_ = -1;
            action();
            break;
        }
        case Key::Back:
        case Key::Select:
        case Key::Up: dock_ = -1; break;
        default: break;
    }
    dirty = true;
}

// ----------------------------------------------------------------- input ---

void App::dispatch(const KeyEvent& ev) {
    if (sys.wake()) {
        input.releaseAll();
        return;
    }
    if (launch_.active) return;
    Screen* s = top();
    if (!s) return;
    if (dock_ >= 0 && s->showDock()) return dockInput(ev);
    if (ev.key == Key::Select && !ev.repeat && s->showDock()) {
        dock_ = 0;
        dirty = true;
        return;
    }
    s->input(ev);
    dirty = true;
}

// ------------------------------------------------------------------ draw ---

void App::drawTopBar() {
    gfx.gradient({0, 0, gfx.W, kTopBar + 8}, rgb(0, 0.6f), rgb(0, 0.f));
    const float cy = kTopBar / 2;
    const float th = gfx.lineHeight(Font::SmallBold);
    float x = 14;

    Settings::TimeMode mode = settings.time;
    double wall = wallSeconds();
    if (mode == Settings::TimeMode::Auto) mode = wallClockValid(wall) ? Settings::TimeMode::Clock : Settings::TimeMode::Uptime;
    if (mode == Settings::TimeMode::Clock) {
        time_t t = (time_t)wall;
        tm lt{};
        localtime_r(&t, &lt);
        x += gfx.text(Font::SmallBold, strprintf("%02d:%02d", lt.tm_hour, lt.tm_min), x, cy - th / 2, theme::text) + 12;
    } else if (mode == Settings::TimeMode::Uptime) {
        gfx.icon("timer", x, cy - 9, 18, theme::dim);
        x += 22;
        x += gfx.text(Font::SmallBold, formatClock(uptimeSeconds()), x, cy - th / 2, theme::text) + 12;
    }
    if (Screen* s = top()) {
        std::string title = s->title();
        if (!title.empty()) gfx.text(Font::SmallBold, title, x, cy - th / 2, theme::dim, gfx.W / 2 - x);
    }

    float rx = gfx.W - 14;
    const Battery& b = sys.battery;
    if (b.capacity >= 0) {
        Color c = b.charging() ? theme::good : b.capacity <= 10 ? theme::bad : b.capacity <= 20 ? theme::warn : theme::text;
        std::string pct = strprintf("%d%%", b.capacity);
        float w = gfx.measure(Font::SmallBold, pct);
        rx -= w;
        gfx.text(Font::SmallBold, pct, rx, cy - th / 2, c);
        const char* icon = b.charging() ? "battery-charging" : b.capacity <= 10 ? "battery-warning" : b.capacity <= 35 ? "battery-low"
                                     : b.capacity <= 70 ? "battery-medium" : "battery-full";
        rx -= 24;
        gfx.icon(icon, rx, cy - 10, 20, c);
        rx -= 14;
    }
    if (sys.hasVolume()) {
        int v = sys.volume();
        bool mute = (sys.hasMute() && sys.muted()) || v == 0;
        std::string pct = strprintf("%d%%", v);
        float w = gfx.measure(Font::SmallBold, pct);
        rx -= w;
        gfx.text(Font::SmallBold, pct, rx, cy - th / 2, mute ? theme::faint : theme::text);
        const char* icon = mute ? "volume-x" : (sys.hasOutputSwitch() && !sys.speaker()) ? "headphones" : v < 50 ? "volume-1" : "volume-2";
        rx -= 24;
        gfx.icon(icon, rx, cy - 10, 20, mute ? theme::faint : theme::text);
        rx -= 14;
    }
    if (sys.usbConnected) {
        rx -= 20;
        gfx.icon("usb", rx, cy - 10, 20, theme::dim);
        rx -= 10;
    }
    if (sys.externalMounted) {
        rx -= 20;
        gfx.icon("card", rx, cy - 10, 20, theme::dim);
        rx -= 10;
    }
    if (settings.expert) {
        long availKb = 0;
        std::string meminfo = readFile("/proc/meminfo").value_or("");
        size_t at = meminfo.find("MemAvailable:");
        if (at != std::string::npos) availKb = atol(meminfo.c_str() + at + 13);
        std::string load = readLine("/proc/loadavg");
        load = load.substr(0, load.find(' '));
        std::string s = strprintf("%s  %s free", load.c_str(), formatBytes((uint64_t)availKb * 1024).c_str());
        float w = gfx.measure(Font::Small, s);
        rx -= w;
        gfx.text(Font::Small, s, rx, cy - gfx.lineHeight(Font::Small) / 2, theme::faint);
    }
}

void App::drawBottomBar() {
    const float y = gfx.H - kBottomBar;
    gfx.gradient({0, y - 10, gfx.W, kBottomBar + 10}, rgb(0, 0.f), rgb(0, 0.65f));
    Screen* s = top();
    if (!s) return;
    const float bh = 20, by = y + (kBottomBar - bh) / 2;
    const float th = gfx.lineHeight(Font::Small);

    bool dock = s->showDock();
    auto items = dock ? dockItems() : std::vector<DockItem>{};
    float dockW = 0;
    if (dock) {
        dockFocus_.go(dock_ >= 0 ? 1.f : 0.f, 0.15f);
        float f = dockFocus_.update();
        // Dock: icons on the right; the focused one opens into a pill with its label.
        float x = gfx.W - 10;
        for (int i = (int)items.size() - 1; i >= 0; --i) {
            bool sel = i == dock_;
            float w = sel ? 32 + gfx.measure(Font::SmallBold, items[i].label) + 12 : 30;
            x -= w;
            if (sel) {
                gfx.round({x, y + 5, w, kBottomBar - 10}, (kBottomBar - 10) / 2, accent(0.9f));
                gfx.icon(items[i].icon, x + 8, y + 8, 20, rgb(0x0b0d12));
                gfx.text(Font::SmallBold, items[i].label, x + 32, y + (kBottomBar - gfx.lineHeight(Font::SmallBold)) / 2, rgb(0x0b0d12));
            } else {
                gfx.icon(items[i].icon, x + 5, y + 8, 20, fade(theme::dim, 0.55f + 0.45f * f));
            }
            x -= 2;
        }
        dockW = gfx.W - x;
    }

    std::vector<Hint> hints;
    if (dock_ >= 0 && dock) hints = {{Key::Confirm, "Open"}, {Key::Back, "Back"}};
    else {
        hints = s->hints();
        if (dock) hints.push_back({Key::Select, "Menu"});
    }
    float x = 14;
    for (const auto& h : hints) {
        std::string label = input.label(h.key);
        float need = gfx.buttonWidth(label, bh) + 6 + gfx.measure(Font::Small, h.label) + 14;
        if (x + need > gfx.W - dockW - 8) break;
        x += gfx.button(label, x, by, bh, rgb(0xe8ebf0, 0.92f), rgb(0x0b0d12)) + 6;
        x += gfx.text(Font::Small, h.label, x, y + (kBottomBar - th) / 2, theme::dim) + 14;
    }
}

void App::drawToast() {
    if (toast_.text.empty() || toast_.until <= anim::now()) {
        if (!toastQueue_.empty() && (toast_.text.empty() || toast_.t.value <= 0)) {
            toast_ = std::move(toastQueue_.front());
            toastQueue_.erase(toastQueue_.begin());
            toast_.until = anim::now() + 3.2;
            toast_.t.set(0);
            toast_.t.go(1.f, 0.2f);
        } else if (!toast_.text.empty()) {
            toast_.t.go(0.f, 0.2f);
        }
    }
    float t = toast_.t.update();
    if (toast_.text.empty() || t <= 0) return;
    if (toast_.until > anim::now()) anim::keepAlive();  // wakes the loop for the fade out
    float w = std::min(gfx.W - 40, gfx.measure(Font::SmallBold, toast_.text) + 56);
    float h = 34, x = (gfx.W - w) / 2, y = kTopBar + 6 - 12 * (1 - t);
    gfx.shadow({x, y, w, h}, h / 2, 10, rgb(0, 0.5f * t));
    gfx.round({x, y, w, h}, h / 2, fade(theme::panelHi, 0.97f * t));
    gfx.icon(toast_.icon, x + 14, y + 7, 20, fade(toast_.color, t));
    gfx.text(Font::SmallBold, toast_.text, x + 42, y + (h - gfx.lineHeight(Font::SmallBold)) / 2, fade(theme::text, t),
             w - 56);
}

void App::drawLaunch() {
    if (!launch_.active) return;
    float t = launch_.t.update();
    gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0, 0.82f * t));
    const Entry* e = catalog.find(launch_.id);
    float s = 0.92f + 0.08f * t;
    float w = ArtCache::kW * s, h = ArtCache::kH * s;
    SDL_FRect r{(gfx.W - w) / 2, (gfx.H - h) / 2 - 24, w, h};
    if (e) {
        auto card = art.card(*e, catalog.category(e->category));
        gfx.shadow(r, 14, 18, rgb(0, 0.6f * t));
        if (card.stock) gfx.image(card.stock, r, t);
        if (card.art) gfx.image(card.art, r, t * card.artAlpha);
    }
    float y = r.y + r.h + 18;
    gfx.text(Font::Small, launch_.started ? "Starting" : "Launching", gfx.W / 2, y, fade(theme::dim, t), 0, Align::Center);
    gfx.text(Font::Bold, launch_.title, gfx.W / 2, y + 20, fade(theme::text, t), gfx.W - 60, Align::Center);
}

void App::drawPerf() {
    if (!settings.perfOverlay) return;
    std::string s = strprintf("%.1f ms  %.0f Hz  art %zu  text %zu", gfx.frameMsAvg, gfx.refresh, art.textureCount(), gfx.textCount());
    gfx.round({8, gfx.H - kBottomBar - 26, gfx.measure(Font::Small, s) + 16, 22}, 6, rgb(0, 0.6f));
    gfx.text(Font::Small, s, 16, gfx.H - kBottomBar - 24, theme::good);
}

// Development aid: KENNEL_AUTOMATE="right,right,wait:500,shot:/tmp/a.bmp,x,quit" runs one token every
// 300 ms (wait:MS pauses); shot:PATH saves the next frame as BMP.
void App::automate(std::vector<KeyEvent>& keys) {
    if (script_.empty() || anim::now() < scriptAt_) return;
    std::string tok = script_.front();
    script_.erase(script_.begin());
    scriptAt_ = anim::now() + 0.3;
    static const std::pair<const char*, Key> names[] = {
        {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right}, {"a", Key::Confirm},
        {"b", Key::Back}, {"x", Key::X}, {"y", Key::Y}, {"l1", Key::L1}, {"r1", Key::R1}, {"l2", Key::L2},
        {"r2", Key::R2}, {"start", Key::Start}, {"select", Key::Select}};
    if (tok.rfind("wait:", 0) == 0) scriptAt_ = anim::now() + atoi(tok.c_str() + 5) / 1000.0;
    else if (tok.rfind("shot:", 0) == 0) shot_ = tok.substr(5);
    else if (tok == "quit") running_ = false;
    else
        for (const auto& [n, k] : names)
            if (tok == n) keys.push_back({k, false});
    dirty = true;
}

void App::frame() {
    for (auto& s : pending_) stack_.push_back(std::move(s));
    pending_.clear();

    // Draw from the topmost screen that fully covers what is below it.
    size_t first = 0;
    for (size_t i = stack_.size(); i-- > 0;) {
        if (!stack_[i]->overlay() && stack_[i]->shown.value >= 1.f && !stack_[i]->closing()) {
            first = i;
            break;
        }
    }
    gfx.beginFrame();
    for (size_t i = first; i < stack_.size(); ++i) {
        stack_[i]->shown.update();
        stack_[i]->update();
        stack_[i]->draw();
    }
    drawTopBar();
    drawBottomBar();
    drawToast();
    drawLaunch();
    drawPerf();
    if (!shot_.empty()) {
        if (SDL_Surface* surf = SDL_RenderReadPixels(gfx.renderer, nullptr)) {
            SDL_SaveBMP(surf, shot_.c_str());
            SDL_DestroySurface(surf);
        }
        shot_.clear();
    }
    gfx.endFrame();
    art.pump();

    bool removed = false;
    for (auto it = stack_.begin(); it != stack_.end();) {
        if ((*it)->finished()) {
            it = stack_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed)
        if (Screen* s = top()) s->resumed();
}

// ------------------------------------------------------------------ main ---

int App::run(int argcIn, char** argvIn) {
    argc = argcIn;
    argv = argvIn;
    uint64_t t0 = SDL_GetTicksNS();
    paths.init(argv[0]);
    logToFile(paths.runtime + "/kennel.log");
    platform = detectPlatform();
    settings.load(paths.config + "/kennel.ini");
    if (!settings.hiddenInitialized) settings.hidden = platform.hideCategories;
    anim::setScale(settings.animScale());
    signal(SIGPIPE, SIG_IGN);

    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        logf("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (!gfx.init(platform.width, platform.height, platform.fullscreen)) return 1;

    catalog.load();
    stats.load(paths.data);
    sys.init();
    art.start(paths.cache);

    auto homeScreen = std::make_unique<HomeScreen>();
    home = homeScreen.get();
    homeScreen->shown.set(1.f);
    stack_.push_back(std::move(homeScreen));
    home->restore(readFile(paths.runtime + "/state").value_or(""));
    reportLastSession();
    startupMs = (SDL_GetTicksNS() - t0) / 1e6;
    logf("ready in %.0f ms", startupMs);

    if (const char* script = getenv("KENNEL_AUTOMATE")) script_ = split(script, ',');

    std::vector<KeyEvent> keys;
    bool needFrame = true;
    while (running_) {
        anim::tick();
        double now = anim::now();
        SDL_Event ev;
        int timeout = needFrame ? 0 : (int)std::max(0.0, (nextPoll_ - now) * 1000);
        if (!script_.empty()) timeout = std::min(timeout, 50);
        int repeat = input.timeoutMs();
        if (repeat >= 0) timeout = std::min(timeout, repeat);
        if (launch_.active && launch_.started) timeout = std::min(timeout, 500);
        bool got = needFrame ? SDL_PollEvent(&ev) : SDL_WaitEventTimeout(&ev, timeout);
        keys.clear();
        while (got) {
            if (ev.type == SDL_EVENT_QUIT) running_ = false;
            else if (ev.type == SDL_EVENT_WINDOW_EXPOSED || ev.type == SDL_EVENT_USER) dirty = true;
            input.handle(ev, keys);
            got = SDL_PollEvent(&ev);
        }
        anim::tick();
        now = anim::now();
        input.tick(keys);
        automate(keys);
        for (const auto& k : keys) dispatch(k);

        if (now >= nextPoll_) {
            nextPoll_ = now + kPollInterval;
            if (sys.poll()) dirty = true;
            checkBattery();
            if (settings.time != Settings::TimeMode::Off) dirty = true;
        }
        if (launch_.active && launch_.started && now - launch_.since > kLaunchTimeout) {
            launch_.active = false;
            toast("The app did not start", "triangle-alert", theme::bad);
        }

        if (dirty || needFrame || !pending_.empty() || art.busy()) {
            dirty = false;
            frame();
            needFrame = anim::alive() || !pending_.empty();
        }
        if (launch_.active && !launch_.started && launch_.t.value >= 1.f) startLaunch();
    }

    saveState();
    art.stop();
    sys.shutdown();
    gfx.shutdown();
    SDL_Quit();
    return exitCode_;
}
