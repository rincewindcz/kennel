#include "expert.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstring>
#include <set>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "art.h"
#include "battery.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "system.h"
#include "util.h"
#include "widgets.h"

namespace {

constexpr float kTop = 92, kBottom = 36;

std::string sanitize(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = in[i];
        if (c == 0x1b) {
            // CSI: ESC [ params final-byte; other escapes: ESC + one char.
            if (i + 1 < in.size() && in[i + 1] == '[') {
                i += 2;
                while (i < in.size() && !((unsigned char)in[i] >= 0x40 && (unsigned char)in[i] <= 0x7e)) ++i;
            } else {
                ++i;
            }
            continue;
        }
        if (c == '\t') {
            out.append(4, ' ');
            continue;
        }
        if (c == '\n') {
            out += '\n';
            continue;
        }
        if (c < 0x20 || c == 0x7f) continue;
        out += (char)c;
    }
    return out;
}

// Byte offset after skipping `count` UTF-8 code points from `from`.
size_t advance(const std::string& s, size_t from, int count) {
    size_t i = from;
    while (i < s.size() && count > 0) {
        ++i;
        while (i < s.size() && ((unsigned char)s[i] & 0xc0) == 0x80) ++i;
        --count;
    }
    return i;
}

long meminfo(const std::string& text, const char* key) {
    size_t at = text.find(key);
    return at == std::string::npos ? -1 : atol(text.c_str() + at + strlen(key));
}

std::string kv(const std::string& key, const std::string& value) {
    std::string k = key;
    if (k.size() < 14) k.append(14 - k.size(), ' ');
    return "  " + k + value + "\n";
}

std::string section(const std::string& name) { return "\n" + name + "\n"; }

std::string fileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return "missing";
    return formatBytes((uint64_t)st.st_size);
}

std::string firstWord(const std::string& command) {
    std::string c = trim(command);
    if (c.empty()) return {};
    if (c[0] == '\'' || c[0] == '"') {
        size_t end = c.find(c[0], 1);
        return c.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    }
    return c.substr(0, c.find(' '));
}

std::string statusReport() {
    std::string out;
    out += "SYSTEM\n";
    char host[256] = {};
    gethostname(host, sizeof host - 1);
    out += kv("Hostname", host);
    utsname u{};
    if (uname(&u) == 0) out += kv("Kernel", std::string(u.release) + " " + u.machine);
    double up = uptimeSeconds();
    out += kv("Uptime", formatClock(up) + "  (" + formatDuration(up) + ")");
    std::string load = readLine("/proc/loadavg");
    out += kv("Load", load);
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    std::string cpu = std::to_string(cores);
    long freq = 0;
    if (readLong("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", freq)) cpu += strprintf(", %ld MHz", freq / 1000);
    out += kv("CPUs", cpu);
    std::string mem = readFile("/proc/meminfo").value_or("");
    long total = meminfo(mem, "MemTotal:"), avail = meminfo(mem, "MemAvailable:");
    if (total >= 0)
        out += kv("RAM", strprintf("%s available of %s", formatBytes((uint64_t)std::max(0L, avail) * 1024).c_str(),
                                   formatBytes((uint64_t)total * 1024).c_str()));
    long swapTotal = meminfo(mem, "SwapTotal:"), swapFree = meminfo(mem, "SwapFree:");
    if (swapTotal > 0)
        out += kv("Swap", strprintf("%s used of %s", formatBytes((uint64_t)(swapTotal - swapFree) * 1024).c_str(),
                                    formatBytes((uint64_t)swapTotal * 1024).c_str()));
    if (isDir("/sys/block/zram0")) {
        auto f = split(readLine("/sys/block/zram0/mm_stat"), ' ');
        if (f.size() >= 3)
            out += kv("zram", strprintf("data %s, compressed %s, memory %s",
                                        formatBytes(strtoull(f[0].c_str(), nullptr, 10)).c_str(),
                                        formatBytes(strtoull(f[1].c_str(), nullptr, 10)).c_str(),
                                        formatBytes(strtoull(f[2].c_str(), nullptr, 10)).c_str()));
    }

    const Battery& b = sys.battery;
    out += section("POWER");
    if (b.capacity >= 0) {
        out += kv("Capacity", strprintf("%d%%", b.capacity));
        out += kv("Status", b.status.empty() ? "-" : b.status);
        out += kv("Health", b.health.empty() ? "-" : b.health);
        out += kv("Voltage", strprintf("%.3f V", b.voltage));
        out += kv("Current", strprintf("%.3f A", b.current));
        if (b.designCharge > 0) out += kv("Design", strprintf("%.0f mAh", b.designCharge * 1000));
        BatteryEstimate est = batteryLog.estimate(b);
        if (est.seconds >= 0)
            out += kv(b.status == "Charging" ? "Until full" : "Time left",
                      strprintf("about %s (%+.1f%%/h, %s)", formatDuration(est.seconds).c_str(), est.ratePerHour,
                                est.fromHistory ? "history" : "current"));
        out += kv("History", strprintf("%zu boots, %s", batteryLog.boots().size(), batteryLog.path().c_str()));
    } else {
        out += kv("Battery", "none");
    }
    out += kv("External", b.externalPower ? "connected" : "no");

    out += section("STORAGE");
    for (const auto& m : platform.mounts) {
        std::string path = paths.expand(m);
        struct statvfs st;
        if (statvfs(path.c_str(), &st) != 0) {
            out += kv(m, "unavailable");
            continue;
        }
        uint64_t size = (uint64_t)st.f_blocks * st.f_frsize, freeB = (uint64_t)st.f_bavail * st.f_frsize;
        uint64_t used = size - (uint64_t)st.f_bfree * st.f_frsize;
        out += kv(m, strprintf("%s used of %s (%.0f%%), %s free", formatBytes(used).c_str(), formatBytes(size).c_str(),
                               size ? used * 100.0 / size : 0.0, formatBytes(freeB).c_str()));
    }

    out += section("DISPLAY");
    out += kv("Renderer", gfx.rendererName);
    out += kv("Logical", strprintf("%.0fx%.0f", gfx.W, gfx.H));
    out += kv("Refresh", strprintf("%.1f Hz", gfx.refresh));
    out += kv("Frame", strprintf("%.2f ms", gfx.frameMsAvg));

    auto devfreq = glob("/sys/class/devfreq/*");
    if (!devfreq.empty()) {
        out += section("GPU");
        for (const auto& d : devfreq) {
            long f = 0;
            out += kv(baseName(d), readLong(d + "/cur_freq", f) ? strprintf("%ld MHz", f / 1000000) : "-");
        }
    }
    auto udcs = glob("/sys/class/udc/*");
    if (!udcs.empty()) {
        out += section("USB");
        for (const auto& d : udcs) out += kv(baseName(d), readLine(d + "/state"));
    }

    out += section("INPUT");
    out += kv("Gamepad", input.padName());

    out += section("KENNEL");
    out += kv("Version", KENNEL_VERSION);
    out += kv("Platform", platform.name + " (" + platform.id + ")");
    out += kv("Platform file", platform.file.empty() ? "built-in" : platform.file);
    out += kv("Start-up", strprintf("%.0f ms", app.startupMs));
    out += kv("Library", strprintf("%zu entries, %.1f ms", app.catalog.entries.size(), app.catalog.loadMs));
    out += kv("Art textures", std::to_string(art.textureCount()));
    out += kv("Text cache", std::to_string(gfx.textCount()));

    for (const auto& [name, command] : platform.statusCommands) {
        out += section(name);
        out += trim(runCapture(command)) + "\n";
    }
    return out;
}

std::string libraryReport() {
    const Catalog& c = app.catalog;
    std::string out = "SOURCES\n";
    for (const auto& s : c.sources)
        out += "  " + s.path + (s.loaded ? strprintf("\n    %d entries, %d archives\n", s.entries, s.archives) : "\n    missing\n");

    out += section("CATEGORIES");
    for (const auto& cat : c.categories) {
        out += strprintf("  %s: %zu %s\n", cat.name.c_str(), cat.entries.size(), cat.archive ? "files" : "entries");
        if (!cat.image.empty()) out += "    image " + cat.image + (fileExists(cat.image) ? "" : " (missing)") + "\n";
    }

    out += section("MESSAGES");
    if (c.messages.empty()) out += "  none\n";
    for (const auto& m : c.messages) out += strprintf("  %s:%d: %s\n", m.file.c_str(), m.line, m.text.c_str());

    out += section("ART");
    for (const auto& cat : c.categories) {
        int missing = 0;
        for (int i : cat.entries)
            if (c.entries[i].art.empty()) ++missing;
        out += strprintf("  %s: %d of %zu without own art\n", cat.name.c_str(), missing, cat.entries.size());
    }

    out += section("COMMANDS");
    std::set<std::string> checked;
    int problems = 0;
    for (const auto& e : c.entries) {
        std::string w = firstWord(e.command);
        if (w.empty() || w[0] != '/' || !checked.insert(w).second) continue;
        if (fileExists(w)) continue;
        ++problems;
        out += "  " + w + " missing\n    used by " + e.category + (e.fromArchive ? "" : " / " + e.name) + "\n";
    }
    if (!problems) out += "  all absolute command paths exist\n";

    out += strprintf("\nLoaded in %.1f ms\n", c.loadMs);
    return out;
}

std::string sessionsReport() {
    const auto& all = app.stats.sessions();
    if (all.empty()) return "No sessions recorded yet.\n";
    std::string out = "  SEQ BOOT  LENGTH        EXIT     WHEN              TITLE\n";
    int n = 0;
    for (auto it = all.rbegin(); it != all.rend() && n < 500; ++it, ++n) {
        const Session& s = *it;
        std::string length = s.unknown ? "unknown" : formatDuration(s.duration);
        std::string exit = s.killed ? "killed" : s.unknown ? "-" : std::to_string(s.rc);
        const Entry* e = app.catalog.find(s.id);
        out += strprintf("%5ld %4d  %-13s %-8s %-17s %s\n", s.seq, s.boot, length.c_str(), exit.c_str(),
                         app.stats.describeLast(s).c_str(), e ? e->title.c_str() : s.id.c_str());
    }
    return out;
}

}  // namespace

// ------------------------------------------------------------ text screen ---

TextScreen::TextScreen(std::string title, std::string icon, std::function<std::string()> load, double refresh, bool follow)
    : title_(std::move(title)), icon_(std::move(icon)), load_(std::move(load)), refresh_(refresh), follow_(follow) {
    reload();
}

SDL_FRect TextScreen::area() const { return {12, kTop, gfx.W - 24, gfx.H - kTop - kBottom - 6}; }

int TextScreen::visibleLines() const { return std::max(1, (int)(area().h / gfx.lineHeight(Font::Mono))); }

int TextScreen::maxTop() const { return std::max(0, (int)lines_.size() - visibleLines()); }

void TextScreen::reload() {
    std::string text = sanitize(load_ ? load_() : std::string());
    raw_.clear();
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        raw_.push_back(text.substr(pos, end - pos));
        pos = end + 1;
    }
    while (raw_.size() > 1 && raw_.back().empty()) raw_.pop_back();
    layout();
    nextLoad_ = anim::now() + refresh_;
}

void TextScreen::layout() {
    lines_.clear();
    float charW = std::max(1.f, gfx.measure(Font::Mono, "M"));
    int cols = std::max(10, (int)((area().w - 24) / charW));
    for (const auto& l : raw_) {
        if (!wrap_ || l.empty()) {
            lines_.push_back(l);
            continue;
        }
        size_t at = 0;
        while (at < l.size()) {
            size_t next = advance(l, at, cols);
            lines_.push_back(l.substr(at, next - at));
            at = next;
        }
    }
    top_ = follow_ ? maxTop() : std::clamp(top_, 0, maxTop());
}

void TextScreen::input(const KeyEvent& ev) {
    int page = std::max(1, visibleLines() - 1);
    switch (ev.key) {
        case Key::Back: close(); return;
        case Key::Up: top_ = std::max(0, top_ - 1); break;
        case Key::Down: top_ = std::min(maxTop(), top_ + 1); break;
        case Key::L1: top_ = std::max(0, top_ - page); break;
        case Key::R1: top_ = std::min(maxTop(), top_ + page); break;
        case Key::L2: top_ = 0; break;
        case Key::R2: top_ = maxTop(); break;
        case Key::Left:
            if (!wrap_) hscroll_ = std::max(0, hscroll_ - 8);
            return;
        case Key::Right:
            if (!wrap_) hscroll_ += 8;
            return;
        case Key::Y:
            wrap_ = !wrap_;
            hscroll_ = 0;
            layout();
            return;
        case Key::X:
            if (refresh_ > 0) {
                follow_ = !follow_;
                if (follow_) top_ = maxTop();
            }
            return;
        default: return;
    }
    if (refresh_ > 0) follow_ = top_ >= maxTop();
}

void TextScreen::update() {
    if (refresh_ > 0 && anim::now() >= nextLoad_ && !closing()) {
        reload();
        app.dirty = true;
    }
}

std::vector<Hint> TextScreen::hints() const {
    std::vector<Hint> h = {{Key::L1, "Page"}, {Key::Y, wrap_ ? "No wrap" : "Wrap"}};
    if (refresh_ > 0) h.push_back({Key::X, follow_ ? "Unfollow" : "Follow"});
    h.push_back({Key::Back, "Back"});
    return h;
}

void TextScreen::draw() {
    float t = shown.value, dx = 24 * (1 - t);
    drawBackground(t);
    int vis = visibleLines();
    std::string status = lines_.empty() ? "empty"
                                        : strprintf("lines %d-%d of %zu", top_ + 1, std::min((int)lines_.size(), top_ + vis),
                                                    lines_.size());
    status += wrap_ ? "  \xC2\xB7  wrap" : strprintf("  \xC2\xB7  column %d", hscroll_ + 1);
    if (refresh_ > 0 && follow_) status += "  \xC2\xB7  following";
    drawPageHeader(icon_, title_, status, t);

    SDL_FRect a = area();
    a.x += dx;
    gfx.round(a, 10, fade(rgb(0x07090d), 0.55f * t));
    gfx.pushClip(a);
    float lh = gfx.lineHeight(Font::Mono);
    for (int i = 0; i < vis && top_ + i < (int)lines_.size(); ++i) {
        const std::string& l = lines_[top_ + i];
        size_t from = wrap_ ? 0 : advance(l, 0, hscroll_);
        if (from >= l.size()) continue;
        gfx.text(Font::Mono, std::string_view(l).substr(from), a.x + 10, a.y + 4 + i * lh, fade(theme::text, 0.92f * t),
                 a.w - 20);
    }
    gfx.popClip();
    drawScrollbar({a.x + a.w - 6, a.y + 4, 3, a.h - 8}, (float)top_, (float)lines_.size(), (float)vis, t);
}

// -------------------------------------------------------- process screen ---

ProcessScreen::ProcessScreen() { sample(); }

void ProcessScreen::sample() {
    list_.sample();
    ++samples_;
    next_ = anim::now() + (samples_ == 1 ? 0.5 : 2.0);
    selected_ = std::clamp(selected_, 0, std::max(0, (int)list_.processes.size() - 1));
    for (size_t i = 0; i < list_.processes.size(); ++i)
        if (list_.processes[i].pid == selectedPid_) selected_ = (int)i;
    if (!list_.processes.empty()) selectedPid_ = list_.processes[selected_].pid;
}

int ProcessScreen::visibleRows() const {
    return std::max(1, (int)((gfx.H - kTop - kBottom - 6 - 28) / (gfx.lineHeight(Font::Mono) + 4)));
}

void ProcessScreen::update() {
    if (anim::now() >= next_ && !closing()) {
        sample();
        app.dirty = true;
    }
    if (samples_ == 1) anim::keepAlive();
}

void ProcessScreen::input(const KeyEvent& ev) {
    int n = (int)list_.processes.size();
    int page = std::max(1, visibleRows() - 1);
    switch (ev.key) {
        case Key::Back: close(); return;
        case Key::Up: selected_ = std::max(0, selected_ - 1); break;
        case Key::Down: selected_ = std::min(n - 1, selected_ + 1); break;
        case Key::L1: selected_ = std::max(0, selected_ - page); break;
        case Key::R1: selected_ = std::min(n - 1, selected_ + page); break;
        case Key::Confirm: {
            if (ev.repeat || selected_ < 0 || selected_ >= n) return;
            const Process& p = list_.processes[selected_];
            int pid = p.pid;
            std::string name = p.name;
            auto sendSignal = [pid, name](int sig, const char* what) {
                if (kill(pid, sig) == 0) app.toast(strprintf("%s %s (%d)", what, name.c_str(), pid), "check", theme::good);
                else app.toast(strprintf("%s %d: %s", name.c_str(), pid, strerror(errno)), "triangle-alert", theme::bad);
            };
            app.dialog("activity", strprintf("%s (%d)", name.c_str(), pid),
                       p.cmdline + "\nUser " + p.user + ", " + formatBytes((uint64_t)p.rssKb * 1024) + " resident.",
                       {{"Cancel", nullptr},
                        {"Terminate", [sendSignal] { sendSignal(SIGTERM, "Sent SIGTERM to"); }},
                        {"Kill", [sendSignal] { sendSignal(SIGKILL, "Killed"); }, true}},
                       0);
            return;
        }
        default: return;
    }
    if (selected_ >= 0 && selected_ < n) selectedPid_ = list_.processes[selected_].pid;
}

std::vector<Hint> ProcessScreen::hints() const {
    return {{Key::Confirm, "Signal"}, {Key::L1, "Page"}, {Key::Back, "Back"}};
}

void ProcessScreen::draw() {
    float t = shown.value, dx = 24 * (1 - t);
    drawBackground(t);
    std::string mem = readFile("/proc/meminfo").value_or("");
    long avail = meminfo(mem, "MemAvailable:");
    std::string sub = strprintf("%zu processes  \xC2\xB7  CPU %.0f%%  \xC2\xB7  %s available", list_.processes.size(),
                                list_.cpuTotal, formatBytes((uint64_t)std::max(0L, avail) * 1024).c_str());
    drawPageHeader("activity", "Processes", sub, t);

    SDL_FRect a{12 + dx, kTop, gfx.W - 24, gfx.H - kTop - kBottom - 6};
    gfx.round(a, 10, fade(rgb(0x07090d), 0.55f * t));
    float lh = gfx.lineHeight(Font::Mono), rh = lh + 4;
    float x = a.x + 10;
    gfx.text(Font::Mono, "  PID USER      CPU%     RSS  COMMAND", x, a.y + 6, fade(theme::faint, t));

    int vis = visibleRows(), n = (int)list_.processes.size();
    if (selected_ < top_) top_ = selected_;
    if (selected_ >= top_ + vis) top_ = selected_ - vis + 1;
    top_ = std::clamp(top_, 0, std::max(0, n - vis));

    SDL_FRect rows{a.x, a.y + 28, a.w, a.h - 28};
    gfx.pushClip(rows);
    float charW = gfx.measure(Font::Mono, "M");
    float cmdX = x + charW * 34;
    for (int i = 0; i < vis && top_ + i < n; ++i) {
        const Process& p = list_.processes[top_ + i];
        float y = rows.y + i * rh;
        bool sel = top_ + i == selected_;
        if (sel) gfx.round({a.x + 4, y, a.w - 16, rh}, 6, accent(0.2f * t));
        Color c = fade(p.uid == (int)getuid() ? theme::text : theme::dim, t);
        std::string cols = strprintf("%5d %-8.8s %5.1f %7s", p.pid, p.user.c_str(), p.cpu,
                                     formatBytes((uint64_t)p.rssKb * 1024).c_str());
        gfx.text(Font::Mono, cols, x, y + 2, c);
        gfx.text(Font::Mono, p.cmdline, cmdX, y + 2, c, a.x + a.w - 16 - cmdX);
    }
    gfx.popClip();
    drawScrollbar({a.x + a.w - 6, rows.y + 4, 3, rows.h - 8}, (float)top_, (float)n, (float)vis, t);
}

// -------------------------------------------------------------- app pages ---

void App::openLog(const std::string& title, const std::string& path) {
    push(std::make_unique<TextScreen>(title, "file-text", [path] {
        if (!fileExists(path)) return std::string("(file missing: ") + path + ")";
        bool truncated = false;
        std::string text = readTail(path, 256 * 1024, &truncated);
        if (text.empty()) return std::string("(empty)");
        if (truncated) text = "(showing the last 256 KiB of " + path + ")\n" + text;
        return text;
    }, 1.0, true));
}

void App::openExpert() {
    push(std::make_unique<MenuScreen>("Expert", "wrench", [] {
        std::vector<MenuItem> m;
        m.push_back(items::header("INSPECT"));
        m.push_back(items::link("gauge", "Status", [] {
            app.push(std::make_unique<TextScreen>("Status", "gauge", statusReport, 1.0));
        }));
        m.push_back(items::link("scroll-text", "Logs", [] {
            app.push(std::make_unique<MenuScreen>("Logs", "scroll-text", [] {
                std::vector<MenuItem> l;
                auto add = [&](const std::string& label, const std::string& path) {
                    MenuItem it = items::action("file-text", label, [label, path] { app.openLog(label, path); });
                    it.value = [path] { return fileSize(path); };
                    l.push_back(std::move(it));
                };
                for (const auto& [name, path] : platform.logs) add(name, paths.expand(path));
                for (const auto& g : platform.logGlobs) {
                    auto files = glob(paths.expand(g));
                    std::sort(files.begin(), files.end());
                    for (const auto& f : files) {
                        std::string label = f;
                        if (label.rfind(paths.home + "/", 0) == 0) label = "~" + label.substr(paths.home.size());
                        add(label, f);
                    }
                }
                add("kennel", paths.runtime + "/kennel.log");
                if (!platform.kernelLog.empty()) {
                    l.push_back(items::action("cpu", "Kernel", [] {
                        app.push(std::make_unique<TextScreen>("Kernel log", "cpu", [] { return runCapture(platform.kernelLog); },
                                                              2.0, true));
                    }));
                }
                return l;
            }));
        }));
        m.push_back(items::link("activity", "Processes", [] { app.push(std::make_unique<ProcessScreen>()); }));
        m.push_back(items::link("library", "Library", [] {
            app.push(std::make_unique<MenuScreen>("Library", "library", [] {
                std::vector<MenuItem> l;
                l.push_back(items::link("file-text", "Report", [] {
                    app.push(std::make_unique<TextScreen>("Library report", "library", libraryReport));
                }));
                l.push_back(items::action("refresh-cw", "Rescan library", [] {
                    app.reloadCatalog();
                    art.clear();
                    app.toast(strprintf("%zu entries in %zu categories", app.catalog.entries.size(),
                                        app.catalog.categories.size()),
                              "check", theme::good);
                }));
                return l;
            }));
        }));
        m.push_back(items::link("history", "Sessions", [] {
            app.push(std::make_unique<TextScreen>("Sessions", "history", sessionsReport));
        }));

        m.push_back(items::header("TOOLS"));
        m.push_back(items::link("terminal", "Terminal", [] { app.openTerminal(); }));
        m.push_back(items::link("folder", "Files", [] { app.openFiles("/"); }));
        m.push_back(items::toggle("gauge", "Performance overlay", [] { return settings.perfOverlay; }, [](bool v) {
            settings.perfOverlay = v;
            settings.save();
        }));
        if (!platform.hudEnv.empty())
            m.push_back(items::toggle("zap", "HUD for next launch", [] { return settings.hudNextLaunch; },
                                      [](bool v) { settings.hudNextLaunch = v; }));

        m.push_back(items::header("ACTIONS"));
        m.push_back(items::action("refresh-cw", "Restart kennel", [] { app.restart(); }));
        if (!platform.previousLauncher.empty()) {
            std::string prev = baseName(platform.previousLauncher);
            m.push_back(items::action("log-out", "Switch to " + prev, [prev] {
                app.confirm("log-out", "Switch to " + prev + "?", "kennel exits and " + prev + " starts in its place.",
                            "Switch", [] { app.switchToPrevious(); });
            }));
        }
        m.push_back(items::action("hard-drive", "Sync filesystems", [] {
            sync();
            app.toast("Filesystems synced", "check", theme::good);
        }));
        return m;
    }));
}
