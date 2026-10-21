#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <thread>

// Shell on a pseudo terminal. A reader thread collects the output and pushes an SDL_EVENT_USER
// when new output arrives after the previous take().
class Pty {
public:
    ~Pty();
    // Starts COMMAND with sh -c, or the user's login shell when COMMAND is empty.
    bool start(const std::string& cwd, const std::string& command, int cols, int rows, std::string& error);
    void resize(int cols, int rows);
    void write(std::string_view data);
    std::string take();
    bool ended() const { return eof_; }
    int exitStatus();
    bool shellInForeground() const;
    std::string cwd() const;
    void hangup();

private:
    void readLoop();

    int fd_ = -1;
    pid_t pid_ = -1;
    int status_ = -1;
    std::thread reader_;
    std::mutex mutex_;
    std::string buffer_;
    std::atomic<bool> eof_{false};
};
