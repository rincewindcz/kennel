#include "files.h"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <map>
#include <mutex>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "expert.h"
#include "image.h"
#include "panels.h"
#include "procs.h"
#include "paths.h"
#include "settings.h"
#include "util.h"
#include "widgets.h"

namespace {

constexpr float kTop = 92, kBottom = 36, kRowH = 34;
constexpr size_t kViewBytes = 512 * 1024;

struct Clipboard {
    std::string path;
    bool cut = false;
} clipboard;

const char* kBookExts[] = {"epub", "pdb", "mobi", "prc", "fb2", "html", "htm", "xhtml"};
const char* kImageExts[] = {"png", "jpg", "jpeg", "bmp", "gif", "qoi"};
const char* kTextExts[] = {"txt", "log", "ini", "cfg", "conf", "sh", "md", "json", "xml", "lua", "py", "puppy", "csv", "tsv"};
const char* kArchiveExts[] = {"zip", "7z", "rar", "tar", "gz", "xz", "bz2"};

template <size_t N>
bool among(const std::string& ext, const char* (&list)[N]) {
    return std::find_if(std::begin(list), std::end(list), [&](const char* e) { return ext == e; }) != std::end(list);
}

std::string extensionOf(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? "" : lower(name.substr(dot + 1));
}

std::string tilde(const std::string& path) {
    if (path == paths.home) return "~";
    if (path.rfind(paths.home + "/", 0) == 0) return "~" + path.substr(paths.home.size());
    return path;
}

std::string join(const std::string& dir, const std::string& name) { return dir == "/" ? "/" + name : dir + "/" + name; }

std::string readHead(const std::string& path, size_t max, bool* truncated) {
    std::string out;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return out;
    out.resize(max + 1);
    size_t got = 0;
    while (got < out.size()) {
        ssize_t n = read(fd, &out[got], out.size() - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    *truncated = got > max;
    out.resize(std::min(got, max));
    return out;
}

bool looksLikeText(const std::string& path) {
    bool truncated = false;
    std::string head = readHead(path, 8192, &truncated);
    return head.find('\0') == std::string::npos;
}

// "name (2).ext" when name already exists in dir.
std::string freeName(const std::string& dir, const std::string& name) {
    if (!fileExists(join(dir, name))) return name;
    size_t dot = name.rfind('.');
    if (dot == 0 || dot == std::string::npos) dot = name.size();
    for (int i = 2;; ++i) {
        std::string candidate = name.substr(0, dot) + strprintf(" (%d)", i) + name.substr(dot);
        if (!fileExists(join(dir, candidate))) return candidate;
    }
}

// Image viewer over the images of a folder. A worker thread decodes and fits the current image, then
// its neighbours; the main thread uploads the results. Textures are kept for the current image, its
// neighbours and the image still fading out.
class ImageScreen : public Screen {
public:
    ImageScreen(std::vector<std::string> paths, int index, std::function<void(const std::string&)> closed)
        : paths_(std::move(paths)), index_(index), closed_(std::move(closed)) {
        boxW_ = (int)gfx.W;
        boxH_ = (int)(gfx.H - kTop - kBottom - 12);
        worker_ = std::thread([this] { work(); });
        request();
    }
    ~ImageScreen() override {
        {
            std::lock_guard lock(mutex_);
            quit_ = true;
        }
        cv_.notify_one();
        worker_.join();
        for (auto& [path, slot] : slots_)
            if (slot.tex) SDL_DestroyTexture(slot.tex);
    }

    void input(const KeyEvent& ev) override {
        switch (ev.key) {
            case Key::Left:
            case Key::L1: step(-1, ev.repeat); break;
            case Key::Right:
            case Key::R1: step(1, ev.repeat); break;
            case Key::X:
                if (!ev.repeat) showActions();
                break;
            case Key::Back:
            case Key::Confirm: leave(); break;
            default: break;
        }
    }
    void update() override {
        std::deque<Result> done;
        {
            std::lock_guard lock(mutex_);
            done.swap(results_);
        }
        if (done.empty()) return;
        for (auto& r : done) {
            if (std::find(paths_.begin(), paths_.end(), r.path) == paths_.end()) continue;
            Slot& s = slot(r.path);
            s.ready = true;
            s.error = r.error;
            s.srcW = r.srcW;
            s.srcH = r.srcH;
            if (!r.img.empty() && !s.tex) {
                s.tex = gfx.texture(r.img);
                s.w = (float)r.img.w;
                s.h = (float)r.img.h;
            }
        }
        changed();
    }
    std::vector<Hint> hints() const override {
        std::vector<Hint> h;
        if (paths_.size() > 1) {
            h.push_back({Key::L1, "Previous"});
            h.push_back({Key::R1, "Next"});
        }
        h.push_back({Key::X, "Actions"});
        h.push_back({Key::Back, "Back"});
        return h;
    }
    std::string title() const override { return "Image"; }
    void draw() override {
        float t = shown.value;
        drawBackground(t);
        const std::string& path = paths_[index_];
        const Slot* cur = find(path);
        std::string sub = strprintf("%d / %d", index_ + 1, (int)paths_.size());
        if (cur && cur->srcW) sub += strprintf("  \xC2\xB7  %dx%d", cur->srcW, cur->srcH);
        if (cur) sub += "  \xC2\xB7  " + formatBytes(cur->bytes);
        if (cur && !cur->error.empty()) sub += "  \xC2\xB7  " + cur->error;
        drawPageHeader("image", baseName(path), sub, t);

        float s = swap_.update(), avail = gfx.H - kTop - kBottom - 6;
        auto picture = [&](const std::string& p, float alpha, float dx) {
            const Slot* sl = find(p);
            if (!sl || !sl->tex || alpha <= 0) return;
            gfx.image(sl->tex, {(gfx.W - sl->w) / 2 + dx, kTop + (avail - sl->h) / 2, sl->w, sl->h}, alpha);
        };
        float stale = shown_ == path ? 1.f : 0.35f;  // the image before, while the current one decodes
        if (s < 1) picture(from_, t * (1 - s), -swapDir_ * 8 * s);
        picture(shown_, t * s * stale, swapDir_ * 8 * (1 - s));
        if (cur && cur->ready && !cur->tex && shown_ == path)
            gfx.text(Font::Small, cur->error, gfx.W / 2, kTop + avail / 2 - 10, fade(theme::dim, t * s), 0, Align::Center);
        else if (shown_.empty())
            gfx.text(Font::Small, "Loading", gfx.W / 2, kTop + avail / 2 - 10, fade(theme::dim, t), 0, Align::Center);
    }

private:
    struct Slot {
        SDL_Texture* tex = nullptr;
        float w = 0, h = 0;
        int srcW = 0, srcH = 0;
        uint64_t bytes = 0;
        std::string error;
        bool ready = false;
    };
    struct Result {
        std::string path, error;
        Image img;
        int srcW = 0, srcH = 0;
    };

    static Result decode(const std::string& path, int boxW, int boxH) {
        Result r;
        r.path = path;
        int w, h;
        if (!imageInfo(path, w, h)) {
            r.error = "Cannot open the image";
            return r;
        }
        int o = imageOrientation(path);
        r.srcW = o >= 5 ? h : w;
        r.srcH = o >= 5 ? w : h;
        Image src;
        if ((long long)w * h > kMaxImagePixels) r.error = "Image too large";
        else if (!loadImage(path, src)) r.error = "Cannot open the image";
        if (!r.error.empty()) return r;
        r.img = o >= 5 ? contain(src, boxH, boxW, 4.f) : contain(src, boxW, boxH, 4.f);
        orient(r.img, o);
        return r;
    }

    void work() {
        while (true) {
            std::string path;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return quit_ || !queue_.empty(); });
                if (quit_) return;
                path = std::move(queue_.front());
                queue_.pop_front();
                busy_ = path;
            }
            Result r = decode(path, boxW_, boxH_);
            {
                std::lock_guard lock(mutex_);
                busy_.clear();
                results_.push_back(std::move(r));
            }
            SDL_Event ev{};
            ev.type = SDL_EVENT_USER;
            SDL_PushEvent(&ev);
        }
    }

    Slot& slot(const std::string& path) {
        auto [it, added] = slots_.try_emplace(path);
        if (added) {
            struct stat st;
            if (stat(path.c_str(), &st) == 0) it->second.bytes = (uint64_t)st.st_size;
        }
        return it->second;
    }
    const Slot* find(const std::string& path) const {
        auto it = slots_.find(path);
        return it == slots_.end() ? nullptr : &it->second;
    }
    int wrap(int i) const { return (i + (int)paths_.size()) % (int)paths_.size(); }

    // The current image first; its neighbours once it is in. Only the settled image is waited for: a
    // new index replaces the queue.
    void request() {
        std::vector<std::string> want{paths_[index_]};
        if (slot(paths_[index_]).ready && paths_.size() > 1) {
            want.push_back(paths_[wrap(index_ + 1)]);
            want.push_back(paths_[wrap(index_ - 1)]);
        }
        {
            std::lock_guard lock(mutex_);
            queue_.clear();
            for (const auto& p : want)
                if (!slot(p).ready && p != busy_ && std::find(queue_.begin(), queue_.end(), p) == queue_.end()) queue_.push_back(p);
        }
        cv_.notify_one();
    }

    void changed() {
        const std::string& path = paths_[index_];
        if (slot(path).ready && shown_ != path) {
            from_ = shown_;
            shown_ = path;
            swapDir_ = (float)dir_;
            swap_.set(0);
            swap_.go(1, 0.22f);
        }
        for (auto it = slots_.begin(); it != slots_.end();) {
            const std::string& p = it->first;
            bool keep = p == path || p == shown_ || (p == from_ && swap_.moving());
            if (paths_.size() > 1) keep = keep || p == paths_[wrap(index_ + 1)] || p == paths_[wrap(index_ - 1)];
            if (keep) {
                ++it;
                continue;
            }
            if (it->second.tex) SDL_DestroyTexture(it->second.tex);
            it = slots_.erase(it);
        }
        request();
        app.dirty = true;
    }

    void step(int dir, bool repeat) {
        int n = (int)paths_.size(), i = index_ + dir;
        if (n < 2 || (repeat && (i < 0 || i >= n))) return;
        index_ = wrap(i);
        dir_ = dir;
        changed();
    }

    void showActions() {
        std::string path = paths_[index_];
        const Slot* cur = find(path);
        std::vector<MenuItem> m;
        if (cur && cur->tex) m.push_back(items::action("image", "Set as wallpaper", [path] { app.setWallpaper(path); }));
        m.push_back(items::action("trash-2", "Delete", [this, path] { remove(path); }, true));
        m.push_back(items::action("info", "Info", [this, path] { info(path); }));
        app.push(std::make_unique<MenuPanel>(baseName(path), std::move(m)));
    }

    void remove(const std::string& path) {
        app.confirm("trash-2", "Delete " + baseName(path) + "?", "The file is deleted.", "Delete", [this, path] {
            if (unlink(path.c_str()) != 0) {
                app.toast("Delete failed: " + std::string(strerror(errno)), "triangle-alert", theme::bad);
                return;
            }
            auto it = std::find(paths_.begin(), paths_.end(), path);
            if (it == paths_.end()) return;
            int i = (int)(it - paths_.begin());
            paths_.erase(it);
            if (paths_.empty()) {
                closed_({});
                close();
                return;
            }
            if (i < index_) --index_;
            else if (index_ == (int)paths_.size()) index_ = 0;
            dir_ = 1;
            changed();
        });
    }

    void info(const std::string& path) {
        const Slot* cur = find(path);
        std::string msg = tilde(path) + "\n";
        if (cur && cur->srcW) msg += strprintf("%d x %d pixels\n", cur->srcW, cur->srcH);
        if (cur) msg += formatBytes(cur->bytes) + strprintf(" (%llu bytes)", (unsigned long long)cur->bytes);
        app.dialog("image", baseName(path), msg, {{"OK", nullptr}}, 0);
    }

    void leave() {
        if (closing()) return;
        closed_(baseName(paths_[index_]));
        close();
    }

    std::vector<std::string> paths_;
    int index_ = 0, dir_ = 1;
    std::function<void(const std::string&)> closed_;
    int boxW_ = 0, boxH_ = 0;
    std::map<std::string, Slot> slots_;
    std::string shown_, from_;
    float swapDir_ = 1;
    anim::Tween swap_;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::deque<Result> results_;
    std::string busy_;
    bool quit_ = false;
};

}  // namespace

FileScreen::FileScreen(std::string dir, std::string select) { open(dir.empty() ? paths.home : dir, select); }

FileScreen::~FileScreen() {
    if (job_.joinable()) job_.join();
}

std::string FileScreen::state() const {
    const Item* it = current();
    return "files\t" + dir_ + "\t" + (it && !it->parent ? it->name : "") + "\n";
}

void FileScreen::open(const std::string& dir, const std::string& select) {
    char resolved[PATH_MAX];
    dir_ = realpath(dir.c_str(), resolved) ? resolved : dir;
    selected_ = 0;
    placed_ = false;
    items_.clear();
    reload();
    selected_ = items_.size() > 1 && items_[0].parent ? 1 : 0;
    for (size_t i = 0; i < items_.size(); ++i)
        if (!select.empty() && items_[i].name == select && !items_[i].parent) selected_ = (int)i;
}

void FileScreen::reload() {
    std::string keep = current() ? current()->name : "";
    items_.clear();
    error_.clear();
    if (dir_ != "/") {
        Item up;
        up.name = "..";
        up.dir = up.parent = true;
        items_.push_back(up);
    }
    DIR* d = opendir(dir_.c_str());
    if (!d) {
        error_ = strerror(errno);
    } else {
        while (dirent* de = readdir(d)) {
            std::string name = de->d_name;
            if (name == "." || name == "..") continue;
            if (!hidden_ && name[0] == '.') continue;
            Item it;
            it.name = name;
            std::string full = join(dir_, name);
            struct stat st, lst;
            if (lstat(full.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode)) it.link = true;
            if (stat(full.c_str(), &st) == 0) {
                it.dir = S_ISDIR(st.st_mode);
                it.size = (uint64_t)st.st_size;
                it.exec = !it.dir && (st.st_mode & 0111);
            }
            items_.push_back(std::move(it));
        }
        closedir(d);
    }
    std::sort(items_.begin() + (dir_ != "/" ? 1 : 0), items_.end(), [](const Item& a, const Item& b) {
        if (a.dir != b.dir) return a.dir;
        return lower(a.name) < lower(b.name);
    });
    selected_ = std::clamp(selected_, 0, std::max(0, (int)items_.size() - 1));
    if (!keep.empty())
        for (size_t i = 0; i < items_.size(); ++i)
            if (items_[i].name == keep) selected_ = (int)i;
    app.dirty = true;
}

const FileScreen::Item* FileScreen::current() const {
    return selected_ >= 0 && selected_ < (int)items_.size() ? &items_[selected_] : nullptr;
}

std::string FileScreen::pathOf(const Item& it) const { return it.parent ? dirName(dir_) : join(dir_, it.name); }

int FileScreen::visibleRows() const { return std::max(1, (int)((gfx.H - kTop - kBottom - 6) / kRowH)); }

// ------------------------------------------------------------- opening ---

void FileScreen::activate() {
    const Item* it = current();
    if (!it) return;
    if (it->parent) {
        open(dirName(dir_), baseName(dir_));
        return;
    }
    std::string path = pathOf(*it);
    if (it->dir) open(path);
    else openFile(path);
}

void FileScreen::openFile(const std::string& path) {
    std::string name = baseName(path), ext = extensionOf(name);
    if (among(ext, kImageExts)) {
        std::vector<std::string> images;
        int index = 0;
        for (const auto& it : items_) {
            if (it.dir || !among(extensionOf(it.name), kImageExts)) continue;
            if (it.name == name) index = (int)images.size();
            images.push_back(join(dir_, it.name));
        }
        if (images.empty()) images.push_back(path);
        app.push(std::make_unique<ImageScreen>(std::move(images), index, [this](const std::string& shown) {
            reload();
            for (size_t i = 0; i < items_.size(); ++i)
                if (!shown.empty() && items_[i].name == shown && !items_[i].parent) selected_ = (int)i;
        }));
        return;
    }
    auto entries = app.catalog.entriesForFile(path);
    if (entries.empty() && among(ext, kBookExts) && fileExists(paths.exeDir + "/kennel-read")) {
        Entry e;
        e.id = "rom:" + path;
        e.category = "Books";
        e.title = name;
        e.command = shellQuote(paths.exeDir + "/kennel-read") + " " + shellQuote(path);
        entries.push_back(std::move(e));
    }
    if (entries.size() == 1 && !among(ext, kTextExts)) {
        app.launch(entries[0]);
        return;
    }
    std::vector<DialogButton> buttons = {{"Cancel", nullptr}};
    for (size_t i = 0; i < entries.size() && i < 2; ++i) {
        Entry e = entries[i];
        buttons.push_back({e.category, [e] { app.launch(e); }});
    }
    bool text = looksLikeText(path);
    if (text)
        buttons.push_back({"View", [path, name] {
                               app.push(std::make_unique<TextScreen>(name, "file-text", [path] {
                                   bool truncated = false;
                                   std::string s = readHead(path, kViewBytes, &truncated);
                                   if (truncated) s += "\n(showing the first 512 KiB)";
                                   return s.empty() ? std::string("(empty)") : s;
                               }));
                           }});
    if (buttons.size() == 2 && text) {
        buttons[1].action();
        return;
    }
    if (text && buttons.size() < 4 && fileExists(paths.exeDir + "/kennel-edit"))
        buttons.insert(buttons.end() - 1, {"Edit", [path] { app.openEditor(path); }});
    struct stat st;
    bool exec = stat(path.c_str(), &st) == 0 && (st.st_mode & 0111);
    if (settings.expert && (exec || ext == "sh") && buttons.size() < 4) {
        std::string dir = dir_;
        std::string cmd = exec ? shellQuote(path) : "sh " + shellQuote(path);
        buttons.push_back({"Run", [dir, cmd] { app.openTerminal(dir, cmd); }});
    }
    if (buttons.size() == 1) {
        app.toast(ext.empty() ? "No app opens this file" : "No app opens ." + ext + " files", "info");
        return;
    }
    app.dialog("file", name, "Open with", std::move(buttons), 1);
}

// ------------------------------------------------------------- actions ---

void FileScreen::showActions() {
    const Item* it = current();
    std::vector<MenuItem> m;
    if (it && !it->parent) {
        std::string path = pathOf(*it);
        m.push_back(items::action(it->dir ? "folder-open" : "play", "Open", [this] { activate(); }));
        if (!it->dir && among(extensionOf(it->name), kImageExts))
            m.push_back(items::action("image", "Set as wallpaper", [path] { app.setWallpaper(path); }));
        if (!it->dir && fileExists(paths.exeDir + "/kennel-edit") && looksLikeText(path))
            m.push_back(items::action("file-pen", "Edit", [path] { app.openEditor(path); }));
        m.push_back(items::action("copy", "Copy", [path] {
            clipboard = {path, false};
            app.toast("Copied " + baseName(path), "copy");
        }));
        m.push_back(items::action("scissors", "Cut", [path] {
            clipboard = {path, true};
            app.toast("Cut " + baseName(path), "scissors");
        }));
    }
    if (!clipboard.path.empty())
        m.push_back(items::action("clipboard-paste", (clipboard.cut ? "Move " : "Paste ") + baseName(clipboard.path) + " here",
                                  [this] { paste(); }));
    if (it && !it->parent) {
        m.push_back(items::action("pencil", "Rename", [this] { rename(); }));
        m.push_back(items::action("trash-2", "Delete", [this] { remove(); }, true));
        m.push_back(items::action("info", "Info", [this] { info(); }));
    }
    m.push_back(items::action("folder-plus", "New folder", [this] { makeFolder(); }));
    if (fileExists(paths.exeDir + "/kennel-edit"))
        m.push_back(items::action("file-plus", "New file", [this] { makeFile(); }));
    m.push_back(items::action(hidden_ ? "eye-off" : "eye", hidden_ ? "Hide hidden files" : "Show hidden files", [this] {
        hidden_ = !hidden_;
        reload();
    }));
    if (settings.expert) {
        std::string dir = dir_;
        m.push_back(items::action("terminal", "Terminal here", [dir] { app.openTerminal(dir); }));
    }
    app.push(std::make_unique<MenuPanel>(it && !it->parent ? it->name : tilde(dir_), std::move(m)));
}

void FileScreen::paste() {
    std::string src = clipboard.path;
    if (!fileExists(src)) {
        app.toast(baseName(src) + " no longer exists", "triangle-alert", theme::warn);
        clipboard = {};
        return;
    }
    if (isDir(src) && (dir_ == src || dir_.rfind(src + "/", 0) == 0)) {
        app.toast("Cannot put a folder into itself", "triangle-alert", theme::warn);
        return;
    }
    std::string name = baseName(src);
    if (clipboard.cut && dirName(src) == dir_) {
        clipboard = {};
        return;
    }
    std::string target = freeName(dir_, name);
    std::string dst = join(dir_, target);
    if (clipboard.cut) {
        clipboard = {};
        if (::rename(src.c_str(), dst.c_str()) == 0) {
            reload();
            open(dir_, target);
            return;
        }
        runJob("Moving " + name, "mv -- " + shellQuote(src) + " " + shellQuote(dst), target);
    } else {
        runJob("Copying " + name, "cp -r -- " + shellQuote(src) + " " + shellQuote(dst), target);
    }
}

void FileScreen::rename() {
    const Item* it = current();
    if (!it || it->parent) return;
    std::string old = it->name, dir = dir_;
    app.push(std::make_unique<TextInputScreen>("Rename " + old, old, [this, old, dir](const std::string& name) {
        if (name.empty() || name == old) return;
        if (name.find('/') != std::string::npos || name == "." || name == "..") {
            app.toast("Not a valid name", "triangle-alert", theme::warn);
            return;
        }
        if (fileExists(join(dir, name))) {
            app.toast(name + " already exists", "triangle-alert", theme::warn);
            return;
        }
        if (::rename(join(dir, old).c_str(), join(dir, name).c_str()) != 0) {
            app.toast("Rename failed: " + std::string(strerror(errno)), "triangle-alert", theme::bad);
            return;
        }
        open(dir_, name);
    }));
}

void FileScreen::makeFolder() {
    std::string dir = dir_;
    app.push(std::make_unique<TextInputScreen>("New folder in " + tilde(dir), "", [this, dir](const std::string& name) {
        if (name.empty()) return;
        if (name.find('/') != std::string::npos || name == "." || name == "..") {
            app.toast("Not a valid name", "triangle-alert", theme::warn);
            return;
        }
        if (mkdir(join(dir, name).c_str(), 0755) != 0) {
            app.toast("Cannot create the folder: " + std::string(strerror(errno)), "triangle-alert", theme::bad);
            return;
        }
        open(dir_, name);
    }));
}

void FileScreen::makeFile() {
    std::string dir = dir_;
    app.push(std::make_unique<TextInputScreen>("New file in " + tilde(dir), "", [dir](const std::string& name) {
        if (name.empty()) return;
        if (name.find('/') != std::string::npos || name == "." || name == "..") {
            app.toast("Not a valid name", "triangle-alert", theme::warn);
            return;
        }
        if (fileExists(join(dir, name))) {
            app.toast(name + " already exists", "triangle-alert", theme::warn);
            return;
        }
        app.openEditor(join(dir, name));
    }));
}

void FileScreen::remove() {
    const Item* it = current();
    if (!it || it->parent) return;
    std::string path = pathOf(*it), name = it->name;
    bool dir = it->dir && !it->link;
    app.confirm("trash-2", "Delete " + name + "?",
                dir ? "The folder and everything in it are deleted." : "The file is deleted.", "Delete", [this, path, name, dir] {
                    if (!dir && unlink(path.c_str()) == 0) {
                        reload();
                        return;
                    }
                    runJob("Deleting " + name, "rm -rf -- " + shellQuote(path), {});
                });
}

void FileScreen::info() {
    const Item* it = current();
    if (!it || it->parent) return;
    std::string path = pathOf(*it);
    struct stat st{};
    lstat(path.c_str(), &st);
    std::string msg = tilde(path) + "\n";
    if (it->link) {
        char target[PATH_MAX];
        ssize_t n = readlink(path.c_str(), target, sizeof target - 1);
        if (n > 0) msg += "Link to " + std::string(target, (size_t)n) + "\n";
    }
    if (it->dir) {
        std::string du = trim(runCapture("du -sk -- " + shellQuote(path)));
        msg += "Folder, " + formatBytes(strtoull(du.c_str(), nullptr, 10) * 1024) + "\n";
    } else {
        msg += formatBytes(it->size) + strprintf(" (%llu bytes)\n", (unsigned long long)it->size);
    }
    msg += strprintf("Mode %04o, owner %s", (unsigned)(st.st_mode & 07777), userName((int)st.st_uid).c_str());
    app.dialog(it->dir ? "folder" : "file", it->name, msg, {{"OK", nullptr}}, 0);
}

void FileScreen::runJob(const std::string& label, const std::string& command, const std::string& select) {
    if (job_.joinable()) return;
    jobLabel_ = label;
    jobSelect_ = select;
    jobDone_ = false;
    logf("files: %s", command.c_str());
    job_ = std::thread([this, command] {
        jobOutput_ = trim(runCapture(command, &jobStatus_));
        jobDone_ = true;
        SDL_Event ev{};
        ev.type = SDL_EVENT_USER;
        SDL_PushEvent(&ev);
    });
    app.dirty = true;
}

void FileScreen::update() {
    if (!job_.joinable() || !jobDone_) return;
    job_.join();
    if (jobStatus_ != 0) app.toast(jobLabel_ + " failed" + (jobOutput_.empty() ? "" : ": " + jobOutput_), "triangle-alert", theme::bad);
    reload();
    if (!jobSelect_.empty()) open(dir_, jobSelect_);
    jobLabel_.clear();
}

// --------------------------------------------------------------- input ---

void FileScreen::input(const KeyEvent& ev) {
    if (job_.joinable()) return;
    int n = (int)items_.size();
    int page = std::max(1, visibleRows() - 1);
    switch (ev.key) {
        case Key::Up:
            if (n) selected_ = ev.repeat ? std::max(0, selected_ - 1) : (selected_ - 1 + n) % n;
            break;
        case Key::Down:
            if (n) selected_ = ev.repeat ? std::min(n - 1, selected_ + 1) : (selected_ + 1) % n;
            break;
        case Key::L1:
        case Key::Left: selected_ = std::max(0, selected_ - page); break;
        case Key::R1:
        case Key::Right: selected_ = std::min(std::max(0, n - 1), selected_ + page); break;
        case Key::L2: selected_ = 0; break;
        case Key::R2: selected_ = std::max(0, n - 1); break;
        case Key::Confirm:
            if (!ev.repeat) activate();
            break;
        case Key::Back:
            if (dir_ == "/") close();
            else open(dirName(dir_), baseName(dir_));
            break;
        case Key::X: showActions(); break;
        case Key::Y:
            open(paths.home);
            break;
        case Key::Start: close(); break;
        default: break;
    }
}

std::vector<Hint> FileScreen::hints() const {
    if (job_.joinable()) return {};
    const Item* it = current();
    std::vector<Hint> h;
    if (it) h.push_back({Key::Confirm, it->parent ? "Up" : "Open"});
    h.push_back({Key::Back, dir_ == "/" ? "Close" : "Up"});
    h.push_back({Key::X, "Actions"});
    h.push_back({Key::Y, "Home"});
    h.push_back({Key::Start, "Close"});
    return h;
}

// ---------------------------------------------------------------- draw ---

void FileScreen::draw() {
    float t = shown.value, dx = 24 * (1 - t);
    drawBackground(t);
    std::string sub = tilde(dir_);
    if (!error_.empty()) sub += "  \xC2\xB7  " + error_;
    drawPageHeader("folder", "Files", sub, t);

    SDL_FRect area{12 + dx, kTop, gfx.W - 24, gfx.H - kTop - kBottom - 6};
    int n = (int)items_.size();
    int vis = visibleRows();
    float content = n * kRowH;
    float target = std::clamp(selected_ * kRowH + kRowH / 2 - area.h / 2, 0.f, std::max(0.f, content - area.h));
    if (!placed_) {
        scroll_.set(target);
        cursor_.set(selected_ * kRowH);
        placed_ = true;
    }
    scroll_.go(target, 0.15f);
    cursor_.go(selected_ * kRowH, 0.12f);
    float scroll = scroll_.update(), cy = cursor_.update();

    if (n == 0 || (n == 1 && items_[0].parent)) {
        gfx.text(Font::Small, error_.empty() ? "Empty folder" : "Cannot open the folder", gfx.W / 2, area.y + 70,
                 fade(theme::dim, t), 0, Align::Center);
    }
    gfx.pushClip(area);
    if (n) {
        SDL_FRect hl{area.x, area.y + cy - scroll + 1, area.w - 10, kRowH - 2};
        gfx.round(hl, 9, accent(0.16f * t));
        gfx.round({hl.x, hl.y + 8, 3, hl.h - 16}, 1.5f, accent(t));
    }
    int first = std::max(0, (int)(scroll / kRowH));
    for (int i = first; i < n && i <= first + vis + 1; ++i) {
        const Item& it = items_[i];
        float y = area.y + i * kRowH - scroll;
        std::string ext = extensionOf(it.name);
        const char* icon = it.parent ? "corner-left-up"
                           : it.dir  ? "folder"
                           : among(ext, kBookExts) ? "book"
                           : among(ext, kImageExts) ? "image"
                           : among(ext, kArchiveExts) ? "file-archive"
                           : among(ext, kTextExts) ? "file-text"
                           : it.exec ? "terminal"
                                     : "file";
        bool sel = i == selected_;
        gfx.icon(icon, area.x + 14, y + (kRowH - 20) / 2, 20, it.dir ? accent(t) : fade(sel ? theme::text : theme::dim, t));
        std::string right = it.dir ? (it.link ? "link" : "") : formatBytes(it.size);
        float rw = right.empty() ? 0 : gfx.text(Font::Small, right, area.x + area.w - 24, y + (kRowH - gfx.lineHeight(Font::Small)) / 2,
                                                fade(theme::dim, t), 0, Align::Right);
        float x = area.x + 44;
        gfx.text(Font::SmallBold, it.name, x, y + (kRowH - gfx.lineHeight(Font::SmallBold)) / 2,
                 fade(it.name[0] == '.' && !it.parent ? theme::dim : theme::text, t), area.x + area.w - 24 - rw - 16 - x);
    }
    gfx.popClip();
    drawScrollbar({area.x + area.w - 6, area.y + 4, 3, area.h - 8}, scroll, content, area.h, t);

    if (job_.joinable()) {
        float w = 340, h = 76;
        SDL_FRect r{(gfx.W - w) / 2, (gfx.H - h) / 2, w, h};
        gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0, 0.5f * t));
        gfx.shadow(r, 14, 18, rgb(0, 0.6f * t));
        gfx.round(r, 14, fade(theme::panel, t));
        gfx.icon("hard-drive", r.x + 20, r.y + 18, 20, accent(t));
        gfx.text(Font::Bold, jobLabel_, r.x + 52, r.y + 14, fade(theme::text, t), w - 72);
        gfx.text(Font::Small, "Please wait", r.x + 52, r.y + 42, fade(theme::dim, t));
    }
}
