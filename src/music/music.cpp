#include "music.h"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

#include "anim.h"
#include "gfx.h"
#include "image.h"
#include "ini.h"
#include "menu.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "util.h"

namespace {

constexpr float kMargin = 16, kBarH = 24, kCover = 160, kRowH = 32;
constexpr int kInputPollMs = 10;
constexpr int kPlayingPollMs = 250;  // status poll while playing
constexpr int kMaxDepth = 4;         // album folders below a library folder
constexpr double kRestartAfter = 3;  // Previous restarts the track after this many seconds
const Color kDark = rgb(0x0b0d12);
const char* kImageExts[] = {"jpg", "jpeg", "png", "bmp"};
const char* kCoverNames[] = {"cover", "folder", "front", "album", "albumart"};

// Case-insensitive, runs of digits compared by value: "2 b" < "10 a", "05" == "5".
bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
            while (i < a.size() && a[i] == '0') ++i;
            while (j < b.size() && b[j] == '0') ++j;
            size_t si = i, sj = j;
            while (i < a.size() && isdigit((unsigned char)a[i])) ++i;
            while (j < b.size() && isdigit((unsigned char)b[j])) ++j;
            if (i - si != j - sj) return i - si < j - sj;
            int c = a.compare(si, i - si, b, sj, j - sj);
            if (c) return c < 0;
            continue;
        }
        int ca = tolower((unsigned char)a[i++]), cb = tolower((unsigned char)b[j++]);
        if (ca != cb) return ca < cb;
    }
    return a.size() - i < b.size() - j;
}

std::string stem(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

bool isImage(const std::string& name) {
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = lower(name.substr(dot + 1));
    return std::find_if(std::begin(kImageExts), std::end(kImageExts), [&](const char* e) { return ext == e; }) != std::end(kImageExts);
}

// Visible entries of dir, split into folders and files, in natural order.
void listDir(const std::string& dir, std::vector<std::string>* dirs, std::vector<std::string>* files) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string name = e->d_name;
        struct stat st;
        if (stat((dir + "/" + name).c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (dirs) dirs->push_back(name);
        } else if (files) {
            files->push_back(name);
        }
    }
    closedir(d);
    if (dirs) std::sort(dirs->begin(), dirs->end(), naturalLess);
    if (files) std::sort(files->begin(), files->end(), naturalLess);
}

std::vector<std::string> audioFiles(const std::string& dir) {
    std::vector<std::string> files, out;
    listDir(dir, nullptr, &files);
    for (auto& f : files)
        if (isAudioFile(f)) out.push_back(std::move(f));
    return out;
}

std::string clock(double seconds) {
    long s = (long)seconds;
    if (s >= 3600) return strprintf("%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
    return strprintf("%ld:%02ld", s / 60, s % 60);
}

}  // namespace

// ------------------------------------------------------------- library ---

void Music::scanAlbums() {
    albums_.clear();
    std::function<void(const std::string&, const std::string&, int)> walk = [&](const std::string& dir, const std::string& name, int depth) {
        std::vector<std::string> dirs, files;
        listDir(dir, &dirs, &files);
        int tracks = (int)std::count_if(files.begin(), files.end(), isAudioFile);
        if (tracks) albums_.push_back({dir, name, tracks});
        if (depth < kMaxDepth)
            for (const auto& d : dirs) walk(dir + "/" + d, name.empty() || depth == 0 ? d : name + "/" + d, depth + 1);
    };
    for (const auto& root : roots_) {
        size_t before = albums_.size();
        walk(root, {}, 0);
        // Tracks directly in a library folder: the album is named after the folder.
        for (size_t i = before; i < albums_.size(); ++i)
            if (albums_[i].name.empty()) albums_[i].name = baseName(root);
    }
    std::stable_sort(albums_.begin(), albums_.end(), [](const Album& a, const Album& b) { return naturalLess(a.name, b.name); });
    albumCursor_ = std::clamp(albumCursor_, 0, std::max(0, (int)albums_.size() - 1));
}

void Music::openAlbum(const std::string& dir, const std::string& name, const std::string& select) {
    albumDir_ = dir;
    albumName_ = name;
    tracks_.clear();
    names_.clear();
    for (const auto& f : audioFiles(dir)) {
        tracks_.push_back(dir + "/" + f);
        names_.push_back(stem(f));
    }
    failed_.assign(tracks_.size(), false);
    cursor_ = 0;
    for (size_t i = 0; i < names_.size(); ++i)
        if (baseName(tracks_[i]) == select) cursor_ = (int)i;
    scroll_ = 0;
    inAlbum_ = true;
    loadCover();
    if (audio_) {
        player_.load(tracks_);
        if (!tracks_.empty()) player_.play(cursor_);
    }
    status_ = {};
    status_.track = cursor_;
    followed_ = cursor_;
    dirty_ = true;
}

void Music::closeAlbum() {
    if (audio_) player_.load({});
    if (cover_) SDL_DestroyTexture(cover_);
    cover_ = nullptr;
    inAlbum_ = false;
    dirty_ = true;
}

// cover.jpg, folder.jpg and the like, else the only image in the folder.
void Music::loadCover() {
    if (cover_) SDL_DestroyTexture(cover_);
    cover_ = nullptr;
    std::vector<std::string> files, images;
    listDir(albumDir_, nullptr, &files);
    for (const auto& f : files)
        if (isImage(f)) images.push_back(f);
    std::string pick;
    for (const char* want : kCoverNames)
        for (const auto& f : images)
            if (pick.empty() && lower(stem(f)) == want) pick = f;
    if (pick.empty() && images.size() == 1) pick = images[0];
    if (pick.empty()) return;
    std::string path = albumDir_ + "/" + pick;
    int w, h;
    Image src;
    if (!imageInfo(path, w, h) || (long long)w * h > kMaxImagePixels || !loadImage(path, src)) {
        logf("cover %s: cannot decode", path.c_str());
        return;
    }
    Image img = cover(src, (int)kCover, (int)kCover);
    roundCorners(img, 10);
    cover_ = gfx.texture(img);
}

// After the first frame, so the screen shows while the audio subsystem starts.
void Music::startAudio() {
    audioTried_ = true;
    audio_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!audio_) {
        logf("audio init: %s", SDL_GetError());
        return;
    }
    player_.open();
    player_.setLoop(loop_);
    if (inAlbum_ && !tracks_.empty()) {
        player_.load(tracks_);
        player_.play(cursor_);
    }
}

void Music::loadSettings() {
    settingsPath_ = paths.config + "/music.ini";
    IniFile ini;
    if (!loadIni(settingsPath_, ini)) return;
    std::string loop = ini.get("music", "loop", "off");
    loop_ = loop == "album" ? Loop::Album : loop == "track" ? Loop::Track : Loop::Off;
}

void Music::saveSettings() {
    const char* loop = loop_ == Loop::Album ? "album" : loop_ == Loop::Track ? "track" : "off";
    writeFileAtomic(settingsPath_, strprintf("[music]\nloop=%s\n", loop));
}

// --------------------------------------------------------------- input ---

void Music::input(const KeyEvent& ev) {
    if (inAlbum_) playerInput(ev);
    else listInput(ev);
    dirty_ = true;
}

void Music::listInput(const KeyEvent& ev) {
    int n = (int)albums_.size();
    switch (ev.key) {
        case Key::Up:
            if (n) albumCursor_ = ev.repeat ? std::max(0, albumCursor_ - 1) : (albumCursor_ + n - 1) % n;
            break;
        case Key::Down:
            if (n) albumCursor_ = ev.repeat ? std::min(n - 1, albumCursor_ + 1) : (albumCursor_ + 1) % n;
            break;
        case Key::Confirm:
            if (n && !ev.repeat) {
                fromList_ = true;
                const Album& a = albums_[albumCursor_];
                openAlbum(a.dir, a.name, {});
            }
            break;
        case Key::Back:
            if (!ev.repeat) running_ = false;
            break;
        default: break;
    }
}

void Music::playerInput(const KeyEvent& ev) {
    int n = (int)tracks_.size();
    switch (ev.key) {
        case Key::Up:
            if (n) cursor_ = ev.repeat ? std::max(0, cursor_ - 1) : (cursor_ + n - 1) % n;
            break;
        case Key::Down:
            if (n) cursor_ = ev.repeat ? std::min(n - 1, cursor_ + 1) : (cursor_ + 1) % n;
            break;
        case Key::Confirm:
            if (!n || ev.repeat || !audio_) break;
            if (cursor_ == status_.track) player_.setPaused(!status_.paused);
            else player_.play(cursor_);
            poll();
            break;
        case Key::Left:
        case Key::L1:
            if (!ev.repeat) step(-1);
            break;
        case Key::Right:
        case Key::R1:
            if (!ev.repeat) step(1);
            break;
        case Key::Y:
            if (ev.repeat) break;
            loop_ = loop_ == Loop::Off ? Loop::Album : loop_ == Loop::Album ? Loop::Track : Loop::Off;
            player_.setLoop(loop_);
            saveSettings();
            break;
        case Key::Back:
            if (ev.repeat) break;
            if (fromList_) closeAlbum();
            else running_ = false;
            break;
        default: break;
    }
}

// Previous and next keep the pause state; Previous restarts the track when it has played a while.
void Music::step(int dir) {
    int n = (int)tracks_.size();
    if (!n || !audio_) return;
    int cur = std::max(0, status_.track);
    int next = dir < 0 && status_.position > kRestartAfter ? cur : (cur + dir + n) % n;
    player_.skip(next);
    poll();
}

void Music::poll() {
    if (!inAlbum_ || !audio_) return;
    Player::Status s = player_.status();
    if (s.track != status_.track) {
        // The cursor follows the playing track unless it was moved away from it.
        if (s.track >= 0) {
            if (cursor_ == followed_) cursor_ = s.track;
            followed_ = s.track;
        }
        failed_ = player_.failed();
        dirty_ = true;
    }
    if (s.paused != status_.paused || s.unavailable != status_.unavailable || (long)s.position != shownSecond_ || s.length != status_.length) dirty_ = true;
    shownSecond_ = (long)s.position;
    status_ = s;
}

// Development aid, as in kennel: KENNEL_AUTOMATE="down,a,wait:2000,shot:/tmp/m.bmp,quit".
void Music::automate(std::vector<KeyEvent>& keys) {
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
    dirty_ = true;
}

// ---------------------------------------------------------------- draw ---

void Music::drawRows(float top, float bottom, int count, int cursor, int& scroll, float rowH,
                     const std::function<void(int, const SDL_FRect&, bool)>& row) {
    int visible = std::max(1, (int)((bottom - top) / rowH));
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + visible) scroll = cursor - visible + 1;
    scroll = std::clamp(scroll, 0, std::max(0, count - visible));
    gfx.pushClip({0, top, gfx.W, bottom - top});
    for (int i = scroll; i < count && i < scroll + visible; ++i) {
        SDL_FRect r{kMargin, top + (i - scroll) * rowH, gfx.W - 2 * kMargin, rowH - 2};
        if (i == cursor) gfx.round(r, 8, theme::panelHi);
        row(i, r, i == cursor);
    }
    gfx.popClip();
    if (count > visible) {
        float trackH = bottom - top, h = std::max(16.f, trackH * visible / count);
        float y = top + (trackH - h) * scroll / std::max(1, count - visible);
        gfx.round({gfx.W - 6, y, 3, h}, 1.5f, theme::line);
    }
}

void Music::drawList() {
    gfx.icon("music", kMargin, kMargin + 2, 32, accent());
    gfx.text(Font::Title, "Music", kMargin + 44, kMargin - 2, theme::text);
    std::string sub = albums_.size() == 1 ? "1 album" : strprintf("%d albums", (int)albums_.size());
    gfx.text(Font::Small, sub, kMargin + 44, kMargin + 30, theme::dim);
    float top = kMargin + 64, bottom = gfx.H - kBarH - 8;
    if (albums_.empty()) {
        float y = (top + bottom) / 2 - 24;
        gfx.text(Font::Bold, "No music found", gfx.W / 2, y, theme::text, 0, Align::Center);
        std::string where;
        for (const auto& r : roots_) {
            bool inHome = r.rfind(paths.home + "/", 0) == 0;
            where += (where.empty() ? "" : " or ") + (inHome ? "~" + r.substr(paths.home.size()) : r);
        }
        gfx.paragraph(Font::Small, "Put album folders with MP3, OGG, FLAC or WAV files in " + where + ".", 60, y + 30, gfx.W - 120,
                      theme::dim, 3);
    }
    drawRows(top, bottom, (int)albums_.size(), albumCursor_, albumScroll_, 36, [&](int i, const SDL_FRect& r, bool sel) {
        const Album& a = albums_[i];
        float cy = r.y + r.h / 2;
        gfx.icon("disc-3", r.x + 10, cy - 10, 20, sel ? accent() : theme::dim);
        std::string count = a.tracks == 1 ? "1 track" : strprintf("%d tracks", a.tracks);
        float cw = gfx.measure(Font::Small, count);
        gfx.text(Font::Small, count, r.x + r.w - 12, cy - gfx.lineHeight(Font::Small) / 2, theme::dim, 0, Align::Right);
        gfx.text(Font::Body, a.name, r.x + 40, cy - gfx.lineHeight(Font::Body) / 2, theme::text, r.w - 40 - cw - 28);
    });
    drawBar({{Key::Confirm, "Open"}, {Key::Back, "Exit"}});
}

void Music::drawPlayer() {
    SDL_FRect art{kMargin, kMargin, kCover, kCover};
    if (cover_) {
        gfx.shadow(art, 10, 12, rgb(0, 0.5f));
        gfx.image(cover_, art);
    } else {
        gfx.round(art, 10, theme::panel);
        gfx.icon("disc-3", art.x + (kCover - 32) / 2, art.y + (kCover - 32) / 2, 32, theme::faint);
    }

    float x = art.x + kCover + 20, w = gfx.W - kMargin - x;
    int n = (int)tracks_.size(), cur = status_.track;
    bool stopped = cur < 0 || cur >= n;
    gfx.text(Font::Title, albumName_, x, kMargin - 4, theme::text, w);
    std::string sub = stopped ? strprintf("%d tracks", n) : strprintf("Track %d of %d", cur + 1, n);
    gfx.text(Font::Small, sub, x, kMargin + 30, theme::dim, w);
    if (audioTried_ && (!audio_ || status_.unavailable)) gfx.text(Font::Bold, "No audio device", x, kMargin + 58, theme::bad, w);
    else if (!stopped) gfx.text(Font::Bold, names_[cur], x, kMargin + 58, theme::text, w);

    float by = kMargin + 94;
    double len = status_.length, pos = status_.position;
    gfx.round({x, by, w, 6}, 3, theme::line);
    if (len > 0 && !stopped) gfx.round({x, by, std::max(6.f, w * (float)std::min(1.0, pos / len)), 6}, 3, accent());
    float ty = by + 12;
    if (!stopped) {
        gfx.text(Font::Small, clock(pos), x, ty, theme::dim);
        if (len > 0) gfx.text(Font::Small, clock(len), x + w, ty, theme::dim, 0, Align::Right);
    }

    float iy = kMargin + kCover - 22;
    bool playing = !stopped && !status_.paused;
    gfx.icon(playing ? "play" : "pause", x, iy, 20, playing ? accent() : theme::dim);
    float lx = x + 26 + gfx.text(Font::SmallBold, playing ? "Playing" : stopped ? "Stopped" : "Paused", x + 26, iy + 2, theme::text) + 24;
    const char* loopIcon = loop_ == Loop::Track ? "repeat-1" : "repeat";
    const char* loopText = loop_ == Loop::Album ? "Loop album" : loop_ == Loop::Track ? "Loop track" : "No loop";
    gfx.icon(loopIcon, lx, iy, 20, loop_ == Loop::Off ? theme::faint : accent());
    gfx.text(Font::SmallBold, loopText, lx + 26, iy + 2, loop_ == Loop::Off ? theme::dim : theme::text);

    float top = kMargin + kCover + 14, bottom = gfx.H - kBarH - 8;
    drawRows(top, bottom, n, cursor_, scroll_, kRowH, [&](int i, const SDL_FRect& r, bool) {
        float cy = r.y + r.h / 2;
        bool current = i == cur;
        Color c = failed_[i] ? theme::faint : current ? accent() : theme::text;
        if (current) gfx.icon(status_.paused ? "pause" : "play", r.x + 8, cy - 10, 20, accent());
        else gfx.text(Font::Small, std::to_string(i + 1), r.x + 28, cy - gfx.lineHeight(Font::Small) / 2, theme::dim, 0, Align::Right);
        gfx.text(Font::Body, names_[i], r.x + 40, cy - gfx.lineHeight(Font::Body) / 2, c, r.w - 52);
    });

    bool toggles = cursor_ == cur && !stopped;
    drawBar({{Key::Confirm, toggles ? (status_.paused ? "Play" : "Pause") : "Play"},
             {Key::L1, "Previous"},
             {Key::R1, "Next"},
             {Key::Y, "Loop"},
             {Key::Back, fromList_ ? "Albums" : "Exit"}});
}

void Music::drawBar(const std::vector<Hint>& hints) {
    float y = gfx.H - kBarH;
    gfx.fill({0, y, gfx.W, kBarH}, rgb(0x12151c));
    float x = 8, bh = 16;
    float ty = y + (kBarH - gfx.lineHeight(Font::Tiny)) / 2;
    for (const auto& h : hints) {
        std::string key = ::input.label(h.key);
        float need = gfx.buttonWidth(key, bh) + 4 + gfx.measure(Font::Tiny, h.label) + 10;
        if (x + need > gfx.W) break;
        x += gfx.button(key, x, y + (kBarH - bh) / 2, bh, rgb(0xe8ebf0, 0.9f), kDark) + 4;
        x += gfx.text(Font::Tiny, h.label, x, ty, theme::dim) + 10;
    }
}

void Music::draw() {
    gfx.beginFrame();
    gfx.gradient({0, 0, gfx.W, gfx.H}, theme::bgTop, theme::bgBottom);
    if (inAlbum_) drawPlayer();
    else drawList();
    if (!shot_.empty()) {
        if (SDL_Surface* surf = SDL_RenderReadPixels(gfx.renderer, nullptr)) {
            SDL_SaveBMP(surf, shot_.c_str());
            SDL_DestroySurface(surf);
        }
        shot_.clear();
    }
    gfx.endFrame();
}

// ----------------------------------------------------------------- main ---

int Music::run(char** argv, const std::string& path, const std::string& library) {
    uint64_t t0 = SDL_GetTicksNS();
    paths.init(argv[0]);
    logToFile(paths.runtime + "/kennel-music.log");
    platform = detectPlatform();
    settings.load(paths.config + "/kennel.ini");
    loadSettings();
    for (const auto& r : split(library.empty() ? "~/music" : library, ';'))
        if (!trim(r).empty()) roots_.push_back(paths.expand(trim(r)));
    anim::setScale(0);

    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_ROLE, "Music");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        logf("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (!gfx.init(platform.width, platform.height, platform.fullscreen)) return 1;

    if (path.empty()) {
        scanAlbums();
    } else if (isDir(path)) {
        openAlbum(path, baseName(path), {});
    } else {
        std::string dir = dirName(path);
        openAlbum(dir, baseName(dir), baseName(path));
    }

    if (const char* script = getenv("KENNEL_AUTOMATE")) script_ = split(script, ',');
    bool gamepadStarted = false;
    std::vector<KeyEvent> keys;
    while (running_) {
        anim::tick();
        int timeout = ::input.timeoutMs();
        if (inAlbum_ && !status_.paused) timeout = timeout < 0 ? kPlayingPollMs : std::min(timeout, kPlayingPollMs);
        if (!script_.empty()) timeout = timeout < 0 ? 50 : std::min(timeout, 50);
        if (dirty_ || !gamepadStarted) timeout = 0;
        SDL_Event ev;
        bool got = waitEvent(&ev, timeout, kInputPollMs);
        keys.clear();
        while (got) {
            switch (ev.type) {
                case SDL_EVENT_QUIT: running_ = false; break;
                case SDL_EVENT_WINDOW_EXPOSED: dirty_ = true; break;
                default: ::input.handle(ev, keys);
            }
            got = SDL_PollEvent(&ev);
        }
        anim::tick();
        ::input.tick(keys);
        automate(keys);
        for (const auto& k : keys) input(k);
        poll();

        if (dirty_ && running_) {
            dirty_ = false;
            draw();
            if (!gamepadStarted) {
                gamepadStarted = true;
                logf("first frame at %.0f ms (draw %.0f ms)", (SDL_GetTicksNS() - t0) / 1e6, gfx.frameMs);
                if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) logf("gamepad init: %s", SDL_GetError());
                startAudio();
                // KMSDRM does not scan out the first presented frame; an idle screen needs a second one.
                dirty_ = true;
            }
        }
    }

    player_.close();
    if (cover_) SDL_DestroyTexture(cover_);
    gfx.shutdown();
    SDL_Quit();
    return 0;
}
