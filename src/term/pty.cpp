#include "pty.h"

#include <SDL3/SDL_events.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "paths.h"
#include "util.h"

// The reader may block on a terminal that a background process keeps open: it is left to the exit.
Pty::~Pty() {
    hangup();
    if (reader_.joinable()) reader_.detach();
}

bool Pty::start(const std::string& cwd, const std::string& command, int cols, int rows, std::string& error) {
    fd_ = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0 || grantpt(fd_) != 0 || unlockpt(fd_) != 0) {
        error = std::string("pseudo terminal: ") + strerror(errno);
        return false;
    }
    const char* slaveName = ptsname(fd_);
    if (!slaveName) {
        error = std::string("ptsname: ") + strerror(errno);
        return false;
    }
    std::string slavePath = slaveName;
    winsize ws{};
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ioctl(fd_, TIOCSWINSZ, &ws);

    std::string shell = "/bin/sh";
    if (passwd* pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell && access(pw->pw_shell, X_OK) == 0)
        shell = pw->pw_shell;

    std::string rc = paths.asset("term/shrc");
    pid_ = fork();
    if (pid_ < 0) {
        error = std::string("fork: ") + strerror(errno);
        return false;
    }
    if (pid_ == 0) {
        setsid();
        int slave = open(slavePath.c_str(), O_RDWR);
        if (slave < 0) _exit(127);
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) close(slave);
        for (int sig : {SIGPIPE, SIGINT, SIGTERM, SIGHUP, SIGCHLD}) signal(sig, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        const char* home = getenv("HOME");
        if ((cwd.empty() || chdir(cwd.c_str()) != 0) && home) (void)!chdir(home);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("SHELL", shell.c_str(), 1);
        setenv("ENV", rc.c_str(), 0);
        unsetenv("KENNEL_AUTOMATE");
        if (!command.empty()) execl("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
        else execl(shell.c_str(), shell.c_str(), "-l", (char*)nullptr);
        _exit(127);
    }
    reader_ = std::thread(&Pty::readLoop, this);
    return true;
}

void Pty::readLoop() {
    char buf[16384];
    while (true) {
        ssize_t n = read(fd_, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        bool wake;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            wake = buffer_.empty();
            buffer_.append(buf, (size_t)n);
        }
        if (wake) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_USER;
            SDL_PushEvent(&ev);
        }
    }
    eof_ = true;
    SDL_Event ev{};
    ev.type = SDL_EVENT_USER;
    SDL_PushEvent(&ev);
}

std::string Pty::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out;
    out.swap(buffer_);
    return out;
}

void Pty::write(std::string_view data) {
    while (!data.empty() && fd_ >= 0) {
        ssize_t n = ::write(fd_, data.data(), data.size());
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data.remove_prefix((size_t)n);
    }
}

void Pty::resize(int cols, int rows) {
    if (fd_ < 0) return;
    winsize ws{};
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ioctl(fd_, TIOCSWINSZ, &ws);
}

int Pty::exitStatus() {
    if (status_ < 0 && pid_ > 0) {
        int st = 0;
        if (waitpid(pid_, &st, 0) == pid_) status_ = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    }
    return status_;
}

bool Pty::shellInForeground() const {
    if (fd_ < 0) return false;
    return tcgetpgrp(fd_) == pid_;
}

std::string Pty::cwd() const {
    char buf[4096];
    ssize_t n = readlink(strprintf("/proc/%d/cwd", (int)pid_).c_str(), buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, (size_t)n) : std::string();
}

void Pty::hangup() {
    if (pid_ > 0 && status_ < 0 && !eof_) kill(-pid_, SIGHUP);
}
