#pragma once

#include <string>
#include <vector>

struct Battery;

// Battery history: one sample a minute in DATA/battery.tsv, written by kennel while it runs and by
// kennel-run while an app runs. Lines: boot_id, uptime, capacity, status letter, voltage in mV.
struct BatterySample {
    double uptime = 0;
    int capacity = 0;
    char status = '?';  // first letter of the power_supply status: C, D, F, N, U
    int millivolts = 0;
};

struct BatteryBoot {
    std::string id;
    std::vector<BatterySample> samples;
};

struct BatteryEstimate {
    double seconds = -1;  // until empty (discharging) or full (charging); < 0 unknown
    double ratePerHour = 0;  // percent per hour, positive while charging
    bool fromHistory = false;  // false: from the instantaneous current
};

class BatteryLog {
public:
    static constexpr double kInterval = 60;

    void load(const std::string& path);
    void sample(const Battery& b);
    const std::vector<BatteryBoot>& boots() const { return boots_; }
    const BatteryBoot* current() const;
    BatteryEstimate estimate(const Battery& b) const;
    const std::string& path() const { return path_; }

private:
    void add(const std::string& boot, const BatterySample& s);

    std::string path_;
    std::string bootId_;
    std::vector<BatteryBoot> boots_;
};

std::string describeEstimate(const Battery& b, const BatteryEstimate& e);

extern BatteryLog batteryLog;
