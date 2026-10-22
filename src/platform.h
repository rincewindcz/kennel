#pragma once

#include <string>
#include <utility>
#include <vector>

// Device integration, read from platforms/<id>.ini (id from /etc/os-release, or KENNEL_PLATFORM).
// Empty commands hide the matching actions.
struct Platform {
    enum class Launch { Exec, Command };

    std::string id = "generic";
    std::string name = "Linux";
    std::string file;

    Launch launch = Launch::Exec;
    std::string launchCommand;  // %c: quoted command, %t: terminal flag or nothing
    std::string terminalFlag = "1";
    std::string stopApp, killApp;
    std::string poweroff, reboot;
    std::string eject, externalMount;
    std::string screenOff;

    std::string backlight;
    std::string mixerCard = "default";
    std::string mixerVolume, mixerOutputSwitch;

    std::vector<std::string> sources;
    std::vector<std::string> hideCategories;
    std::vector<std::string> tailCategories;
    std::string autolaunch;
    std::string appLog;
    std::string systemIcons;
    std::vector<std::string> fallbackFonts;
    std::string hudEnv;

    std::string launcherScript;
    std::string previousLauncher;

    std::string usbMode, usbModeSet;  // usbModeSet: %m is the quoted mode value
    std::vector<std::pair<std::string, std::string>> usbModes;  // label, value

    int width = 640, height = 480;
    bool fullscreen = true;

    std::vector<std::pair<std::string, std::string>> logs;
    std::vector<std::string> logGlobs;
    std::string kernelLog;
    std::vector<std::pair<std::string, std::string>> statusCommands;
    std::vector<std::string> mounts;

    void load(const std::string& path);
};

Platform detectPlatform();
extern Platform platform;
