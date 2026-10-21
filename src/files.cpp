#include "files.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
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

class ImageScreen : public Screen {
public:
    explicit ImageScreen(std::string path) : path_(std::move(path)) {
        Image img;
        if (loadImage(path_, img)) {
            size_ = strprintf("%dx%d", img.w, img.h);
            Image fit = contain(img, (int)gfx.W, (int)(gfx.H - kTop - kBottom - 12), 4.f);
            tex_ = gfx.texture(fit);
            w_ = (float)fit.w;
            h_ = (float)fit.h;
        }
    }
    ~ImageScreen() override {
        if (tex_) SDL_DestroyTexture(tex_);
    }
    void input(const KeyEvent& ev) override {
        if (ev.key == Key::Back || ev.key == Key::Confirm) close();
    }
    std::vector<Hint> hints() const override { return {{Key::Back, "Back"}}; }
    std::string title() const override { return "Image"; }
    void draw() override {
        float t = shown.value;
        drawBackground(t);
        drawPageHeader("image", baseName(path_), tex_ ? size_ : "Cannot open the image", t);
        if (!tex_) return;
        float top = kTop, avail = gfx.H - kTop - kBottom - 6;
        gfx.image(tex_, {(gfx.W - w_) / 2, top + (avail - h_) / 2, w_, h_}, t);
    }

private:
    std::string path_, size_;
    SDL_Texture* tex_ = nullptr;
    float w_ = 0, h_ = 0;
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
        app.push(std::make_unique<ImageScreen>(path));
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
