#pragma once

#include <cstdint>
#include <string>

struct Battery {
    bool present = false;
    int capacity = -1;
    std::string status;  // Charging, Discharging, Full, Not charging
    std::string health;
    double voltage = 0;  // V
    double current = 0;  // A, sign as reported by the driver
    double designCharge = 0;  // Ah
    bool externalPower = false;
    bool charging() const { return status == "Charging" || status == "Full"; }
};

// Device state shown in the bars and changed from the quick settings.
class System {
public:
    void init();
    void shutdown();
    bool poll();  // true when anything shown in the top bar changed

    Battery battery;
    std::string batteryDir() const { return batteryDir_; }
    bool externalMounted = false;
    bool usbConnected = false;
    int cpuPercent = -1;  // busy share of all cores since the previous poll
    uint64_t memAvailable = 0, memTotal = 0;

    bool hasBacklight() const { return maxBrightness_ > 0; }
    int maxBrightness() const { return (int)maxBrightness_; }
    int brightness() const;  // raw level
    int backlight() const;   // raw level as set now (0 while off), -1 without a backlight
    void setBrightness(int level);
    void screenOff();
    bool wake();  // true if the screen was off
    bool screenIsOff() const { return savedBrightness_ >= 0; }

    bool hasVolume() const;
    int volume();
    void setVolume(int percent);
    bool hasMute() const;
    bool muted();
    void setMuted(bool m);
    bool hasOutputSwitch() const;
    bool speaker();
    void setSpeaker(bool on);

private:
    void readBattery();
    void readCpu();
    void readMemory();
    std::string signature_;
    unsigned long long cpuBusy_ = 0, cpuTotal_ = 0;
    long maxBrightness_ = 0;
    long savedBrightness_ = -1;
    std::string batteryDir_;
    std::string chargerDirs_[4];
};

extern System sys;
