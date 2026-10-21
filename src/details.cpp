#include "details.h"

#include "art.h"
#include "expert.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "util.h"

DetailsScreen::DetailsScreen(std::string id) : id_(std::move(id)) {
    menu_.compact = true;
    menu_.rowHeight = 36;
    build();
}

void DetailsScreen::build() {
    const Entry* e = app.catalog.find(id_);
    menu_.items.clear();
    if (!e) return;
    std::string id = id_;
    menu_.items.push_back(items::action("play", "Play", [id] {
        if (const Entry* e = app.catalog.find(id)) app.launch(*e);
    }));
    menu_.items.push_back(items::toggle("star", "Favorite", [id] { return app.stats.isFavorite(id); },
                                        [id](bool on) {
                                            if (on != app.stats.isFavorite(id)) app.stats.toggleFavorite(id);
                                        }));
    menu_.items.push_back(items::toggle("eye-off", "Hide from Home", [id] { return app.stats.isHidden(id); },
                                        [id](bool on) {
                                            app.stats.setHidden(id, on);
                                            if (on) app.toast("Hidden. Settings > Hidden entries shows it again", "eye-off");
                                        }));
    if (!platform.autolaunch.empty())
        menu_.items.push_back(items::toggle("zap", "Start at boot", [id] { return app.autolaunchId() == id; },
                                            [id](bool on) {
                                                bool cur = app.autolaunchId() == id;
                                                if (on == cur) return;
                                                app.setAutolaunch(on ? app.catalog.find(id) : nullptr);
                                            }));
    if (!platform.appLog.empty())
        menu_.items.push_back(items::link("file-text", "Last app log", [] { app.openLog("Last app log", paths.expand(platform.appLog)); }));
    if (app.stats.get(id_))
        menu_.items.push_back(items::action("undo-2", "Reset statistics", [id] {
            app.confirm("undo-2", "Reset statistics?", "Play time and sessions of this entry are deleted.", "Reset",
                        [id] { app.stats.reset(id); });
        }, true));
    if (settings.expert) {
        menu_.items.push_back(items::action("terminal", "Run in terminal", [id] {
            if (const Entry* e = app.catalog.find(id)) app.launch(*e, true);
        }));
        menu_.items.push_back(items::link("code", "Entry internals", [id] {
            app.push(std::make_unique<TextScreen>("Entry", "code", [id] {
                const Entry* e = app.catalog.find(id);
                if (!e) return std::string("entry not found");
                std::string out;
                out += "ID        " + e->id + "\n";
                out += "Category  " + e->category + "\n";
                out += "Name      " + e->name + "\n";
                out += "Source    " + e->source + ":" + std::to_string(e->line) + "\n";
                if (!e->path.empty()) out += "File      " + e->path + "\n";
                out += "Art       " + (e->art.empty() ? std::string("-") : e->art) + "\n";
                out += "Fallback  " + (e->fallbackArt.empty() ? std::string("-") : e->fallbackArt) + "\n";
                out += "Terminal  " + std::string(e->terminal ? "yes" : "no") + "\n\n";
                out += "Command\n" + e->command + "\n\nLaunched as\n" + app.wrapCommand(*e) + "\n";
                return out;
            }));
        }));
    }
    menu_.fixSelection();
}

void DetailsScreen::input(const KeyEvent& ev) {
    if (ev.key == Key::Back || ev.key == Key::X) {
        close();
        return;
    }
    menu_.input(ev);
}

std::vector<Hint> DetailsScreen::hints() const { return {{Key::Confirm, "Select"}, {Key::Back, "Back"}}; }

void DetailsScreen::draw() {
    float t = shown.value;
    drawBackground(t);
    const Entry* e = app.catalog.find(id_);
    if (!e) return;
    const Category* c = app.catalog.category(e->category);

    float ready = 0;
    if (SDL_Texture* bd = art.backdrop(*e, c, ready)) gfx.image(bd, {-20, -20, gfx.W + 40, gfx.H + 40}, 0.5f * t * ready);
    gfx.gradient({0, 0, gfx.W, gfx.H}, rgb(0x0a0c11, 0.45f * t), rgb(0x0a0c11, 0.9f * t));

    float dx = 20 * (1 - t);
    SDL_FRect card{24 - dx, 48, ArtCache::kW * 0.9f, ArtCache::kH * 0.9f};
    gfx.shadow(card, 13, 16, rgb(0, 0.55f * t));
    auto cd = art.card(*e, c);
    if (cd.stock) gfx.image(cd.stock, card, t);
    if (cd.art) gfx.image(cd.art, card, t * cd.artAlpha);

    // Statistics under the card.
    std::vector<std::pair<std::string, std::string>> rows;
    const EntryStats* s = app.stats.get(id_);
    if (s && s->launches) {
        rows.push_back({"Play time", formatDuration(s->total)});
        rows.push_back({"Sessions", std::to_string(s->launches)});
        if (s->last) {
            rows.push_back({"Last played", app.stats.describeLast(*s->last)});
            const Session& l = *s->last;
            rows.push_back({"Last session", l.unknown ? "unknown length" : formatDuration(l.duration)});
            std::string exit = l.unknown ? "-" : l.killed ? "killed" : l.rc == 0 ? "normal" : strprintf("code %d", l.rc);
            rows.push_back({"Last exit", exit});
        }
        if (s->firstWall > 0) {
            time_t ft = (time_t)s->firstWall;
            tm lt{};
            localtime_r(&ft, &lt);
            rows.push_back({"First played", strprintf("%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday)});
        }
    } else {
        rows.push_back({"Play time", "never played"});
    }
    float y = card.y + card.h + 18;
    for (const auto& [k, v] : rows) {
        gfx.text(Font::Small, k, card.x, y, fade(theme::faint, t));
        gfx.text(Font::SmallBold, v, card.x + card.w, y, fade(theme::text, t), card.w - 100, Align::Right);
        y += 22;
    }

    // Title, description and actions on the right.
    float rx = card.x + card.w + 24 + dx * 2, rw = gfx.W - rx - 20;
    float ty = 46;
    auto lines = gfx.wrap(Font::Title, e->title, rw, 2);
    for (const auto& l : lines) {
        gfx.text(Font::Title, l, rx, ty, fade(theme::text, t));
        ty += gfx.lineHeight(Font::Title);
    }
    std::string sub = e->category;
    if (!e->tags.empty()) sub += "  \xC2\xB7  " + e->tags;
    gfx.text(Font::Small, sub, rx, ty + 2, fade(theme::dim, t), rw);
    ty += 24;
    if (!e->description.empty() && e->description != e->category)
        ty += gfx.paragraph(Font::Small, e->description, rx, ty, rw, fade(theme::faint, t), 2) + 4;
    ty = std::max(ty + 8, 150.f);
    menu_.draw({rx - 12, ty, rw + 20, gfx.H - 40 - ty}, t);
}
