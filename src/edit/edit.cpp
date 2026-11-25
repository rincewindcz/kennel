#include "edit.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <unistd.h>

#include "anim.h"
#include "ini.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "util.h"

using edit::Pos;
using A = Keyboard::Action;

namespace {

constexpr float kTopH = 24, kBarH = 24, kFieldBarH = 52, kPanelW = 300, kPadX = 6;
constexpr int kTab = 4;
constexpr int kMinFont = 10, kMaxFont = 20;
constexpr int kInputPollMs = 10;  // waitEvent() interval
const Color kBg = rgb(0x0b0d12), kFg = rgb(0xe6e9ef), kBarBg = rgb(0x12151c);

uint32_t decode(const std::string& s, int i, int& len) {
    auto c = (unsigned char)s[i];
    len = edit::utf8Len(c);
    if (len == 1 || i + len > (int)s.size()) {
        len = 1;
        return c < 0x80 ? c : 0xFFFD;
    }
    uint32_t cp = c & (0xff >> (len + 1));
    for (int k = 1; k < len; ++k) cp = cp << 6 | ((unsigned char)s[i + k] & 0x3f);
    return cp;
}

size_t lastCharStart(const std::string& s) {
    size_t i = s.size();
    if (i == 0) return 0;
    --i;
    while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80) --i;
    return i;
}

std::string absolute(const std::string& path) {
    if (path.empty() || path[0] == '/') return path;
    char cwd[PATH_MAX];
    return getcwd(cwd, sizeof cwd) ? std::string(cwd) + "/" + path : path;
}

void hangup(int) { raise(SIGTERM); }  // SDL turns SIGTERM into a quit event

const char* kHelp[] = {
    "TYPE (keyboard shown)",
    "  D-pad       move on the keyboard",
    "  A           press the key          B   backspace",
    "  X           space                  START  new line",
    "  L2          shift, twice for caps  R2  symbols",
    "  L1 / R1     cursor left / right    SELECT  navigate",
    "  Keyboard row: Esc navigate, Tab, Ctrl, arrows, gear menu",
    "  Ctrl+S save  Ctrl+F find  Ctrl+H replace  Ctrl+W word wrap",
    "",
    "NAVIGATE",
    "  D-pad       cursor                 L1 / R1  page up / down",
    "  L2 / R2     start / end of file    X   find next",
    "  Y           word wrap              A, SELECT  type",
    "  START       menu                   B   close",
    "",
    "FIND: START next, L1/R1 previous/next, Y match case.",
    "REPLACE: SELECT switches field, START reviews the matches:",
    "  A replace, R1 skip, Y replace all, B back to the fields.",
};

}  // namespace

// --------------------------------------------------------------- setup ---

void Edit::loadSettings() {
    settingsPath_ = paths.config + "/edit.ini";
    IniFile ini;
    if (!loadIni(settingsPath_, ini)) return;
    wrap_ = ini.get("edit", "wrap", "1") != "0";
    fontSize_ = std::clamp(atoi(ini.get("edit", "font_size", std::to_string(fontSize_)).c_str()), kMinFont, kMaxFont);
}

void Edit::saveSettings() {
    writeFileAtomic(settingsPath_, strprintf("[edit]\nwrap=%d\nfont_size=%d\n", wrap_ ? 1 : 0, fontSize_));
}

bool Edit::openFont() {
    if (!glyphs_.open("JetBrainsMono", fontSize_)) return false;
    cellW_ = std::max(1, glyphs_.glyph(Glyphs::Regular, 'M').advance);
    cellH_ = std::max(1, glyphs_.lineSkip());
    return true;
}

bool Edit::open(const std::string& path) {
    path_ = absolute(path);
    if (path_.empty()) return true;
    std::string error;
    switch (buf_.load(path_, error)) {
        case edit::Buffer::Load::Ok:
            if (buf_.readOnly) notice(buf_.readOnlyReason, theme::warn);
            return true;
        case edit::Buffer::Load::Missing: notice("New file", theme::dim); return true;
        default:
            message_ = "Cannot open " + baseName(path_) + ": " + error;
            return false;
    }
}

void Edit::layout() {
    Mode m = overlay(mode_) ? under_ : mode_;
    float bottom = gfx.H;
    switch (m) {
        case Mode::Type: bottom -= keyboard_.height(); break;
        case Mode::Find:
        case Mode::SaveAs: bottom -= keyboard_.height() + kFieldBarH; break;
        case Mode::Review: bottom -= kBarH + kFieldBarH; break;
        default: bottom -= kBarH; break;
    }
    cols_ = std::max(10, (int)((gfx.W - 2 * kPadX) / cellW_));
    rows_ = std::max(1, (int)((bottom - kTopH - 4) / cellH_));
    text_ = {kPadX, kTopH + 2, (float)cols_ * cellW_, (float)rows_ * cellH_};
    ensureVisible(false);
    dirty_ = true;
}

void Edit::setMode(Mode m) {
    if (!overlay(m)) under_ = m;
    mode_ = m;
    layout();
}

void Edit::notice(const std::string& text, Color c) {
    notice_ = text;
    noticeColor_ = c;
    noticeUntil_ = anim::now() + 3;
    dirty_ = true;
}

void Edit::dialog(Dialog d) {
    dialog_ = std::move(d);
    setMode(Mode::Dialog);
}

// -------------------------------------------------------------- layout ---

int Edit::colOf(int line, int byte) const {
    const std::string& l = buf_.lines[line];
    int col = 0;
    for (int i = 0; i < byte && i < (int)l.size(); i += edit::utf8Len((unsigned char)l[i]))
        col = l[i] == '\t' ? (col / kTab + 1) * kTab : col + 1;
    return col;
}

// Visual rows of a line: with word wrap, a row breaks after its last space, or at the column limit
// when it has none.
std::vector<Edit::Row> Edit::rowsOf(int line) const {
    const std::string& l = buf_.lines[line];
    std::vector<Row> rows;
    if (!wrap_) {
        rows.push_back({0, (int)l.size(), 0});
        return rows;
    }
    int start = 0, startCol = 0, col = 0, brk = -1, brkCol = 0;
    for (int i = 0; i < (int)l.size();) {
        int len = edit::utf8Len((unsigned char)l[i]);
        int next = l[i] == '\t' ? (col / kTab + 1) * kTab : col + 1;
        if (next - startCol > cols_ && i > start) {
            bool atSpace = brk > start;
            rows.push_back({start, atSpace ? brk : i, startCol});
            startCol = atSpace ? brkCol : col;
            start = atSpace ? brk : i;
            brk = -1;
            continue;
        }
        col = next;
        i += len;
        if (l[i - len] == ' ') {
            brk = i;
            brkCol = col;
        }
    }
    rows.push_back({start, (int)l.size(), startCol});
    return rows;
}

int Edit::rowIndex(const std::vector<Row>& rows, int byte) const {
    int r = 0;
    while (r + 1 < (int)rows.size() && rows[r + 1].start <= byte) ++r;
    return r;
}

// Byte offset in row r closest to display column col (relative to the row). A row that continues on
// the next one cannot hold the cursor at its end: that position is the next row's start.
int Edit::byteAt(int line, const std::vector<Row>& rows, int r, int col) const {
    const std::string& l = buf_.lines[line];
    const Row& row = rows[r];
    int c = row.startCol, i = row.start, last = row.start;
    while (i < row.end) {
        int next = l[i] == '\t' ? (c / kTab + 1) * kTab : c + 1;
        if (next - row.startCol > col) break;
        last = i;
        c = next;
        i += edit::utf8Len((unsigned char)l[i]);
    }
    if (i >= row.end && r + 1 < (int)rows.size()) return last;
    return i;
}

bool Edit::prevRow(int& line, int& sub) const {
    if (sub > 0) {
        --sub;
        return true;
    }
    if (line == 0) return false;
    --line;
    sub = (int)rowsOf(line).size() - 1;
    return true;
}

bool Edit::nextRow(int& line, int& sub) const {
    if (sub + 1 < (int)rowsOf(line).size()) {
        ++sub;
        return true;
    }
    if (line + 1 >= (int)buf_.lines.size()) return false;
    ++line;
    sub = 0;
    return true;
}

void Edit::ensureVisible(bool center) {
    int nl = (int)buf_.lines.size();
    topLine_ = std::clamp(topLine_, 0, nl - 1);
    topRow_ = std::clamp(topRow_, 0, (int)rowsOf(topLine_).size() - 1);
    cur_ = buf_.clamp(cur_);
    auto rows = rowsOf(cur_.line);
    int sub = rowIndex(rows, cur_.col);

    if (!wrap_) {
        int col = colOf(cur_.line, cur_.col), margin = std::min(4, cols_ / 4);
        if (col < hscroll_ + margin) hscroll_ = std::max(0, col - margin);
        if (col > hscroll_ + cols_ - 1 - margin) hscroll_ = col - cols_ + 1 + margin;
    } else {
        hscroll_ = 0;
    }

    bool above = cur_.line < topLine_ || (cur_.line == topLine_ && sub < topRow_);
    int dist = 0;  // visual rows from the top row to the cursor's
    if (!above) {
        int l = topLine_, s = topRow_;
        while ((l != cur_.line || s != sub) && dist < rows_) {
            if (!nextRow(l, s)) break;
            ++dist;
        }
    }
    if (!above && dist < rows_ && !center) return;
    // Put the cursor's row at the top, the middle or the bottom.
    int back = center ? rows_ / 2 : above ? 0 : rows_ - 1;
    int l = cur_.line, s = sub;
    while (back-- > 0 && prevRow(l, s)) continue;
    topLine_ = l;
    topRow_ = s;
}

void Edit::moveTo(Pos p, bool keepGoal) {
    cur_ = buf_.clamp(p);
    if (!keepGoal) goalCol_ = -1;
    ensureVisible(false);
    dirty_ = true;
}

void Edit::moveRows(int n) {
    auto rows = rowsOf(cur_.line);
    int line = cur_.line, sub = rowIndex(rows, cur_.col);
    if (goalCol_ < 0) goalCol_ = colOf(cur_.line, cur_.col) - rows[sub].startCol;
    for (int i = 0; i < std::abs(n); ++i) {
        if (!(n < 0 ? prevRow(line, sub) : nextRow(line, sub))) {
            if (i == 0) {
                moveTo(n < 0 ? Pos{line, 0} : Pos{line, (int)buf_.lines[line].size()});
                return;
            }
            break;
        }
    }
    rows = rowsOf(line);
    moveTo({line, byteAt(line, rows, sub, goalCol_)}, true);
}

void Edit::page(int dir) {
    int n = std::max(1, rows_ - 1);
    for (int i = 0; i < n; ++i)
        if (!(dir < 0 ? prevRow(topLine_, topRow_) : nextRow(topLine_, topRow_))) break;
    moveRows(dir * n);
}

// -------------------------------------------------------------- editing ---

bool Edit::editable() {
    if (!buf_.readOnly) return true;
    notice(buf_.readOnlyReason, theme::warn);
    return false;
}

void Edit::edited(Pos p) {
    modified_ = true;
    hasMatch_ = false;
    moveTo(p);
}

void Edit::typeText(const std::string& text) {
    if (editable()) edited(buf_.insert(cur_, text));
}

void Edit::action(A a) {
    switch (a) {
        case A::Backspace:
            if (editable()) edited(buf_.backspace(cur_));
            break;
        case A::Enter: typeText("\n"); break;
        case A::Tab: typeText("\t"); break;
        case A::Esc: setMode(mode_ == Mode::Type ? Mode::Navigate : Mode::Type); break;
        case A::Left: moveTo(buf_.left(cur_)); break;
        case A::Right: moveTo(buf_.right(cur_)); break;
        case A::Up: moveRows(-1); break;
        case A::Down: moveRows(1); break;
        case A::Home: moveTo({cur_.line, 0}); break;
        case A::End: moveTo({cur_.line, (int)buf_.lines[cur_.line].size()}); break;
        case A::PageUp: page(-1); break;
        case A::PageDown: page(1); break;
        case A::Menu: openMenu(); break;
        default: break;
    }
}

void Edit::ctrlKey(char c) {
    switch (c | 0x20) {
        case 's': save(); break;
        case 'f':
            if (mode_ == Mode::Find && !replacing_) findStep(true);
            else openFind(false);
            break;
        case 'h': openFind(true); break;
        case 'w': toggleWrap(); break;
        default: break;
    }
}

void Edit::toggleWrap() {
    wrap_ = !wrap_;
    saveSettings();
    topRow_ = 0;
    ensureVisible(true);
    notice(wrap_ ? "Word wrap on" : "Word wrap off", theme::dim);
}

// ---------------------------------------------------------------- find ---

void Edit::openFind(bool replace) {
    if (replace && !editable()) return;
    if (mode_ != Mode::Find && mode_ != Mode::Review) back_ = under_ == Mode::Navigate ? Mode::Navigate : Mode::Type;
    replacing_ = replace;
    field_ = 0;
    setMode(Mode::Find);
}

void Edit::closeFind() {
    hasMatch_ = false;
    setMode(back_);
}

bool Edit::findStep(bool forward) {
    if (find_.empty()) {
        if (mode_ != Mode::Find) openFind(false);
        return false;
    }
    Pos from = forward ? (hasMatch_ ? Pos{match_.line, match_.col + (int)find_.size()} : cur_) : (hasMatch_ ? match_ : cur_);
    Pos at;
    if (!buf_.find(find_, from, forward, matchCase_, at)) {
        hasMatch_ = false;
        notice("Not found: " + find_, theme::warn);
        return false;
    }
    hasMatch_ = true;
    match_ = at;
    cur_ = {at.line, at.col + (int)find_.size()};
    goalCol_ = -1;
    ensureVisible(true);
    dirty_ = true;
    return true;
}

void Edit::replaceCurrent() {
    if (!editable()) return;
    if (hasMatch_ && buf_.replaceAt(match_, find_, replace_, matchCase_)) {
        modified_ = true;
        cur_ = {match_.line, match_.col + (int)replace_.size()};
        hasMatch_ = false;
    }
    if (!findStep(true)) setMode(Mode::Find);
}

void Edit::replaceAll() {
    if (!editable() || find_.empty()) return;
    int n = buf_.replaceAll(find_, replace_, matchCase_);
    if (n > 0) modified_ = true;
    hasMatch_ = false;
    moveTo(cur_);
    notice(n == 1 ? "Replaced 1 match" : strprintf("Replaced %d matches", n), n ? theme::good : theme::warn);
    setMode(Mode::Find);
}

// Text and keys for the find, replace and Save as fields.
void Edit::fieldAction(A a, const std::string& text) {
    bool saving = mode_ == Mode::SaveAs;
    std::string& f = saving ? saveName_ : field_ ? replace_ : find_;
    switch (a) {
        case A::Text:
            f += text;
            if (!saving) hasMatch_ = false;
            break;
        case A::Backspace:
            if (!f.empty()) {
                f.erase(lastCharStart(f));
                if (!saving) hasMatch_ = false;
            } else if (!saving && field_ == 1) {
                field_ = 0;
            } else if (saving) {
                closeAfterSave_ = false;
                setMode(back_);
            } else {
                closeFind();
            }
            break;
        case A::Enter:
            if (saving) saveAs();
            else if (replacing_ && field_ == 0) field_ = 1;
            else if (findStep(true) && replacing_) setMode(Mode::Review);
            break;
        case A::Esc:
            if (saving) {
                closeAfterSave_ = false;
                setMode(back_);
            } else {
                closeFind();
            }
            break;
        case A::Menu: openMenu(); break;
        default: break;
    }
    dirty_ = true;
}

// ---------------------------------------------------------------- files ---

void Edit::save() {
    if (path_.empty()) {
        openSaveAs();
        return;
    }
    if (editable() && writeTo(path_) && closeAfterSave_) running_ = false;
}

void Edit::openSaveAs() {
    if (mode_ != Mode::SaveAs) back_ = under_ == Mode::Navigate ? Mode::Navigate : Mode::Type;
    saveName_ = path_.empty() ? "" : baseName(path_);
    setMode(Mode::SaveAs);
}

void Edit::saveAs() {
    std::string name = trim(saveName_);
    if (name.empty()) {
        notice("Enter a file name", theme::warn);
        return;
    }
    std::string target = name[0] == '/'                   ? name
                         : name.rfind("~/", 0) == 0        ? paths.home + name.substr(1)
                         : (path_.empty() ? paths.home : dirName(path_)) + "/" + name;
    if (isDir(target)) {
        notice(name + " is a folder", theme::warn);
        return;
    }
    auto write = [this, target] {
        if (!writeTo(target)) return;
        setMode(back_);
        if (closeAfterSave_) running_ = false;
    };
    if (target != path_ && fileExists(target)) {
        dialog({"Replace " + baseName(target) + "?", "A file with this name already exists.", {"Cancel", "Replace"}, 0, 0,
                [this, write](int i) {
                    setMode(Mode::SaveAs);
                    if (i == 1) write();
                }});
        return;
    }
    write();
}

bool Edit::writeTo(const std::string& target) {
    std::string error;
    if (!buf_.save(target, error)) {
        notice("Not saved: " + error, theme::bad);
        return false;
    }
    unlink(recoveryPath(path_).c_str());
    path_ = absolute(target);
    unlink(recoveryPath(path_).c_str());
    buf_.readOnly = false;
    modified_ = false;
    notice("Saved " + baseName(path_), theme::good);
    return true;
}

void Edit::requestClose() {
    if (!modified_) {
        running_ = false;
        return;
    }
    std::string name = path_.empty() ? "the new file" : baseName(path_);
    dialog({"Save changes?", "Changes to " + name + " are not saved.", {"Save", "Discard", "Cancel"}, 2, 2, [this](int i) {
                setMode(under_);
                if (i == 0) {
                    closeAfterSave_ = true;
                    save();
                } else if (i == 1) {
                    unlink(recoveryPath(path_).c_str());
                    running_ = false;
                }
            }});
}

std::string Edit::recoveryPath(const std::string& path) const {
    return paths.data + strprintf("/edit/recovery/%016llx", (unsigned long long)fnv1a(path));
}

void Edit::writeRecovery() {
    std::string file = recoveryPath(path_);
    makeDirs(dirName(file));
    if (writeFileAtomic(file, path_ + "\n" + buf_.text())) logf("unsaved changes kept in %s", file.c_str());
    else logf("cannot write %s", file.c_str());
}

void Edit::checkRecovery() {
    auto data = readFile(recoveryPath(path_));
    if (!data) return;
    size_t nl = data->find('\n');
    if (nl == std::string::npos || data->substr(0, nl) != path_) return;
    std::string text = data->substr(nl + 1);
    dialog({"Restore unsaved changes?", "kennel-edit was closed with unsaved changes to " +
                                            (path_.empty() ? std::string("a new file") : baseName(path_)) + ".",
            {"Restore", "Discard"}, 0, 1, [this, text](int i) {
                setMode(Mode::Type);
                if (i == 0) {
                    buf_.setText(text);
                    modified_ = true;
                    moveTo({0, 0});
                } else {
                    unlink(recoveryPath(path_).c_str());
                }
            }});
}

// ---------------------------------------------------------------- input ---

void Edit::input(const KeyEvent& ev) {
    dirty_ = true;
    switch (mode_) {
        case Mode::Type: inputType(ev); break;
        case Mode::Navigate: inputNavigate(ev); break;
        case Mode::Find:
        case Mode::SaveAs: inputField(ev); break;
        case Mode::Review: inputReview(ev); break;
        case Mode::Menu: inputMenu(ev); break;
        case Mode::Help:
            if (ev.key == Key::Back || ev.key == Key::Confirm || ev.key == Key::Start) setMode(under_);
            break;
        case Mode::Dialog: inputDialog(ev); break;
        case Mode::Message:
            if (!ev.repeat) running_ = false;
            break;
    }
}

void Edit::inputType(const KeyEvent& ev) {
    switch (ev.key) {
        case Key::Select: setMode(Mode::Navigate); return;
        case Key::L1: moveTo(buf_.left(cur_)); return;
        case Key::R1: moveTo(buf_.right(cur_)); return;
        default: break;
    }
    Keyboard::Result r = keyboard_.input(ev);
    if (!r.handled) return;
    if (r.action == A::Text) {
        if (r.ctrl && r.text.size() == 1) ctrlKey(r.text[0]);
        else typeText(r.text);
    } else {
        action(r.action);
    }
}

void Edit::inputNavigate(const KeyEvent& ev) {
    switch (ev.key) {
        case Key::Up: moveRows(-1); break;
        case Key::Down: moveRows(1); break;
        case Key::Left: moveTo(buf_.left(cur_)); break;
        case Key::Right: moveTo(buf_.right(cur_)); break;
        case Key::L1: page(-1); break;
        case Key::R1: page(1); break;
        case Key::L2: moveTo({0, 0}); break;
        case Key::R2: moveTo(buf_.end()); break;
        case Key::Confirm:
        case Key::Select:
            if (!ev.repeat) setMode(Mode::Type);
            break;
        case Key::X: findStep(true); break;
        case Key::Y:
            if (!ev.repeat) toggleWrap();
            break;
        case Key::Start:
            if (!ev.repeat) openMenu();
            break;
        case Key::Back:
            if (!ev.repeat) requestClose();
            break;
        default: break;
    }
}

void Edit::inputField(const KeyEvent& ev) {
    bool find = mode_ == Mode::Find;
    switch (ev.key) {
        case Key::Select:
            if (find && replacing_ && !ev.repeat) field_ = 1 - field_;
            return;
        case Key::L1:
            if (find) findStep(false);
            return;
        case Key::R1:
            if (find) findStep(true);
            return;
        case Key::Y:
            if (find && !ev.repeat) {
                matchCase_ = !matchCase_;
                hasMatch_ = false;
            }
            return;
        default: break;
    }
    Keyboard::Result r = keyboard_.input(ev);
    if (!r.handled) return;
    if (r.action == A::Text && r.ctrl && r.text.size() == 1) ctrlKey(r.text[0]);
    else fieldAction(r.action, r.text);
}

void Edit::inputReview(const KeyEvent& ev) {
    if (ev.repeat && ev.key != Key::L1 && ev.key != Key::R1) return;
    switch (ev.key) {
        case Key::Confirm: replaceCurrent(); break;
        case Key::R1: findStep(true); break;
        case Key::L1: findStep(false); break;
        case Key::Y: replaceAll(); break;
        case Key::Back: setMode(Mode::Find); break;
        case Key::Start: closeFind(); break;
        default: break;
    }
}

void Edit::inputMenu(const KeyEvent& ev) {
    if (ev.key == Key::Back || ev.key == Key::Start) {
        setMode(under_);
        return;
    }
    if (ev.key == Key::Confirm) {
        const MenuItem* m = menu_.current();
        if (m && m->type == MenuItem::Type::Action) setMode(under_);
    }
    menu_.input(ev);
}

void Edit::inputDialog(const KeyEvent& ev) {
    int n = (int)dialog_.buttons.size();
    switch (ev.key) {
        case Key::Left: dialog_.selected = std::max(0, dialog_.selected - 1); break;
        case Key::Right: dialog_.selected = std::min(n - 1, dialog_.selected + 1); break;
        case Key::Confirm:
        case Key::Back:
            if (!ev.repeat) {
                Dialog d = std::move(dialog_);
                d.done(ev.key == Key::Confirm ? d.selected : d.cancel);
            }
            break;
        default: break;
    }
}

// A USB keyboard types directly.
void Edit::keyboard(const SDL_KeyboardEvent& k) {
    bool fields = mode_ == Mode::Find || mode_ == Mode::SaveAs;
    if (!fields && mode_ != Mode::Type && mode_ != Mode::Navigate) return;
    if ((k.mod & SDL_KMOD_CTRL) && k.key >= SDLK_A && k.key <= SDLK_Z) {
        ctrlKey((char)('a' + (k.key - SDLK_A)));
        dirty_ = true;
        return;
    }
    A a = A::None;
    switch (k.key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: a = A::Enter; break;
        case SDLK_BACKSPACE: a = A::Backspace; break;
        case SDLK_TAB: a = A::Tab; break;
        case SDLK_ESCAPE: a = A::Esc; break;
        case SDLK_UP: a = A::Up; break;
        case SDLK_DOWN: a = A::Down; break;
        case SDLK_LEFT: a = A::Left; break;
        case SDLK_RIGHT: a = A::Right; break;
        case SDLK_HOME: a = A::Home; break;
        case SDLK_END: a = A::End; break;
        case SDLK_PAGEUP: a = A::PageUp; break;
        case SDLK_PAGEDOWN: a = A::PageDown; break;
        case SDLK_DELETE:
            if (!fields && editable()) edited(buf_.erase(cur_));
            dirty_ = true;
            return;
        default: return;
    }
    if (fields) fieldAction(a, {});
    else if (a == A::Esc && mode_ == Mode::Navigate) return;
    else action(a);
    dirty_ = true;
}

void Edit::text(const char* s) {
    if (mode_ == Mode::Find || mode_ == Mode::SaveAs) fieldAction(A::Text, s);
    else if (mode_ == Mode::Type || mode_ == Mode::Navigate) typeText(s);
    dirty_ = true;
}

void Edit::openMenu() {
    menu_ = Menu{};
    menu_.compact = true;
    menu_.rowHeight = 36;
    auto& m = menu_.items;
    m.push_back(items::action("save", "Save", [this] { save(); }));
    m.push_back(items::action("file-pen", "Save as", [this] { openSaveAs(); }));
    m.push_back(items::action("search", "Find", [this] { openFind(false); }));
    m.push_back(items::action("replace", "Replace", [this] { openFind(true); }));
    m.push_back(items::toggle("wrap-text", "Word wrap", [this] { return wrap_; }, [this](bool) { toggleWrap(); }));
    std::vector<std::string> sizes;
    for (int s = kMinFont; s <= kMaxFont; ++s) sizes.push_back(std::to_string(s));
    m.push_back(items::choice("type", "Font size", sizes, [this] { return fontSize_ - kMinFont; }, [this](int v) {
        int old = fontSize_;
        fontSize_ = kMinFont + v;
        if (!openFont()) {
            fontSize_ = old;
            openFont();
        }
        saveSettings();
        layout();
    }));
    m.push_back(items::action("circle-help", "Controls", [this] { setMode(Mode::Help); }));
    m.push_back(items::action("x", "Close", [this] { requestClose(); }, true));
    menu_.fixSelection();
    setMode(Mode::Menu);
}

// ----------------------------------------------------------------- draw ---

void Edit::drawStatus() {
    gfx.fill({0, 0, gfx.W, kTopH}, kBarBg);
    float ty = (kTopH - gfx.lineHeight(Font::Small)) / 2;
    std::string right = strprintf("Ln %d, Col %d", cur_.line + 1, colOf(cur_.line, cur_.col) + 1);
    if (buf_.crlf) right += "  CRLF";
    if (buf_.readOnly) right += "  Read-only";
    float rw = gfx.text(Font::Small, right, gfx.W - 10, ty, theme::dim, 0, Align::Right);
    float x = 10;
    if (anim::now() < noticeUntil_) {
        float nw = gfx.measure(Font::SmallBold, notice_);
        gfx.text(Font::SmallBold, notice_, gfx.W - 10 - rw - 16, ty, noticeColor_, 0, Align::Right);
        rw += nw + 16;
    }
    std::string name = path_.empty() ? "Untitled" : baseName(path_);
    x += gfx.text(Font::SmallBold, name, x, ty, theme::text, gfx.W - 30 - rw - x - 20);
    if (modified_) gfx.text(Font::Small, " \xE2\x97\x8F", x, ty, accent());
}

void Edit::drawText() {
    gfx.pushClip({0, text_.y, gfx.W, text_.h});
    const int ascent = glyphs_.ascent();
    const bool highlight = mode_ == Mode::Find || mode_ == Mode::Review || (overlay(mode_) && (under_ == Mode::Find || under_ == Mode::Review));
    const int shift = wrap_ ? 0 : hscroll_;
    int line = topLine_, sub = topRow_;
    for (int r = 0; r < rows_ && line < (int)buf_.lines.size(); ++line, sub = 0) {
        const std::string& l = buf_.lines[line];
        auto rows = rowsOf(line);
        // Match ranges in this line (bytes).
        std::vector<int> matches;
        if (highlight) matches = buf_.matches(line, find_, matchCase_);
        const int matchLen = (int)find_.size();
        for (; sub < (int)rows.size() && r < rows_; ++sub, ++r) {
            const Row& row = rows[sub];
            float y = text_.y + r * cellH_;
            int col = row.startCol;
            for (int i = row.start; i < row.end;) {
                int len;
                uint32_t cp = decode(l, i, len);
                int next = cp == '\t' ? (col / kTab + 1) * kTab : col + 1;
                int dc = col - row.startCol - shift;
                if (dc >= cols_) break;
                if (dc >= 0) {
                    float x = text_.x + dc * cellW_;
                    for (int ms : matches)
                        if (i >= ms && i < ms + matchLen) {
                            bool current = hasMatch_ && match_.line == line && match_.col == ms;
                            gfx.fill({x, y, (float)(next - col) * cellW_, (float)cellH_}, accent(current ? 0.55f : 0.22f));
                        }
                    if (cp != '\t' && cp != ' ') glyphs_.add(glyphs_.glyph(Glyphs::Regular, cp), x, y + ascent, kFg);
                }
                col = next;
                i += len;
            }
            bool cursorHere = line == cur_.line && rowIndex(rows, cur_.col) == sub;
            if (cursorHere && !overlay(mode_) && mode_ != Mode::Message) {
                int dc = colOf(line, cur_.col) - row.startCol - shift;
                gfx.fill({text_.x + dc * cellW_ - 1, y, 2, (float)cellH_}, accent());
            }
        }
    }
    glyphs_.flush(gfx.renderer);
    gfx.popClip();
}

void Edit::drawField(const SDL_FRect& r, const std::string& label, const std::string& value, bool focused) {
    gfx.round(r, 8, focused ? theme::panelHi : theme::panel);
    if (focused) gfx.stroke(r, 8, 1.5f, accent());
    float ty = r.y + (r.h - gfx.lineHeight(Font::Small)) / 2;
    float lw = gfx.text(Font::Tiny, label, r.x + 10, r.y + (r.h - gfx.lineHeight(Font::Tiny)) / 2, theme::faint) + 18;
    float avail = r.w - lw - 16;
    float w = gfx.measure(Font::Small, value);
    gfx.pushClip({r.x + lw, r.y, avail + 4, r.h});
    float x = r.x + lw - std::max(0.f, w - avail);
    gfx.text(Font::Small, value, x, ty, theme::text);
    if (focused) gfx.fill({x + w + 1, r.y + 7, 2, r.h - 14}, accent());
    gfx.popClip();
}

void Edit::drawFieldBar(const SDL_FRect& r) {
    gfx.fill(r, kBarBg);
    SDL_FRect f{r.x + 8, r.y + 4, r.w - 16, 28};
    std::string hint;
    if (mode_ == Mode::SaveAs || under_ == Mode::SaveAs) {
        drawField(f, "SAVE AS", saveName_, true);
        hint = "START save   B on an empty name: cancel   Name or path (~/...)";
    } else {
        float cw = gfx.measure(Font::Tiny, "Aa") + 16;
        SDL_FRect fields{f.x, f.y, f.w - cw - 8, f.h};
        bool review = under_ == Mode::Review;
        if (replacing_) {
            float half = (fields.w - 8) / 2;
            drawField({fields.x, fields.y, half, fields.h}, "FIND", find_, !review && field_ == 0);
            drawField({fields.x + half + 8, fields.y, half, fields.h}, "REPLACE", replace_, !review && field_ == 1);
        } else {
            drawField(fields, "FIND", find_, true);
        }
        SDL_FRect chip{f.x + f.w - cw, f.y + 4, cw, f.h - 8};
        gfx.round(chip, chip.h / 2, matchCase_ ? accent() : theme::panelHi);
        gfx.text(Font::Tiny, "Aa", chip.x + cw / 2, chip.y + (chip.h - gfx.lineHeight(Font::Tiny)) / 2, matchCase_ ? kBg : theme::dim,
                 0, Align::Center);
        if (review) hint = "Review the matches below";
        else if (replacing_) hint = "START next field / review   SELECT switch field   Y match case   L1/R1 previous/next";
        else hint = "START find next   L1/R1 previous/next   Y match case   B on empty: close";
    }
    gfx.text(Font::Tiny, hint, r.x + 12, r.y + 36, theme::faint, r.w - 24);
}

void Edit::drawBar(const std::vector<std::pair<std::string, std::string>>& hints) {
    float y = gfx.H - kBarH;
    gfx.fill({0, y, gfx.W, kBarH}, kBarBg);
    float x = 8, bh = 16;
    float ty = y + (kBarH - gfx.lineHeight(Font::Tiny)) / 2;
    for (const auto& [key, label] : hints) {
        float need = gfx.buttonWidth(key, bh) + 4 + gfx.measure(Font::Tiny, label) + 10;
        if (x + need > gfx.W) break;
        x += gfx.button(key, x, y + (kBarH - bh) / 2, bh, rgb(0xe8ebf0, 0.9f), kBg) + 4;
        x += gfx.text(Font::Tiny, label, x, ty, theme::dim) + 10;
    }
}

void Edit::drawMenu() {
    float x = gfx.W - kPanelW;
    gfx.fill({0, 0, x, gfx.H}, rgb(0, 0.35f));
    gfx.fill({x, 0, kPanelW, gfx.H}, theme::panel);
    gfx.fill({x, 0, 1, gfx.H}, theme::line);
    gfx.text(Font::Bold, "Editor", x + 20, 16, theme::text);
    menu_.draw({x + 6, 50, kPanelW - 12, gfx.H - 50 - 10});
}

void Edit::drawHelp() {
    gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0x0b0d12, 0.96f));
    gfx.text(Font::Bold, "Controls", 24, 14, theme::text);
    float y = 44, lh = gfx.lineHeight(Font::Mono) + 2;
    for (const char* l : kHelp) {
        gfx.text(Font::Mono, l, 24, y, l[0] == ' ' ? theme::text : theme::dim);
        y += lh;
    }
}

void Edit::drawDialog() {
    gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0, 0.5f));
    float w = 420;
    auto lines = gfx.wrap(Font::Small, dialog_.message, w - 48, 4);
    float h = 70 + lines.size() * gfx.lineHeight(Font::Small) + 50;
    SDL_FRect r{(gfx.W - w) / 2, (gfx.H - h) / 2, w, h};
    gfx.shadow(r, 14, 18, rgb(0, 0.6f));
    gfx.round(r, 14, theme::panel);
    gfx.text(Font::Bold, dialog_.title, r.x + 24, r.y + 18, theme::text, w - 48);
    float y = r.y + 52;
    for (const auto& l : lines) {
        gfx.text(Font::Small, l, r.x + 24, y, theme::dim);
        y += gfx.lineHeight(Font::Small);
    }
    int n = (int)dialog_.buttons.size();
    float gap = 8, bw = (w - 48 - gap * (n - 1)) / n, bh = 34, by = r.y + h - bh - 18;
    for (int i = 0; i < n; ++i) {
        SDL_FRect b{r.x + 24 + i * (bw + gap), by, bw, bh};
        bool sel = i == dialog_.selected;
        gfx.round(b, 9, sel ? accent() : theme::panelHi);
        gfx.text(Font::SmallBold, dialog_.buttons[i], b.x + bw / 2, b.y + (bh - gfx.lineHeight(Font::SmallBold)) / 2,
                 sel ? kBg : theme::text, 0, Align::Center);
    }
}

void Edit::drawMessage() {
    gfx.icon("triangle-alert", gfx.W / 2 - 16, gfx.H / 2 - 70, 32, theme::warn);
    gfx.text(Font::Body, message_, gfx.W / 2, gfx.H / 2 - 24, theme::text, gfx.W - 48, Align::Center);
    gfx.text(Font::Small, "Press any button to close", gfx.W / 2, gfx.H / 2 + 12, theme::dim, 0, Align::Center);
}

void Edit::draw() {
    gfx.beginFrame();
    gfx.fill({0, 0, gfx.W, gfx.H}, kBg);
    if (mode_ == Mode::Message) {
        drawMessage();
    } else {
        drawStatus();
        drawText();
        Mode m = overlay(mode_) ? under_ : mode_;
        float kh = keyboard_.height();
        switch (m) {
            case Mode::Type: keyboard_.draw({0, gfx.H - kh, gfx.W, kh}); break;
            case Mode::Find:
            case Mode::SaveAs:
                drawFieldBar({0, gfx.H - kh - kFieldBarH, gfx.W, kFieldBarH});
                keyboard_.draw({0, gfx.H - kh, gfx.W, kh});
                break;
            case Mode::Review:
                drawFieldBar({0, gfx.H - kBarH - kFieldBarH, gfx.W, kFieldBarH});
                drawBar({{"A", "Replace"}, {"R1", "Skip"}, {"L1", "Previous"}, {"Y", "Replace all"}, {"B", "Edit"}, {"START", "Done"}});
                break;
            case Mode::Navigate:
                drawBar({{"SELECT", "Type"}, {"START", "Menu"}, {"B", "Close"}, {"X", "Find next"}, {"Y", "Wrap"},
                         {"L1", "Page up"}, {"R1", "Page down"}, {"L2", "Start"}, {"R2", "End"}});
                break;
            default: break;
        }
        if (mode_ == Mode::Menu) drawMenu();
        if (mode_ == Mode::Help) drawHelp();
        if (mode_ == Mode::Dialog) drawDialog();
    }
    if (!shot_.empty()) {
        if (SDL_Surface* surf = SDL_RenderReadPixels(gfx.renderer, nullptr)) {
            SDL_SaveBMP(surf, shot_.c_str());
            SDL_DestroySurface(surf);
        }
        shot_.clear();
    }
    gfx.endFrame();
}

// Development aid, as in kennel-term: type:TEXT types TEXT (no commas).
void Edit::automate(std::vector<KeyEvent>& keys) {
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
    else if (tok.rfind("type:", 0) == 0) text(tok.substr(5).c_str());
    else if (tok.rfind("ctrl:", 0) == 0 && tok.size() == 6) ctrlKey(tok[5]);
    else if (tok == "quit") running_ = false;
    else
        for (const auto& [n, k] : names)
            if (tok == n) keys.push_back({k, false});
    dirty_ = true;
}

// ----------------------------------------------------------------- main ---

int Edit::run(int argc, char** argv) {
    uint64_t t0 = SDL_GetTicksNS();
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        fprintf(stderr, "usage: kennel-edit [FILE]\n");
        return 2;
    }
    paths.init(argv[0]);
    logToFile(paths.runtime + "/kennel-edit.log");
    platform = detectPlatform();
    settings.load(paths.config + "/kennel.ini");
    loadSettings();
    anim::setScale(0);
    signal(SIGHUP, hangup);
    bool opened = open(argc == 2 ? argv[1] : "");

    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        logf("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (!gfx.init(platform.width, platform.height, platform.fullscreen)) return 1;
    if (!openFont()) {
        logf("no editor font");
        return 1;
    }
    ::input.repeatAll = true;
    if (opened) {
        setMode(Mode::Type);
        checkRecovery();
    } else {
        logf("%s", message_.c_str());
        exitCode_ = 1;
        setMode(Mode::Message);
    }
    SDL_StartTextInput(gfx.window);

    if (const char* script = getenv("KENNEL_AUTOMATE")) script_ = split(script, ',');
    bool gamepadStarted = false;
    std::vector<KeyEvent> keys;
    while (running_) {
        anim::tick();
        int timeout = ::input.timeoutMs();
        if (!script_.empty()) timeout = timeout < 0 ? 50 : std::min(timeout, 50);
        if (noticeUntil_ > anim::now()) {
            int left = (int)std::ceil((noticeUntil_ - anim::now()) * 1000);
            timeout = timeout < 0 ? left : std::min(timeout, left);
        } else if (noticeUntil_ > 0) {
            noticeUntil_ = 0;
            dirty_ = true;
        }
        if (dirty_ || !gamepadStarted) timeout = 0;
        SDL_Event ev;
        bool got = waitEvent(&ev, timeout, kInputPollMs);
        keys.clear();
        while (got) {
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                    // appd stop (SIGTERM) or SIGHUP: unsaved changes go to the recovery folder.
                    if (modified_) writeRecovery();
                    running_ = false;
                    break;
                case SDL_EVENT_WINDOW_EXPOSED: dirty_ = true; break;
                case SDL_EVENT_TEXT_INPUT: text(ev.text.text); break;
                case SDL_EVENT_KEY_DOWN: keyboard(ev.key); break;
                case SDL_EVENT_KEY_UP: break;
                default: ::input.handle(ev, keys);
            }
            got = SDL_PollEvent(&ev);
        }
        anim::tick();
        ::input.tick(keys);
        automate(keys);
        for (const auto& k : keys)
            if (running_) input(k);

        if (dirty_ && running_) {
            dirty_ = false;
            draw();
            if (!gamepadStarted) {
                gamepadStarted = true;
                logf("first frame at %.0f ms (draw %.0f ms)", (SDL_GetTicksNS() - t0) / 1e6, gfx.frameMs);
                if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) logf("gamepad init: %s", SDL_GetError());
                // KMSDRM does not scan out the first presented frame; an idle screen needs a second one.
                dirty_ = true;
            }
        }
    }

    SDL_StopTextInput(gfx.window);
    glyphs_.close();
    gfx.shutdown();
    SDL_Quit();
    return exitCode_;
}
