#include "battery.h"

#include <cmath>
#include <algorithm>
#include <cstdlib>

#include "system.h"
#include "util.h"

BatteryLog batteryLog;

namespace {
constexpr size_t kMaxBytes = 1 << 20, kKeepBytes = 512 << 10;
constexpr double kWindow = 3600, kMinSpan = 600, kMinRate = 0.5;
}  // namespace

void BatteryLog::add(const std::string& boot, const BatterySample& s) {
    if (boots_.empty() || boots_.back().id != boot) boots_.push_back({boot, {}});
    boots_.back().samples.push_back(s);
}

void BatteryLog::load(const std::string& path) {
    path_ = path;
    bootId_ = bootId();
    boots_.clear();
    std::string text = readFile(path).value_or("");
    if (text.size() > kMaxBytes) {
        size_t cut = text.find('\n', text.size() - kKeepBytes);
        text = cut == std::string::npos ? std::string() : text.substr(cut + 1);
        writeFileAtomic(path, text);
    }
    for (const auto& line : split(text, '\n')) {
        auto f = split(line, '\t');
        if (f.size() < 5 || f[0].empty() || f[3].empty()) continue;
        BatterySample s;
        s.uptime = atof(f[1].c_str());
        s.capacity = atoi(f[2].c_str());
        s.status = f[3][0];
        s.millivolts = atoi(f[4].c_str());
        add(f[0], s);
    }
}

const BatteryBoot* BatteryLog::current() const {
    return !boots_.empty() && boots_.back().id == bootId_ ? &boots_.back() : nullptr;
}

void BatteryLog::sample(const Battery& b) {
    if (path_.empty() || b.capacity < 0) return;
    BatterySample s;
    s.uptime = uptimeSeconds();
    s.capacity = b.capacity;
    s.status = b.status.empty() ? '?' : b.status[0];
    s.millivolts = (int)std::lround(b.voltage * 1000);
    const BatteryBoot* cur = current();
    if (cur && !cur->samples.empty()) {
        const BatterySample& last = cur->samples.back();
        if (s.uptime - last.uptime < kInterval && s.status == last.status) return;
    }
    add(bootId_, s);
    appendFile(path_, strprintf("%s\t%.0f\t%d\t%c\t%d\n", bootId_.c_str(), s.uptime, s.capacity, s.status, s.millivolts));
}

BatteryEstimate BatteryLog::estimate(const Battery& b) const {
    BatteryEstimate e;
    if (b.capacity < 0) return e;
    bool charging = b.status == "Charging" && b.capacity < 100;
    bool discharging = b.status == "Discharging";
    if (!charging && !discharging) return e;

    // Least squares slope over the samples of the current charge or discharge, within the last hour.
    if (const BatteryBoot* cur = current()) {
        const auto& v = cur->samples;
        char want = charging ? 'C' : 'D';
        size_t first = v.size();
        while (first > 0 && v[first - 1].status == want && v.back().uptime - v[first - 1].uptime <= kWindow) --first;
        if (v.size() - first >= 3 && v.back().uptime - v[first].uptime >= kMinSpan) {
            double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (size_t i = first; i < v.size(); ++i) {
                double x = (v[i].uptime - v[first].uptime) / 3600, y = v[i].capacity;
                n += 1;
                sx += x;
                sy += y;
                sxx += x * x;
                sxy += x * y;
            }
            double d = n * sxx - sx * sx;
            if (d > 0) {
                double slope = (n * sxy - sx * sy) / d;
                if ((charging && slope > 0.1) || (discharging && slope < -0.1)) {
                    e.ratePerHour = slope;
                    e.fromHistory = true;
                }
            }
        }
    }
    if (!e.fromHistory && std::fabs(b.current) > 0.001 && b.designCharge > 0) {
        double rate = std::fabs(b.current) / b.designCharge * 100;
        e.ratePerHour = charging ? rate : -rate;
    }
    if (std::fabs(e.ratePerHour) < kMinRate) {
        e.ratePerHour = 0;
        return e;
    }
    double left = charging ? 100 - b.capacity : b.capacity;
    e.seconds = left / std::fabs(e.ratePerHour) * 3600;
    return e;
}

std::string describeEstimate(const Battery& b, const BatteryEstimate& e) {
    if (b.capacity < 0) return "no battery";
    std::string out = strprintf("%d%%", b.capacity);
    if (b.status == "Full") return out + ", full";
    if (e.seconds < 0) return b.status == "Charging" ? out + ", charging" : out;
    std::string t = formatDuration(std::max(300.0, std::round(e.seconds / 300) * 300));
    return out + (b.status == "Charging" ? ", full in about " : ", about ") + t + (b.status == "Charging" ? "" : " left");
}
