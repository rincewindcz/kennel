#include "system.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "util.h"

#ifdef KENNEL_ALSA
#include <alsa/asoundlib.h>
#endif

System sys;

#ifdef KENNEL_ALSA
namespace {

snd_mixer_t* mixer = nullptr;
snd_mixer_elem_t* volumeElem = nullptr;
snd_mixer_elem_t* outputElem = nullptr;

snd_mixer_elem_t* findElem(const std::string& name) {
    if (!mixer || name.empty()) return nullptr;
    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, name.c_str());
    return snd_mixer_find_selem(mixer, sid);
}

void openMixer() {
    if (snd_mixer_open(&mixer, 0) < 0) {
        mixer = nullptr;
        return;
    }
    if (snd_mixer_attach(mixer, platform.mixerCard.c_str()) < 0 || snd_mixer_selem_register(mixer, nullptr, nullptr) < 0 ||
        snd_mixer_load(mixer) < 0) {
        snd_mixer_close(mixer);
        mixer = nullptr;
        logf("mixer %s unavailable", platform.mixerCard.c_str());
        return;
    }
    std::vector<std::string> names;
    if (!platform.mixerVolume.empty()) names.push_back(platform.mixerVolume);
    names.insert(names.end(), {"Master", "PCM", "Headphone", "Speaker"});
    for (const auto& n : names) {
        snd_mixer_elem_t* e = findElem(n);
        if (e && snd_mixer_selem_has_playback_volume(e)) {
            volumeElem = e;
            logf("mixer volume control %s", n.c_str());
            break;
        }
    }
    outputElem = findElem(platform.mixerOutputSwitch);
    if (outputElem && !snd_mixer_selem_has_playback_switch(outputElem)) outputElem = nullptr;
}

void refreshMixer() {
    if (mixer) snd_mixer_handle_events(mixer);
}

}  // namespace
#endif

void System::init() {
    for (const auto& dir : glob("/sys/class/power_supply/*")) {
        std::string type = readLine(dir + "/type");
        if (type == "Battery" && batteryDir_.empty()) batteryDir_ = dir;
        else if (type == "Mains" || type == "USB") {
            for (auto& c : chargerDirs_)
                if (c.empty()) {
                    c = dir;
                    break;
                }
        }
    }
    if (!platform.backlight.empty()) {
        long max = 0;
        if (readLong(platform.backlight + "/max_brightness", max)) maxBrightness_ = max;
    }
#ifdef KENNEL_ALSA
    openMixer();
#endif
    poll();
}

void System::shutdown() {
    wake();
#ifdef KENNEL_ALSA
    if (mixer) snd_mixer_close(mixer);
    mixer = nullptr;
    volumeElem = outputElem = nullptr;
#endif
}

void System::readBattery() {
    battery = Battery{};
    if (!batteryDir_.empty()) {
        long v;
        battery.present = !readLong(batteryDir_ + "/present", v) || v != 0;
        if (readLong(batteryDir_ + "/capacity", v)) battery.capacity = std::clamp((int)v, 0, 100);
        battery.status = readLine(batteryDir_ + "/status");
        battery.health = readLine(batteryDir_ + "/health");
        if (readLong(batteryDir_ + "/voltage_now", v)) battery.voltage = v / 1e6;
        if (readLong(batteryDir_ + "/current_now", v)) battery.current = v / 1e6;
        if (readLong(batteryDir_ + "/charge_full_design", v)) battery.designCharge = v / 1e6;
    }
    for (const auto& c : chargerDirs_) {
        long online;
        if (!c.empty() && readLong(c + "/online", online) && online) battery.externalPower = true;
    }
}

void System::readCpu() {
    // cpu  user nice system idle iowait irq softirq steal ...
    std::string line = readLine("/proc/stat");
    if (line.rfind("cpu ", 0) != 0) return;
    unsigned long long v[8] = {}, total = 0;
    const char* p = line.c_str() + 4;
    for (int i = 0; i < 8; ++i) {
        char* end;
        v[i] = strtoull(p, &end, 10);
        if (end == p) break;
        total += v[i];
        p = end;
    }
    unsigned long long busy = total - v[3] - v[4];
    if (cpuTotal_ && total > cpuTotal_)
        cpuPercent = (int)std::lround((busy - cpuBusy_) * 100.0 / (total - cpuTotal_));
    cpuBusy_ = busy;
    cpuTotal_ = total;
}

void System::readMemory() {
    std::string meminfo = readFile("/proc/meminfo").value_or("");
    auto field = [&](const char* key) -> uint64_t {
        size_t at = meminfo.find(key);
        return at == std::string::npos ? 0 : strtoull(meminfo.c_str() + at + strlen(key), nullptr, 10) * 1024;
    };
    memTotal = field("MemTotal:");
    memAvailable = field("MemAvailable:");
}

bool System::poll() {
    readBattery();
    readCpu();
    readMemory();
    externalMounted = false;
    if (!platform.externalMount.empty()) {
        std::string mounts = readFile("/proc/mounts").value_or("");
        externalMounted = mounts.find(" " + platform.externalMount + " ") != std::string::npos;
    }
    usbConnected = false;
    for (const auto& udc : glob("/sys/class/udc/*"))
        if (readLine(udc + "/state") == "configured") usbConnected = true;
#ifdef KENNEL_ALSA
    refreshMixer();
#endif
    std::string sig = strprintf("%d|%s|%d|%d|%d|%d|%d|%d|%llu", battery.capacity, battery.status.c_str(), battery.externalPower,
                                externalMounted, usbConnected, hasVolume() ? volume() : -1, hasOutputSwitch() ? speaker() : -1,
                                settings.barCpu ? cpuPercent : -1,
                                settings.barMemory ? (unsigned long long)(memAvailable >> 20) : 0ull);
    if (hasMute()) sig += muted() ? "|m" : "|u";
    bool changed = sig != signature_;
    signature_ = sig;
    return changed;
}

int System::brightness() const {
    long v = savedBrightness_;
    if (v < 0 && !readLong(platform.backlight + "/brightness", v)) return 0;
    return (int)v;
}

int System::backlight() const {
    long v = -1;
    if (hasBacklight() && !readLong(platform.backlight + "/brightness", v)) v = -1;
    return (int)v;
}

void System::setBrightness(int level) {
    if (!hasBacklight()) return;
    writeFile(platform.backlight + "/brightness", std::to_string(std::clamp<long>(level, 1, maxBrightness_)));
}

void System::screenOff() {
    if (!hasBacklight() || screenIsOff()) return;
    long cur = 0;
    if (!readLong(platform.backlight + "/brightness", cur) || cur <= 0) return;
    savedBrightness_ = cur;
    writeFile(platform.backlight + "/brightness", "0");
}

bool System::wake() {
    if (!screenIsOff()) return false;
    long cur = 0;
    if (readLong(platform.backlight + "/brightness", cur) && cur > 0) {  // turned on by someone else
        savedBrightness_ = -1;
        return false;
    }
    writeFile(platform.backlight + "/brightness", std::to_string(savedBrightness_));
    savedBrightness_ = -1;
    return true;
}

#ifdef KENNEL_ALSA
bool System::hasVolume() const { return volumeElem != nullptr; }

int System::volume() {
    if (!volumeElem) return 0;
    long min = 0, max = 0, v = 0;
    snd_mixer_selem_get_playback_volume_range(volumeElem, &min, &max);
    snd_mixer_selem_get_playback_volume(volumeElem, SND_MIXER_SCHN_FRONT_LEFT, &v);
    return max > min ? (int)std::lround((v - min) * 100.0 / (max - min)) : 0;
}

void System::setVolume(int percent) {
    if (!volumeElem) return;
    long min = 0, max = 0;
    snd_mixer_selem_get_playback_volume_range(volumeElem, &min, &max);
    snd_mixer_selem_set_playback_volume_all(volumeElem, min + std::lround(std::clamp(percent, 0, 100) * (max - min) / 100.0));
}

bool System::hasMute() const { return volumeElem && snd_mixer_selem_has_playback_switch(volumeElem); }

bool System::muted() {
    int on = 1;
    if (hasMute()) snd_mixer_selem_get_playback_switch(volumeElem, SND_MIXER_SCHN_FRONT_LEFT, &on);
    return !on;
}

void System::setMuted(bool m) {
    if (hasMute()) snd_mixer_selem_set_playback_switch_all(volumeElem, m ? 0 : 1);
}

bool System::hasOutputSwitch() const { return outputElem != nullptr; }

bool System::speaker() {
    int on = 1;
    if (outputElem) snd_mixer_selem_get_playback_switch(outputElem, SND_MIXER_SCHN_FRONT_LEFT, &on);
    return on != 0;
}

void System::setSpeaker(bool on) {
    if (outputElem) snd_mixer_selem_set_playback_switch_all(outputElem, on ? 1 : 0);
}
#else
bool System::hasVolume() const { return false; }
int System::volume() { return 0; }
void System::setVolume(int) {}
bool System::hasMute() const { return false; }
bool System::muted() { return false; }
void System::setMuted(bool) {}
bool System::hasOutputSwitch() const { return false; }
bool System::speaker() { return true; }
void System::setSpeaker(bool) {}
#endif
