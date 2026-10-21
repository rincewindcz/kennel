#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

std::string trim(std::string_view s);
std::string lower(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
std::vector<std::string> split(std::string_view s, char sep);
std::string replaceAll(std::string s, std::string_view from, std::string_view to);
std::string shellQuote(std::string_view s);
std::string strprintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

std::optional<std::string> readFile(const std::string& path);
std::string readTail(const std::string& path, size_t maxBytes, bool* truncated = nullptr);
std::string readLine(const std::string& path);
bool readLong(const std::string& path, long& out);
bool writeFileAtomic(const std::string& path, std::string_view data);
bool writeFile(const std::string& path, std::string_view data);
bool appendFile(const std::string& path, std::string_view data);
bool makeDirs(const std::string& path);
bool fileExists(const std::string& path);
bool isDir(const std::string& path);
std::string dirName(const std::string& path);
std::string baseName(const std::string& path);
std::vector<std::string> glob(const std::string& pattern);

std::string runCapture(const std::string& command, int* status = nullptr);
bool spawnDetached(const std::string& command);

uint64_t fnv1a(std::string_view s, uint64_t h = 1469598103934665603ull);
uint32_t colorFromName(std::string_view name);
bool parseColor(std::string_view s, uint32_t& out);

double uptimeSeconds();
double wallSeconds();
bool wallClockValid(double wall);
std::string bootId();

std::string formatDuration(double seconds);
std::string formatClock(double seconds);
std::string formatBytes(uint64_t bytes);
std::string formatAgo(double seconds);

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logToFile(const std::string& path);
