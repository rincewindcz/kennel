#include "home.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "art.h"
#include "details.h"
#include "panels.h"
#include "platform.h"
#include "settings.h"
#include "util.h"
#include "widgets.h"

namespace {
constexpr float kCardW = ArtCache::kW, kCardH = ArtCache::kH;
constexpr float kSideScale = 0.68f;
constexpr float kNearPitch = kCardW / 2 + 18 + kCardW * kSideScale / 2;
constexpr float kFarPitch = kCardW * kSideScale + 14;
constexpr float kCarouselY = 206;
constexpr double kBackdropSettle = 0.18;

float cardX(float d) {
    float a = std::fabs(d);
    float x = a <= 1 ? kNearPitch * a : kNearPitch + (a - 1) * kFarPitch;
    return d < 0 ? -x : x;
}

std::string firstLetter(const std::string& title) {
    for (unsigned char c : title)
        if (std::isalnum(c)) return std::string(1, (char)std::toupper(c));
    return "#";
}
}  // namespace

HomeScreen::HomeScreen() {
    rebuild();
    info_.set(1);
    backdropFade_.set(1);
}

const Entry* HomeScreen::entryAt(const Row& row, int i) const {
    if (i < 0 || i >= (int)row.entries.size()) return nullptr;
    return &app.catalog.entries[row.entries[i]];
}

const Entry* HomeScreen::selected() const {
    if (rows_.empty()) return nullptr;
    const Row& r = rows_[row_];
    return entryAt(r, r.index);
}

const Category* HomeScreen::categoryOf(const Entry& e) const { return app.catalog.category(e.category); }

void HomeScreen::rebuild() {
    std::string keepRow = rows_.empty() ? "" : rows_[row_].key;
    const Entry* cur = selected();
    std::string keepId = cur ? cur->id : "";
    std::vector<std::pair<std::string, std::string>> indexById;
    for (const auto& r : rows_)
        if (const Entry* e = entryAt(r, r.index)) indexById.emplace_back(r.key, e->id);

    const Catalog& cat = app.catalog;
    rows_.clear();

    auto visible = [&](int idx) { return !cat.entries[idx].hidden; };
    if (settings.showContinue) {
        Row r{Kind::Continue, "continue", "Continue", "history", "Recently played", settings.accentColor()};
        for (const auto& id : app.stats.recent(20)) {
            int idx = cat.indexOf(id);
            if (idx >= 0 && visible(idx)) r.entries.push_back(idx);
        }
        if (!r.entries.empty()) rows_.push_back(std::move(r));
    }
    if (settings.showFavorites) {
        Row r{Kind::Favorites, "favorites", "Favorites", "star", "", 0xffb020};
        for (const auto& id : app.stats.favorites()) {
            int idx = cat.indexOf(id);
            if (idx >= 0 && visible(idx)) r.entries.push_back(idx);
        }
        std::sort(r.entries.begin(), r.entries.end(),
                  [&](int a, int b) { return lower(cat.entries[a].title) < lower(cat.entries[b].title); });
        if (!r.entries.empty()) rows_.push_back(std::move(r));
    }

    std::vector<const Category*> ordered;
    for (const auto& name : settings.order)
        if (const Category* c = cat.category(name)) ordered.push_back(c);
    auto isTail = [](const Category& c) {
        return std::find(platform.tailCategories.begin(), platform.tailCategories.end(), c.name) != platform.tailCategories.end();
    };
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& c : cat.categories)
            if (isTail(c) == (pass == 1) && std::find(ordered.begin(), ordered.end(), &c) == ordered.end()) ordered.push_back(&c);

    for (const Category* c : ordered) {
        if (std::find(settings.hidden.begin(), settings.hidden.end(), c->name) != settings.hidden.end()) continue;
        Row r{Kind::Category, "cat:" + c->name, c->name, c->glyph, c->description, c->color, c};
        for (int idx : c->entries)
            if (visible(idx)) r.entries.push_back(idx);
        if (r.entries.empty()) continue;
        auto lastSeq = [&](int idx) {
            const EntryStats* s = app.stats.get(cat.entries[idx].id);
            return s && s->last ? s->last->seq : 0L;
        };
        auto total = [&](int idx) {
            const EntryStats* s = app.stats.get(cat.entries[idx].id);
            return s ? s->total : 0.0;
        };
        if (settings.sort == Settings::Sort::LastPlayed)
            std::stable_sort(r.entries.begin(), r.entries.end(), [&](int a, int b) { return lastSeq(a) > lastSeq(b); });
        else if (settings.sort == Settings::Sort::PlayTime)
            std::stable_sort(r.entries.begin(), r.entries.end(), [&](int a, int b) { return total(a) > total(b); });
        rows_.push_back(std::move(r));
    }

    for (auto& r : rows_) {
        for (const auto& [key, id] : indexById) {
            if (key != r.key) continue;
            for (size_t i = 0; i < r.entries.size(); ++i)
                if (cat.entries[r.entries[i]].id == id) r.index = (int)i;
        }
    }
    row_ = 0;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].key == keepRow) row_ = (int)i;
    if (!rows_.empty()) {
        Row& r = rows_[row_];
        r.index = std::clamp(r.index, 0, (int)r.entries.size() - 1);
        for (size_t i = 0; i < r.entries.size(); ++i)
            if (cat.entries[r.entries[i]].id == keepId) r.index = (int)i;
        scroll_.set((float)r.index);
    }
    prevRow_ = -1;
}

void HomeScreen::restore(const std::string& state) {
    auto lines = split(state, '\n');
    if (lines.size() < 2) return;
    for (size_t r = 0; r < rows_.size(); ++r) {
        if (rows_[r].key != lines[0]) continue;
        row_ = (int)r;
        for (size_t i = 0; i < rows_[r].entries.size(); ++i)
            if (app.catalog.entries[rows_[r].entries[i]].id == lines[1]) rows_[r].index = (int)i;
        scroll_.set((float)rows_[r].index);
        changedAt_ = 0;
        return;
    }
}

std::string HomeScreen::stateString() const {
    const Entry* e = selected();
    if (!e) return {};
    return rows_[row_].key + "\n" + e->id + "\n";
}

void HomeScreen::resumed() {
    // Back from settings or details: favourites, sorting and visibility may have changed.
    rebuild();
}

void HomeScreen::select(int index, bool animate) {
    Row& r = rows_[row_];
    index = std::clamp(index, 0, (int)r.entries.size() - 1);
    if (index == r.index) return;
    r.index = index;
    if (animate) scroll_.go((float)index, 0.22f);
    else scroll_.set((float)index);
    info_.set(0);
    info_.go(1, 0.18f);
    changedAt_ = anim::now();
}

void HomeScreen::moveRow(int dir) {
    int next = std::clamp(row_ + dir, 0, (int)rows_.size() - 1);
    if (next == row_) return;
    prevRow_ = row_;
    prevScroll_ = scroll_.value;
    rowDir_ = dir;
    row_ = next;
    scroll_.set((float)rows_[row_].index);
    rowT_.set(0);
    rowT_.go(1, 0.24f);
    info_.set(0);
    info_.go(1, 0.2f);
    changedAt_ = anim::now();
}

void HomeScreen::jumpLetter(int dir) {
    Row& r = rows_[row_];
    if (r.entries.empty()) return;
    std::string cur = firstLetter(entryAt(r, r.index)->title);
    int i = r.index;
    if (dir > 0) {
        while (i < (int)r.entries.size() && firstLetter(entryAt(r, i)->title) == cur) ++i;
        if (i >= (int)r.entries.size()) i = (int)r.entries.size() - 1;
    } else {
        // Start of the current letter group, or of the previous one when already there.
        while (i > 0 && firstLetter(entryAt(r, i - 1)->title) == cur) --i;
        if (i == r.index && i > 0) {
            std::string prev = firstLetter(entryAt(r, i - 1)->title);
            while (i > 0 && firstLetter(entryAt(r, i - 1)->title) == prev) --i;
        }
    }
    select(i);
    app.toast(firstLetter(entryAt(r, r.index)->title), "type");
}

void HomeScreen::input(const KeyEvent& ev) {
    if (rows_.empty()) return;
    Row& r = rows_[row_];
    int n = (int)r.entries.size();
    switch (ev.key) {
        case Key::Left:
            if (r.index == 0 && !ev.repeat && n > 2) select(n - 1, false);
            else select(r.index - 1);
            break;
        case Key::Right:
            if (r.index == n - 1 && !ev.repeat && n > 2) select(0, false);
            else select(r.index + 1);
            break;
        case Key::L1: select(r.index - 5); break;
        case Key::R1: select(r.index + 5); break;
        case Key::L2: jumpLetter(-1); break;
        case Key::R2: jumpLetter(1); break;
        case Key::Up: moveRow(-1); break;
        case Key::Down: moveRow(1); break;
        case Key::Back:
            if (r.index > 0) select(0);
            else moveRow(-row_);
            break;
        case Key::Confirm:
            if (const Entry* e = selected()) app.launch(*e);
            break;
        case Key::X:
            if (const Entry* e = selected()) app.push(std::make_unique<DetailsScreen>(e->id));
            break;
        case Key::Y:
            if (const Entry* e = selected()) {
                bool fav = !app.stats.isFavorite(e->id);
                app.stats.toggleFavorite(e->id);
                app.toast(fav ? "Added to Favorites" : "Removed from Favorites", fav ? "star-filled" : "star",
                          fav ? rgb(0xffb020) : theme::dim);
                std::string id = e->id, key = r.key;
                rebuild();
                // Stay on the same game, in the same row if it still exists.
                for (size_t i = 0; i < rows_.size(); ++i) {
                    if (rows_[i].key != key) continue;
                    row_ = (int)i;
                    for (size_t j = 0; j < rows_[i].entries.size(); ++j)
                        if (app.catalog.entries[rows_[i].entries[j]].id == id) rows_[i].index = (int)j;
                    scroll_.set((float)rows_[i].index);
                }
            }
            break;
        case Key::Start: app.push(std::make_unique<QuickPanel>()); break;
        default: break;
    }
}

std::vector<Hint> HomeScreen::hints() const {
    const Entry* e = selected();
    if (!e) return {{Key::Start, "Quick settings"}};
    return {{Key::Confirm, "Play"},
            {Key::X, "Details"},
            {Key::Y, app.stats.isFavorite(e->id) ? "Unfavorite" : "Favorite"},
            {Key::Start, "Quick"}};
}

void HomeScreen::drawBackdrop() {
    drawBackground();
    if (settings.background != Settings::Background::Art || rows_.empty()) return;

    const Entry* e = selected();
    std::string want = e ? e->id : "";
    if (want != backdropId_ && anim::now() - changedAt_ >= kBackdropSettle) {
        prevBackdropId_ = backdropId_;
        backdropId_ = want;
        backdropFade_.set(0);
        backdropFade_.go(1, 0.35f);
    } else if (want != backdropId_) {
        anim::keepAlive();
    }
    float t = backdropFade_.update();

    auto layer = [&](const std::string& id, float a) {
        const Entry* le = app.catalog.find(id);
        if (!le || a <= 0) return;
        float ready = 0;
        SDL_Texture* tex = art.backdrop(*le, categoryOf(*le), ready);
        if (tex) gfx.image(tex, {-20, -20, gfx.W + 40, gfx.H + 40}, a * ready * 0.5f);
    };
    if (t < 1) layer(prevBackdropId_, 1 - t);
    layer(backdropId_, t);
    gfx.gradient({0, 0, gfx.W, gfx.H}, rgb(0x0a0c11, 0.55f), rgb(0x0a0c11, 0.88f));
}

void HomeScreen::drawHeader(const Row& row, float dy, float alpha) {
    float x = 24, y = 40 + dy;
    Color c = row.kind == Kind::Category ? mix(rgb(row.color), theme::text, 0.55f) : rgb(row.color);
    gfx.icon(row.glyph, x, y + 6, 20, fade(c, alpha));
    gfx.text(Font::Title, row.name, x + 28, y, fade(theme::text, alpha), gfx.W - 220);
    std::string sub = strprintf("%zu %s", row.entries.size(), row.entries.size() == 1 ? "item" : "items");
    if (!row.subtitle.empty() && row.subtitle != row.name) sub += "  \xC2\xB7  " + row.subtitle;
    gfx.text(Font::Small, sub, x + 28, y + 32, fade(theme::dim, alpha), gfx.W - 220);
}

void HomeScreen::drawRow(const Row& row, float scroll, float dy, float alpha) {
    int n = (int)row.entries.size();
    int first = std::max(0, (int)std::floor(scroll) - 3), last = std::min(n - 1, (int)std::ceil(scroll) + 3);
    std::vector<int> order;
    for (int i = first; i <= last; ++i) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return std::fabs(a - scroll) > std::fabs(b - scroll); });

    for (int i : order) {
        const Entry* e = entryAt(row, i);
        float d = i - scroll, ad = std::fabs(d);
        float s = 1 - (1 - kSideScale) * std::min(ad, 1.f);
        float a = alpha * (1 - 0.45f * std::min(ad, 1.f)) * std::clamp(3.2f - ad, 0.f, 1.f);
        if (a <= 0.01f) continue;
        float w = kCardW * s, h = kCardH * s;
        SDL_FRect r{gfx.W / 2 + cardX(d) - w / 2, kCarouselY + dy - h / 2, w, h};
        if (r.x > gfx.W || r.x + r.w < 0) continue;

        float focus = 1 - std::min(ad, 1.f);
        gfx.shadow(r, 14 * s, 10 + 10 * focus, rgb(0, (0.35f + 0.3f * focus) * a));
        auto card = art.card(*e, categoryOf(*e));
        if (card.stock) gfx.image(card.stock, r, a);
        else if (!card.art || card.artAlpha < 1) gfx.round(r, 14 * s, fade(theme::panel, a));
        if (card.art) gfx.image(card.art, r, a * card.artAlpha);
        if (focus > 0.5f)
            gfx.stroke({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, 17, 2, accent((focus - 0.5f) * 2 * alpha));
        if (app.stats.isFavorite(e->id) && row.kind != Kind::Favorites)
            gfx.icon("star-filled", r.x + r.w - 26 * s - 4, r.y + 8, 20, fade(rgb(0xffb020), a));
    }
    for (int i = std::max(0, last + 1); i < std::min(n, last + 4); ++i) art.prefetch(*entryAt(row, i));
    for (int i = std::max(0, first - 3); i < first; ++i) art.prefetch(*entryAt(row, i));

    // Position: a thin bar under the carousel.
    if (n > 1) {
        float tw = 160, tx = (gfx.W - tw) / 2, ty = kCarouselY + kCardH / 2 + 14 + dy;
        gfx.round({tx, ty, tw, 3}, 1.5f, fade(theme::line, alpha));
        float kw = std::max(12.f, tw / n), kx = tx + (tw - kw) * (scroll / (n - 1));
        gfx.round({kx, ty, kw, 3}, 1.5f, accent(alpha));
    }
}

void HomeScreen::drawInfo(const Row& row, float alpha) {
    const Entry* e = entryAt(row, row.index);
    if (!e) return;
    float t = info_.update();
    float a = alpha * t, dy = 6 * (1 - t);
    float y = kCarouselY + kCardH / 2 + 30 + dy;

    gfx.text(Font::Title, e->title, gfx.W / 2, y, fade(theme::text, a), gfx.W - 60, Align::Center);
    y += 36;

    std::vector<std::pair<std::string, std::string>> parts;
    const EntryStats* s = app.stats.get(e->id);
    if (s && s->launches > 0) {
        if (s->total >= 1) parts.push_back({"clock", formatDuration(s->total)});
        if (s->last) parts.push_back({"history", app.stats.describeLast(*s->last)});
        parts.push_back({"play", strprintf("%d %s", s->launches, s->launches == 1 ? "session" : "sessions")});
    } else {
        parts.push_back({"play", "Never played"});
    }
    float w = 0;
    for (const auto& [icon, text] : parts) w += 20 + gfx.measure(Font::Small, text) + 18;
    w -= 18;
    float x = (gfx.W - w) / 2;
    for (const auto& [icon, text] : parts) {
        gfx.icon(icon, x, y, 16, fade(theme::dim, a));
        x += 20;
        x += gfx.text(Font::Small, text, x, y, fade(theme::dim, a)) + 18;
    }
    y += 22;
    std::string extra = e->tags;
    if (row.kind != Kind::Category) extra = e->category + (extra.empty() ? "" : "  \xC2\xB7  " + extra);
    else if (extra.empty() && !e->fromArchive) extra = e->description;
    if (!extra.empty()) gfx.text(Font::Small, extra, gfx.W / 2, y, fade(theme::faint, a), gfx.W - 80, Align::Center);
}

void HomeScreen::drawEmpty() {
    float y = gfx.H / 2 - 60;
    gfx.icon("library", gfx.W / 2 - 16, y, 32, theme::dim);
    gfx.text(Font::Bold, "Nothing to show yet", gfx.W / 2, y + 44, theme::text, 0, Align::Center);
    std::string where;
    for (const auto& s : platform.sources) where += (where.empty() ? "" : ", ") + s;
    gfx.paragraph(Font::Small, "Add entries in " + where, 60, y + 72, gfx.W - 120, theme::dim, 3);
}

void HomeScreen::draw() {
    drawBackdrop();
    if (rows_.empty()) return drawEmpty();
    float scroll = scroll_.update();
    float t = rowT_.update();
    const float shift = 46;

    const Row& row = rows_[row_];
    if (prevRow_ >= 0 && t < 1) {
        const Row& prev = rows_[prevRow_];
        drawHeader(prev, -rowDir_ * shift * t, 1 - t);
        drawRow(prev, prevScroll_, -rowDir_ * shift * t, 1 - t);
    }
    float dy = prevRow_ >= 0 ? rowDir_ * shift * (1 - t) : 0;
    float a = prevRow_ >= 0 ? t : 1;
    drawHeader(row, dy, a);
    drawRow(row, scroll, dy, a);
    drawInfo(row, a);

    // Neighbouring rows, top right.
    float rx = gfx.W - 22, ry = 44;
    if (row_ > 0) {
        gfx.text(Font::Small, rows_[row_ - 1].name, rx - 20, ry, theme::faint, 150, Align::Right);
        gfx.icon("chevron-up", rx - 16, ry + 1, 16, theme::faint);
    }
    if (row_ + 1 < (int)rows_.size()) {
        gfx.text(Font::Small, rows_[row_ + 1].name, rx - 20, ry + 20, theme::faint, 150, Align::Right);
        gfx.icon("chevron-down", rx - 16, ry + 21, 16, theme::faint);
    }
}
