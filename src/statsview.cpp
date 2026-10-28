#include "statsview.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

#include "battery.h"
#include "details.h"
#include "system.h"
#include "util.h"
#include "widgets.h"

namespace {
constexpr float kBottom = 36, kRowH = 40, kTabsY = 88, kListY = 128;
const char* kTabNames[] = {"Overview", "Most played", "Categories", "Recent", "Battery"};

std::string titleOf(const std::string& id) {
    const Entry* e = app.catalog.find(id);
    return e ? e->title : id;
}

std::string exitText(const Session& s) {
    if (s.unknown) return {};
    if (s.killed) return "killed";
    if (s.rc == 0) return {};
    return strprintf("code %d", s.rc);
}
}  // namespace

StatsScreen::StatsScreen() {
    tabT_.set(1);
    compute();
}

void StatsScreen::compute() {
    const auto& sessions = app.stats.sessions();
    for (auto& r : rows_) r.clear();
    recentEntries_.clear();

    double total = 0, longest = 0, bootTotal = 0;
    std::unordered_map<std::string, std::pair<double, int>> perEntry;
    for (const auto& s : sessions) {
        total += s.duration;
        longest = std::max(longest, s.duration);
        if (s.boot == app.stats.boot()) bootTotal += s.duration;
        auto& p = perEntry[s.id];
        p.first += s.duration;
        ++p.second;
    }

    tiles_[0] = {formatDuration(total), "Total play time"};
    tiles_[1] = {std::to_string(sessions.size()), "Sessions"};
    tiles_[2] = {std::to_string(perEntry.size()), "Played entries"};
    tiles_[3] = {longest > 0 ? formatDuration(longest) : "-", "Longest session"};
    bootTime_ = formatDuration(bootTotal);
    subtitle_ = sessions.empty() ? "No sessions recorded"
                                 : strprintf("%s in %zu sessions", formatDuration(total).c_str(), sessions.size());

    for (const auto& id : app.stats.recent(4)) {
        const EntryStats* es = app.stats.get(id);
        Row r;
        r.id = id;
        r.title = titleOf(id);
        if (es && es->last) {
            r.sub = app.stats.describeLast(*es->last);
            r.right = es->last->unknown ? "unknown" : formatDuration(es->last->duration);
        }
        recentEntries_.push_back(std::move(r));
    }

    std::vector<std::pair<std::string, std::pair<double, int>>> sorted(perEntry.begin(), perEntry.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.second.first != b.second.first) return a.second.first > b.second.first;
        return a.second.second > b.second.second;
    });
    double maxTotal = sorted.empty() ? 0 : sorted.front().second.first;
    int rank = 0;
    for (const auto& [id, p] : sorted) {
        Row r;
        r.id = id;
        r.title = titleOf(id);
        r.rank = ++rank;
        r.right = formatDuration(p.first);
        r.sub = strprintf("%d %s", p.second, p.second == 1 ? "session" : "sessions");
        r.bar = maxTotal > 0 ? (float)(p.first / maxTotal) : 0.f;
        rows_[kMostPlayed].push_back(std::move(r));
    }

    struct CategoryTotal {
        double time = 0;
        int sessions = 0;
        std::set<std::string> ids;
    };
    std::map<std::string, CategoryTotal> perCategory;
    for (const auto& s : sessions) {
        const Entry* e = app.catalog.find(s.id);
        CategoryTotal& c = perCategory[e ? e->category : "Removed entries"];
        c.time += s.duration;
        ++c.sessions;
        c.ids.insert(s.id);
    }
    std::vector<std::pair<std::string, CategoryTotal>> cats(perCategory.begin(), perCategory.end());
    std::sort(cats.begin(), cats.end(), [](const auto& a, const auto& b) { return a.second.time > b.second.time; });
    double maxCat = cats.empty() ? 0 : cats.front().second.time;
    rank = 0;
    for (const auto& [name, c] : cats) {
        Row r;
        r.title = name;
        r.rank = ++rank;
        r.right = formatDuration(c.time);
        r.sub = strprintf("%d %s, %zu %s", c.sessions, c.sessions == 1 ? "session" : "sessions", c.ids.size(),
                          c.ids.size() == 1 ? "entry" : "entries");
        r.bar = maxCat > 0 ? (float)(c.time / maxCat) : 0.f;
        rows_[kCategories].push_back(std::move(r));
    }

    int n = 0;
    for (auto it = sessions.rbegin(); it != sessions.rend() && n < 200; ++it, ++n) {
        Row r;
        r.id = it->id;
        r.title = titleOf(it->id);
        r.right = it->unknown ? "unknown" : formatDuration(it->duration);
        std::string exit = exitText(*it);
        r.sub = app.stats.describeLast(*it) + (exit.empty() ? "" : "  \xC2\xB7  " + exit);
        rows_[kRecent].push_back(std::move(r));
    }

    for (int t = 0; t < kTabCount; ++t)
        selected_[t] = rows_[t].empty() ? 0 : std::clamp(selected_[t], 0, (int)rows_[t].size() - 1);
}

void StatsScreen::setTab(int tab) {
    tab = (tab % kTabCount + kTabCount) % kTabCount;
    if (tab == tab_) return;
    prevTab_ = tab_;
    tab_ = tab;
    tabT_.set(0);
    tabT_.go(1, 0.2f);
    placed_ = false;
}

void StatsScreen::input(const KeyEvent& ev) {
    int count = (int)rows().size();
    int& sel = selected_[tab_];
    switch (ev.key) {
        case Key::Back: close(); break;
        case Key::L1: setTab(tab_ - 1); break;
        case Key::R1: setTab(tab_ + 1); break;
        case Key::Left:
            if (!ev.repeat) setTab(tab_ - 1);
            break;
        case Key::Right:
            if (!ev.repeat) setTab(tab_ + 1);
            break;
        case Key::Up:
            if (tab_ == kBattery) boot_ = std::min(boot_ + 1, std::max(0, (int)batteryLog.boots().size() - 1));
            else if (count) sel = ev.repeat ? std::max(0, sel - 1) : (sel - 1 + count) % count;
            break;
        case Key::Down:
            if (tab_ == kBattery) boot_ = std::max(0, boot_ - 1);
            else if (count) sel = ev.repeat ? std::min(count - 1, sel + 1) : (sel + 1) % count;
            break;
        case Key::L2:
            if (count) sel = std::max(0, sel - 6);
            break;
        case Key::R2:
            if (count) sel = std::min(count - 1, sel + 6);
            break;
        case Key::Confirm:
            if (!ev.repeat && count && tab_ != kOverview) {
                const std::string& id = rows()[sel].id;
                if (!id.empty() && app.catalog.find(id)) app.push(std::make_unique<DetailsScreen>(id));
            }
            break;
        default: break;
    }
}

std::vector<Hint> StatsScreen::hints() const {
    std::vector<Hint> h;
    if (tab_ != kOverview && !rows().empty()) {
        const std::string& id = rows()[selected_[tab_]].id;
        if (!id.empty() && app.catalog.find(id)) h.push_back({Key::Confirm, "Details"});
    }
    if (tab_ == kBattery && batteryLog.boots().size() > 1) h.push_back({Key::Up, "Earlier boot"});
    h.push_back({Key::R1, "Tab"});
    h.push_back({Key::Back, "Back"});
    return h;
}

void StatsScreen::drawTabs(float dx, float alpha) {
    float x = 24 + dx, h = 28;
    for (int i = 0; i < kTabCount; ++i) {
        float w = gfx.measure(Font::SmallBold, kTabNames[i]) + 24;
        bool active = i == tab_;
        if (active) gfx.round({x, kTabsY, w, h}, h / 2, accent(alpha));
        else gfx.round({x, kTabsY, w, h}, h / 2, fade(theme::panel, 0.7f * alpha));
        gfx.text(Font::SmallBold, kTabNames[i], x + w / 2, kTabsY + (h - gfx.lineHeight(Font::SmallBold)) / 2,
                 fade(active ? rgb(0x0b0d12) : theme::dim, alpha), 0, Align::Center);
        x += w + 8;
    }
}

void StatsScreen::drawEmpty(const SDL_FRect& area, float alpha) {
    float cy = area.y + area.h / 2 - 30;
    gfx.icon("chart-column", gfx.W / 2 - 16, cy, 32, fade(theme::dim, alpha));
    gfx.text(Font::Bold, "No play sessions yet", gfx.W / 2, cy + 44, fade(theme::text, alpha), 0, Align::Center);
    gfx.text(Font::Small, "Play something and it shows up here.", gfx.W / 2, cy + 68, fade(theme::dim, alpha), 0,
             Align::Center);
}

void StatsScreen::drawOverview(const SDL_FRect& area, float alpha) {
    if (app.stats.sessions().empty()) return drawEmpty(area, alpha);
    float gap = 10, tw = (area.w - 3 * gap) / 4, th = 70;
    for (int i = 0; i < 4; ++i) {
        SDL_FRect r{area.x + i * (tw + gap), area.y, tw, th};
        gfx.round(r, 12, fade(theme::panel, 0.9f * alpha));
        gfx.text(Font::Title, tiles_[i].value, r.x + 12, r.y + 10, fade(theme::text, alpha), r.w - 20);
        gfx.text(Font::Small, tiles_[i].label, r.x + 12, r.y + 44, fade(theme::dim, alpha), r.w - 20);
    }

    float y = area.y + th + 16;
    gfx.icon("timer", area.x, y, 16, fade(theme::dim, alpha));
    gfx.text(Font::Small, "This boot", area.x + 22, y, fade(theme::dim, alpha));
    gfx.text(Font::SmallBold, bootTime_, area.x + area.w, y, fade(theme::text, alpha), 0, Align::Right);
    y += 30;

    gfx.text(Font::Tiny, "RECENTLY PLAYED", area.x, y, fade(theme::faint, alpha));
    y += 20;
    for (const auto& r : recentEntries_) {
        if (y + 36 > area.y + area.h) break;
        gfx.round({area.x, y, area.w, 36}, 10, fade(theme::panel, 0.6f * alpha));
        gfx.text(Font::SmallBold, r.title, area.x + 12, y + 9, fade(theme::text, alpha), area.w * 0.5f);
        float rw = gfx.text(Font::SmallBold, r.right, area.x + area.w - 12, y + 9, fade(theme::text, alpha), 0, Align::Right);
        gfx.text(Font::Small, r.sub, area.x + area.w - 12 - rw - 16, y + 9, fade(theme::dim, alpha), area.w * 0.3f,
                 Align::Right);
        y += 42;
    }
}

void StatsScreen::drawBattery(const SDL_FRect& area, float alpha) {
    const Battery& b = sys.battery;
    const auto& boots = batteryLog.boots();
    if (b.capacity < 0 && boots.empty()) {
        float cy = area.y + area.h / 2 - 30;
        gfx.icon("battery", gfx.W / 2 - 16, cy, 32, fade(theme::dim, alpha));
        gfx.text(Font::Bold, "No battery", gfx.W / 2, cy + 44, fade(theme::text, alpha), 0, Align::Center);
        return;
    }

    BatteryEstimate est = batteryLog.estimate(b);
    std::string left = "-", leftLabel = "Time left";
    if (b.status == "Charging") leftLabel = "Until full";
    if (est.seconds >= 0) left = formatDuration(std::max(300.0, std::round(est.seconds / 300) * 300));
    Tile tiles[3] = {{b.capacity >= 0 ? strprintf("%d%%", b.capacity) : "-", b.status.empty() ? "Battery" : b.status},
                     {left, leftLabel},
                     {est.ratePerHour != 0 ? strprintf("%+.1f%%", est.ratePerHour) : "-",
                      est.fromHistory ? "Per hour, last hour" : "Per hour, current"}};
    float gap = 10, tw = (area.w - 2 * gap) / 3, th = 62;
    for (int i = 0; i < 3; ++i) {
        SDL_FRect r{area.x + i * (tw + gap), area.y, tw, th};
        gfx.round(r, 12, fade(theme::panel, 0.9f * alpha));
        gfx.text(Font::Title, tiles[i].value, r.x + 12, r.y + 6, fade(theme::text, alpha), r.w - 20);
        gfx.text(Font::Small, tiles[i].label, r.x + 12, r.y + 38, fade(theme::dim, alpha), r.w - 20);
    }

    SDL_FRect g{area.x, area.y + th + 12, area.w, area.h - th - 12};
    gfx.round(g, 12, fade(theme::panel, 0.6f * alpha));
    if (boots.empty()) return;
    boot_ = std::clamp(boot_, 0, (int)boots.size() - 1);
    const BatteryBoot& boot = boots[boots.size() - 1 - boot_];
    const auto& v = boot.samples;
    bool current = &boot == batteryLog.current();
    std::string label = current ? "This boot" : strprintf("%d %s earlier", boot_, boot_ == 1 ? "boot" : "boots");
    gfx.text(Font::SmallBold, label, g.x + 12, g.y + 8, fade(theme::text, alpha));

    SDL_FRect plot{g.x + 40, g.y + 34, g.w - 56, g.h - 34 - 26};
    for (int pct = 0; pct <= 100; pct += 25) {
        float y = std::round(plot.y + plot.h * (1 - pct / 100.f));
        gfx.fill({plot.x, y, plot.w, 1}, fade(theme::line, 0.7f * alpha));
        gfx.text(Font::Tiny, std::to_string(pct), plot.x - 8, y - gfx.lineHeight(Font::Tiny) / 2, fade(theme::faint, alpha), 0,
                 Align::Right);
    }
    if (v.empty()) return;
    double t0 = v.front().uptime, t1 = std::max(v.back().uptime, t0 + 3600);
    gfx.text(Font::Tiny, formatClock(t0), plot.x, plot.y + plot.h + 6, fade(theme::faint, alpha));
    gfx.text(Font::Tiny, formatClock(t1), plot.x + plot.w, plot.y + plot.h + 6, fade(theme::faint, alpha), 0, Align::Right);
    gfx.text(Font::Tiny, "uptime", plot.x + plot.w / 2, plot.y + plot.h + 6, fade(theme::faint, alpha), 0, Align::Center);

    // One 2 px column per step, linear between samples (also across sleep or gaps in the log).
    const float step = 2;
    size_t j = 0;
    for (float x = 0; x < plot.w; x += step) {
        double t = t0 + (t1 - t0) * (x + step / 2) / plot.w;
        if (t > v.back().uptime) break;
        while (j + 1 < v.size() && v[j + 1].uptime < t) ++j;
        if (t < v[j].uptime) continue;
        const BatterySample& a = v[j];
        const BatterySample& n = j + 1 < v.size() ? v[j + 1] : v[j];
        double f = n.uptime > a.uptime ? (t - a.uptime) / (n.uptime - a.uptime) : 0;
        float cap = (float)(a.capacity + (n.capacity - a.capacity) * f);
        float y = plot.y + plot.h * (1 - std::clamp(cap, 0.f, 100.f) / 100);
        Color c = a.status == 'C' || a.status == 'F' ? theme::good : accent();
        gfx.fill({plot.x + x, y, step, plot.y + plot.h - y}, fade(c, 0.22f * alpha));
        gfx.fill({plot.x + x, y - 1, step, 2}, fade(c, alpha));
    }
}

void StatsScreen::drawList(const SDL_FRect& area, float alpha) {
    const auto& list = rows();
    if (list.empty()) return drawEmpty(area, alpha);
    int sel = selected_[tab_];
    float content = list.size() * kRowH;
    float target = std::clamp(sel * kRowH + kRowH / 2 - area.h / 2, 0.f, std::max(0.f, content - area.h));
    if (!placed_) {
        scroll_.set(target);
        cursor_.set(sel * kRowH);
        placed_ = true;
    }
    scroll_.go(target, 0.18f);
    cursor_.go(sel * kRowH, 0.14f);
    float scroll = scroll_.update(), cy = cursor_.update();

    gfx.pushClip(area);
    SDL_FRect hl{area.x, area.y + cy - scroll + 2, area.w - 10, kRowH - 4};
    gfx.round(hl, 10, accent(0.16f * alpha));
    gfx.round({hl.x, hl.y + 9, 3, hl.h - 18}, 1.5f, accent(alpha));

    bool bars = tab_ != kRecent;
    for (int i = 0; i < (int)list.size(); ++i) {
        float y = area.y + i * kRowH - scroll;
        if (y + kRowH < area.y || y > area.y + area.h) continue;
        const Row& r = list[i];
        float x = area.x + 14, right = area.x + area.w - 24;
        if (r.rank > 0) {
            gfx.text(Font::SmallBold, std::to_string(r.rank), x + 18, y + 5, fade(theme::faint, alpha), 0, Align::Right);
            x += 30;
        }
        float rw = gfx.text(Font::SmallBold, r.right, right, y + 5, fade(theme::text, alpha), 0, Align::Right);
        gfx.text(Font::SmallBold, r.title, x, y + 5, fade(theme::text, alpha), right - rw - x - 16);
        float lineY = y + 5 + gfx.lineHeight(Font::SmallBold) + 3;
        if (bars && r.bar >= 0) {
            float bw = (right - x) * 0.55f;
            gfx.round({x, lineY + 3, bw, 4}, 2, fade(theme::line, alpha));
            gfx.round({x, lineY + 3, std::max(4.f, bw * r.bar), 4}, 2, accent(alpha));
            gfx.text(Font::Tiny, r.sub, right, lineY - 1, fade(theme::dim, alpha), (right - x) * 0.4f, Align::Right);
        } else {
            gfx.text(Font::Tiny, r.sub, x, lineY - 1, fade(theme::dim, alpha), right - x);
        }
    }
    gfx.popClip();
    drawScrollbar({area.x + area.w - 6, area.y + 4, 3, area.h - 8}, scroll, content, area.h, alpha);
}

void StatsScreen::draw() {
    float t = shown.value;
    float dx = 24 * (1 - t);
    drawBackground(t);
    drawPageHeader("chart-column", "Statistics", subtitle_, t);
    drawTabs(dx, t);

    float tt = tabT_.update();
    int dir = tab_ > prevTab_ ? 1 : -1;
    float shift = 16 * (1 - tt) * dir;
    SDL_FRect area{24 + dx + shift, kListY, gfx.W - 48, gfx.H - kListY - kBottom - 6};
    float a = t * tt;
    if (tab_ == kOverview) drawOverview(area, a);
    else if (tab_ == kBattery) drawBattery(area, a);
    else drawList({area.x - 12, area.y, area.w + 24, area.h}, a);
}

void App::openStatistics() { push(std::make_unique<StatsScreen>()); }
