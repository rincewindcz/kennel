#pragma once

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
    bool externalMounted = false;
    bool usbConnected = false;

    bool hasBacklight() const { return maxBrightness_ > 0; }
    int brightness() const;
    void setBrightness(int percent);
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
    std::string signature_;
    long maxBrightness_ = 0;
    long savedBrightness_ = -1;
    std::string batteryDir_;
    std::string chargerDirs_[4];
};

extern System sys;
