#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct Process {
    int pid = 0;
    int uid = 0;
    std::string user;
    char state = '?';
    long rssKb = 0;
    float cpu = 0;  // percent of one core since the previous sample
    std::string name;
    std::string cmdline;
};

// /proc sampler; CPU usage is the difference between two samples.
class ProcessList {
public:
    void sample();
    std::vector<Process> processes;
    double cpuTotal = 0;  // all cores, percent of one core

private:
    std::unordered_map<int, unsigned long long> lastTicks_;
    unsigned long long lastTotal_ = 0;
    std::unordered_map<int, std::string> users_;
};

std::string userName(int uid);
