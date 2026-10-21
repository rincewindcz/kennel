#pragma once

#include <string>

// Copy of kennel's settings and data (statistics, favourites, hidden entries, battery history,
// reading positions, terminal history) in a kennel-backup folder: on the external card when it is
// mounted, else in HOME. The art cache is not included.
std::string backupDir();
// "3 h ago, 42 sessions" style summary of the backup in dir, empty when there is none.
std::string describeBackup(const std::string& dir);
bool makeBackup(const std::string& dir, std::string& error);
// Replaces the current settings and data with the backup; kennel must restart afterwards.
bool restoreBackup(const std::string& dir, std::string& error);
