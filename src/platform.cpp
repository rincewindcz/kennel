#include "platform.h"

#include <cstdlib>
#include <cstring>

#include "ini.h"
#include "paths.h"
#include "util.h"

Platform platform;

void Platform::load(const std::string& path) {
    IniFile ini;
    if (!loadIni(path, ini)) return;
    file = path;
    const IniSection* p = ini.find("platform");
    if (p) {
        auto str = [&](const char* key, std::string& out) {
            if (const IniEntry* e = p->find(key)) out = e->value;
        };
        auto list = [&](const char* key, std::vector<std::string>& out) {
            if (const IniEntry* e = p->find(key)) out = split(e->value, ';');
        };
        str("id", id);
        str("name", name);
        launch = iequals(p->get("launch", "exec"), "command") ? Launch::Command : Launch::Exec;
        str("launch_command", launchCommand);
        str("terminal_flag", terminalFlag);
        str("stop_app", stopApp);
        str("kill_app", killApp);
        str("poweroff", poweroff);
        str("reboot", reboot);
        str("eject", eject);
        str("external_mount", externalMount);
        str("screen_off", screenOff);
        str("backlight", backlight);
        str("mixer_card", mixerCard);
        str("mixer_volume", mixerVolume);
        str("mixer_output_switch", mixerOutputSwitch);
        list("sources", sources);
        list("hide_categories", hideCategories);
        list("tail_categories", tailCategories);
        str("autolaunch", autolaunch);
        str("app_log", appLog);
        str("system_icons", systemIcons);
        list("fallback_fonts", fallbackFonts);
        str("hud_env", hudEnv);
        str("launcher_script", launcherScript);
        str("previous_launcher", previousLauncher);
        str("kernel_log", kernelLog);
        list("mounts", mounts);
        width = atoi(p->get("width", std::to_string(width)).c_str());
        height = atoi(p->get("height", std::to_string(height)).c_str());
        fullscreen = p->get("fullscreen", fullscreen ? "1" : "0") != "0";
    }
    if (const IniSection* s = ini.find("logs")) {
        logs.clear();
        logGlobs.clear();
        for (const auto& e : s->entries) {
            if (iequals(e.key, "glob")) logGlobs.push_back(e.value);
            else logs.emplace_back(e.key, e.value);
        }
    }
    if (const IniSection* s = ini.find("status"))
        for (const auto& e : s->entries) statusCommands.emplace_back(e.key, e.value);
}

Platform detectPlatform() {
    Platform p;
    p.sources = {"~/.config/kennel/apps.puppy", "~/.config/kennel/apps/*.puppy"};
    p.autolaunch = "~/.config/kennel/autolaunch";
    p.kernelLog = "dmesg";
    p.mounts = {"/", "~"};
    p.fullscreen = false;

    std::string file, id;
    if (const char* env = getenv("KENNEL_PLATFORM")) {
        if (strchr(env, '/')) file = env;
        else id = env;
    } else {
        IniFile osRelease = parseIni(readFile("/etc/os-release").value_or(""));
        id = lower(trim(replaceAll(osRelease.get("", "ID"), "\"", "")));
    }
    if (file.empty() && !id.empty()) {
        for (const std::string& dir : {paths.exeDir + "/platforms", paths.exeDir + "/../share/kennel/platforms"}) {
            if (fileExists(dir + "/" + id + ".ini")) {
                file = dir + "/" + id + ".ini";
                break;
            }
        }
    }
    if (!file.empty()) p.load(file);
    if (p.backlight.empty()) {
        auto bl = glob("/sys/class/backlight/*");
        if (!bl.empty()) p.backlight = bl.front();
    }
    logf("platform %s (%s)", p.id.c_str(), p.file.empty() ? "built-in" : p.file.c_str());
    return p;
}
