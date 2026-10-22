#include <algorithm>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

#include "art.h"
#include "backup.h"
#include "home.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "system.h"
#include "util.h"
#include "widgets.h"

namespace {

void save() {
    anim::setScale(settings.animScale());
    if (!settings.save()) app.toast("Could not save settings", "triangle-alert", theme::bad);
}

std::vector<std::string> categoryOrder() {
    std::vector<std::string> out;
    for (const auto& n : settings.order)
        if (app.catalog.category(n)) out.push_back(n);
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& c : app.catalog.categories) {
            bool tail = std::find(platform.tailCategories.begin(), platform.tailCategories.end(), c.name) !=
                        platform.tailCategories.end();
            if (tail == (pass == 1) && std::find(out.begin(), out.end(), c.name) == out.end()) out.push_back(c.name);
        }
    return out;
}

bool launcherIsKennel() {
    std::string script = readFile(paths.expand(platform.launcherScript)).value_or("");
    return script.find(paths.exe) != std::string::npos;
}

// Index into platform.usbModes of the mode usb_mode prints; 0 when unknown.
int usbModeIndex() {
    std::string cur = trim(runCapture(platform.usbMode));
    for (size_t i = 0; i < platform.usbModes.size(); ++i)
        if (platform.usbModes[i].second == cur) return (int)i;
    return 0;
}

// The switch runs in the background (the USB gadget restarts for a second or two); the row
// shows the new mode at once and ignores input until the switch is done.
MenuItem usbModeItem() {
    struct State {
        int current = 0, switching = -1;
    };
    std::vector<std::string> labels;
    for (const auto& m : platform.usbModes) labels.push_back(m.first);
    auto state = std::make_shared<State>();
    state->current = usbModeIndex();
    return items::choice("usb", "USB mode", labels,
                         [state] { return state->switching >= 0 ? state->switching : state->current; },
                         [state](int i) {
                             if (state->switching >= 0) return;
                             state->switching = i;
                             const auto& [label, value] = platform.usbModes[i];
                             app.runAsync(replaceAll(platform.usbModeSet, "%m", shellQuote(value)),
                                          [state, i, label](int status, const std::string& out) {
                                              state->switching = -1;
                                              if (status == 0) {
                                                  state->current = i;
                                                  app.toast("USB mode: " + label, "usb", theme::good);
                                              } else {
                                                  app.toast("USB mode change failed: " + trim(out), "triangle-alert",
                                                            theme::bad);
                                              }
                                          });
                         });
}

void setLauncher(bool kennel) {
    std::string path = paths.expand(platform.launcherScript);
    std::string body = kennel ? "#!/bin/sh\n# kennel, with the previous launcher as fallback when it fails\n" + shellQuote(paths.exe) +
                                    " || exec " + platform.previousLauncher + "\n"
                              : "#!/bin/sh\nexec " + platform.previousLauncher + "\n";
    if (writeFileAtomic(path, body)) {
        chmod(path.c_str(), 0755);
        app.toast(kennel ? "kennel starts at boot" : platform.previousLauncher + " starts at boot", "check", theme::good);
    } else {
        app.toast("Could not write " + path, "triangle-alert", theme::bad);
    }
}

class CategoryScreen : public MenuScreen {
public:
    CategoryScreen()
        : MenuScreen("Categories", "list", [] {
              std::vector<MenuItem> out;
              for (const auto& name : categoryOrder()) {
                  const Category* c = app.catalog.category(name);
                  std::string label = strprintf("%s (%zu)", name.c_str(), c ? c->entries.size() : (size_t)0);
                  out.push_back(items::toggle(c ? c->glyph : "layers", label,
                                              [name] {
                                                  return std::find(settings.hidden.begin(), settings.hidden.end(), name) ==
                                                         settings.hidden.end();
                                              },
                                              [name](bool on) {
                                                  auto& h = settings.hidden;
                                                  h.erase(std::remove(h.begin(), h.end(), name), h.end());
                                                  if (!on) h.push_back(name);
                                                  save();
                                              }));
              }
              return out;
          }) {
        subtitle = "Shown on Home, in this order";
        extraHints = {{Key::X, "Move up"}, {Key::Y, "Move down"}};
    }

    void input(const KeyEvent& ev) override {
        if (ev.key == Key::X || ev.key == Key::Y) {
            auto order = categoryOrder();
            int i = menu_.selected, j = i + (ev.key == Key::X ? -1 : 1);
            if (i < 0 || j < 0 || j >= (int)order.size()) return;
            std::swap(order[i], order[j]);
            settings.order = order;
            save();
            rebuild();
            menu_.selected = j;
            return;
        }
        MenuScreen::input(ev);
    }
};

// Entries hidden from Home, by category and title; switching one on shows it again.
void openHiddenEntries() {
    std::vector<std::string> ids(app.stats.hidden().begin(), app.stats.hidden().end());
    std::vector<std::pair<std::string, std::string>> rows;  // sort key, id
    for (const auto& id : ids) {
        const Entry* e = app.catalog.find(id);
        rows.push_back({e ? lower(e->category + "\t" + e->title) : "\x7f" + id, id});
    }
    std::sort(rows.begin(), rows.end());
    auto page = std::make_unique<MenuScreen>("Hidden entries", "eye-off", [rows] {
        std::vector<MenuItem> out;
        std::string category;
        for (const auto& [key, id] : rows) {
            const Entry* e = app.catalog.find(id);
            std::string cat = e ? e->category : "Not in the library";
            if (cat != category) {
                out.push_back(items::header(cat));
                category = cat;
            }
            out.push_back(items::toggle("", e ? e->title : id, [id] { return !app.stats.isHidden(id); },
                                        [id](bool shown) { app.stats.setHidden(id, !shown); }));
            out.back().indent = true;
        }
        if (out.empty()) out.push_back(items::info("eye", "Nothing hidden", [] { return std::string(); }));
        return out;
    });
    page->subtitle = "Switched on shows the entry on Home again";
    app.push(std::move(page));
}

void openBackup() {
    auto page = std::make_unique<MenuScreen>("Backup", "hard-drive", [] {
        std::vector<MenuItem> m;
        std::string dir = backupDir();
        m.push_back(items::info("folder", "Location", [dir] { return dir; }));
        m.push_back(items::info("history", "Last backup", [dir] {
            std::string d = describeBackup(dir);
            return d.empty() ? std::string("none") : d;
        }));
        m.push_back(items::action("copy", "Back up now", [dir] {
            std::string error;
            if (makeBackup(dir, error)) {
                app.toast("Backed up to " + dir, "check", theme::good);
                if (auto* page = dynamic_cast<MenuScreen*>(app.top())) page->requestRebuild();
            } else app.toast("Backup failed: " + error, "triangle-alert", theme::bad);
        }));
        if (!describeBackup(dir).empty())
            m.push_back(items::action("undo-2", "Restore", [dir] {
                app.confirm("undo-2", "Restore the backup?",
                            "Settings, statistics, favourites, hidden entries, battery history and reading positions are "
                            "replaced by the backup from " + describeBackup(dir) + ". kennel restarts.",
                            "Restore", [dir] {
                                std::string error;
                                if (restoreBackup(dir, error)) app.restart();
                                else app.toast("Restore failed: " + error, "triangle-alert", theme::bad);
                            });
            }, true));
        return m;
    });
    page->subtitle = "Settings and data, without the art cache";
    app.push(std::move(page));
}

}  // namespace

void App::openSettings() {
    push(std::make_unique<MenuScreen>("Settings", "settings", [] {
        std::vector<MenuItem> m;
        m.push_back(items::header("APPEARANCE"));
        std::vector<std::string> accents;
        for (int i = 0; i < kAccentCount; ++i) accents.push_back(kAccents[i].name);
        accents.push_back("Custom");
        m.push_back(items::choice("palette", "Accent colour", accents, [] { return settings.accent; }, [](int v) {
            bool custom = settings.accent == kAccentCount;
            settings.accent = v;
            save();
            // The colour rows appear or disappear.
            if (custom != (v == kAccentCount))
                if (auto* page = dynamic_cast<MenuScreen*>(app.top())) page->requestRebuild();
        }));
        if (settings.accent == kAccentCount) {
            MenuItem info = items::info("", "Colour", [] { return strprintf("#%06x", settings.customAccent & 0xffffff); });
            info.swatch = [] { return rgb(settings.customAccent); };
            info.indent = true;
            m.push_back(std::move(info));
            const char* channels[] = {"Red", "Green", "Blue"};
            for (int ch = 0; ch < 3; ++ch) {
                int shift = 16 - ch * 8;
                MenuItem s = items::slider("", channels[ch], 0, 255, 5,
                                           [shift] { return (int)((settings.customAccent >> shift) & 0xff); },
                                           [shift](int v) {
                                               uint32_t& c = settings.customAccent;
                                               c = (c & ~(0xffu << shift)) | ((uint32_t)v << shift);
                                               save();
                                           });
                s.format = [](int v) { return std::to_string(v); };
                s.indent = true;
                m.push_back(std::move(s));
            }
        }
        m.push_back(items::choice("layers", "Background", {"Gradient", "Game art", "Wallpaper"}, [] { return (int)settings.background; },
                                  [](int v) {
                                      bool was = settings.background == Settings::Background::Wallpaper;
                                      settings.background = (Settings::Background)v;
                                      save();
                                      if (was != (v == (int)Settings::Background::Wallpaper))
                                          if (auto* page = dynamic_cast<MenuScreen*>(app.top())) page->requestRebuild();
                                  }));
        if (settings.background == Settings::Background::Wallpaper) {
            MenuItem image = items::info("", "Image", [] {
                if (settings.wallpaper.empty()) return std::string("none, choose one in Files");
                return wallpaper() ? baseName(settings.wallpaper) : "cannot open " + baseName(settings.wallpaper);
            });
            image.indent = true;
            m.push_back(std::move(image));
        }
        m.push_back(items::choice("zap", "Animations", {"Normal", "Fast", "Off"}, [] { return (int)settings.animations; },
                                  [](int v) {
                                      settings.animations = (Settings::Animations)v;
                                      save();
                                  }));
        m.push_back(items::link("sliders-horizontal", "Top bar", [] {
            app.push(std::make_unique<MenuScreen>("Top bar", "sliders-horizontal", [] {
                std::vector<MenuItem> t;
                t.push_back(items::choice("clock", "Time", {"Auto", "Clock", "Uptime", "Off"}, [] { return (int)settings.time; },
                                          [](int v) {
                                              settings.time = (Settings::TimeMode)v;
                                              save();
                                          }));
                auto flag = [&](const char* icon, const char* label, bool* value) {
                    t.push_back(items::toggle(icon, label, [value] { return *value; }, [value](bool v) {
                        *value = v;
                        save();
                    }));
                };
                flag("cpu", "CPU load", &settings.barCpu);
                flag("memory-stick", "Free memory", &settings.barMemory);
                flag("volume-2", "Volume", &settings.barVolume);
                flag("battery-full", "Battery", &settings.barBattery);
                flag("card", "External card", &settings.barCard);
                flag("usb", "USB connection", &settings.barUsb);
                return t;
            }));
        }));

        m.push_back(items::header("LIBRARY"));
        m.push_back(items::choice("arrow-up-down", "Sort games by", {"Name", "Last played", "Play time"},
                                  [] { return (int)settings.sort; }, [](int v) {
                                      settings.sort = (Settings::Sort)v;
                                      save();
                                  }));
        m.push_back(items::toggle("history", "Continue row", [] { return settings.showContinue; }, [](bool v) {
            settings.showContinue = v;
            save();
        }));
        m.push_back(items::toggle("star", "Favorites row", [] { return settings.showFavorites; }, [](bool v) {
            settings.showFavorites = v;
            save();
        }));
        m.push_back(items::link("list", "Categories", [] { app.push(std::make_unique<CategoryScreen>()); }));
        MenuItem hiddenLink = items::link("eye-off", "Hidden entries", [] { openHiddenEntries(); });
        hiddenLink.value = [] { return std::to_string(app.stats.hidden().size()); };
        m.push_back(std::move(hiddenLink));
        m.push_back(items::action("refresh-cw", "Rescan library", [] {
            app.reloadCatalog();
            art.clear();
            app.toast(strprintf("%zu entries in %zu categories", app.catalog.entries.size(), app.catalog.categories.size()),
                      "check", theme::good);
        }));

        m.push_back(items::header("CONTROLS"));
        m.push_back(items::choice("gamepad-2", "Confirm button", {"South (A)", "East (B)"},
                                  [] { return settings.swapConfirm ? 1 : 0; }, [](int v) {
                                      settings.swapConfirm = v == 1;
                                      input.releaseAll();
                                      save();
                                  }));

        m.push_back(items::header("SYSTEM"));
        m.push_back(items::choice("battery-warning", "Low battery warning", {"Off", "10 %", "15 %", "20 %", "30 %"},
                                  [] {
                                      int v = settings.lowBattery;
                                      return v == 0 ? 0 : v <= 10 ? 1 : v <= 15 ? 2 : v <= 20 ? 3 : 4;
                                  },
                                  [](int i) {
                                      const int values[] = {0, 10, 15, 20, 30};
                                      settings.lowBattery = values[i];
                                      save();
                                  }));
        m.push_back(items::toggle("triangle-alert", "Report app errors", [] { return settings.reportErrors; }, [](bool v) {
            settings.reportErrors = v;
            save();
        }));
        if (!platform.usbModeSet.empty() && !platform.usbMode.empty() && !platform.usbModes.empty())
            m.push_back(usbModeItem());
        m.push_back(items::link("hard-drive", "Backup", [] { openBackup(); }));
        m.push_back(items::toggle("terminal", "Expert mode", [] { return settings.expert; }, [](bool v) {
            settings.expert = v;
            save();
        }));
        if (!platform.launcherScript.empty() && !platform.previousLauncher.empty())
            m.push_back(items::choice("log-out", "Launcher at boot", {"kennel", baseName(platform.previousLauncher)},
                                      [] { return launcherIsKennel() ? 0 : 1; }, [](int v) { setLauncher(v == 0); }));
        m.push_back(items::link("info", "About", [] {
            app.push(std::make_unique<MenuScreen>("About", "info", [] {
                std::vector<MenuItem> a;
                a.push_back(items::info("gamepad-2", "kennel", [] { return std::string(KENNEL_VERSION); }));
                a.push_back(items::info("cpu", "Platform", [] { return platform.name + " (" + platform.id + ")"; }));
                a.push_back(items::info("monitor-off", "Renderer", [] {
                    return strprintf("%s, %.0fx%.0f, %.0f Hz", gfx.rendererName.c_str(), gfx.W, gfx.H, gfx.refresh);
                }));
                a.push_back(items::info("gamepad-2", "Gamepad", [] { return input.padName(); }));
                a.push_back(items::info("timer", "Start-up", [] {
                    return strprintf("%.0f ms (library %.0f ms)", app.startupMs, app.catalog.loadMs);
                }));
                a.push_back(items::info("library", "Library", [] {
                    return strprintf("%zu entries, %zu sources", app.catalog.entries.size(), app.catalog.sources.size());
                }));
                a.push_back(items::header("FILES"));
                a.push_back(items::info("folder", "Program", [] { return paths.exeDir; }));
                a.push_back(items::info("settings", "Settings", [] { return paths.config; }));
                a.push_back(items::info("chart-column", "Statistics", [] { return paths.data; }));
                a.push_back(items::info("hard-drive", "Cache", [] { return paths.cache; }));
                a.push_back(items::info("file-text", "Platform file", [] { return platform.file.empty() ? "built-in" : platform.file; }));
                a.push_back(items::header("LICENSES"));
                a.push_back(items::info("type", "Fonts", [] { return std::string("Inter, JetBrains Mono (OFL 1.1)"); }));
                a.push_back(items::info("palette", "Icons", [] { return std::string("Lucide (ISC)"); }));
                a.push_back(items::info("code", "Libraries", [] { return std::string("SDL3, SDL3_ttf, stb_image, QOI"); }));
                return a;
            }));
        }));
        return m;
    }));
}
