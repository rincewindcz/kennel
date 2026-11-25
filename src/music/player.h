#pragma once

#include <SDL3/SDL_audio.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "decoder.h"

enum class Loop { Off, Album, Track };

// Plays the tracks of an album on the default audio device. A feeder thread decodes about a second
// ahead into one device stream at a fixed format, opening the next track when one ends, so tracks
// follow without a gap. Track changes asked for by the caller are carried out by the feeder too.
class Player {
public:
    struct Status {
        int track = 0;
        double position = 0, length = 0;  // seconds; length 0 when unknown
        bool paused = true;
        bool unavailable = false;  // the device could not be opened
    };

    ~Player();
    // Starts the feeder, which opens the default playback device; requests made before it is open wait.
    void open();
    void close();
    // Replaces the album; nothing plays until play().
    void load(std::vector<std::string> tracks);
    // Starts track from its beginning and resumes playback.
    void play(int track);
    // Starts track from its beginning without changing the pause state.
    void skip(int track);
    void setPaused(bool paused);
    void setLoop(Loop loop);
    Status status();
    // Tracks that could not be decoded.
    std::vector<bool> failed();

private:
    static constexpr int kRate = 44100, kChannels = 2;
    static constexpr int kFrameBytes = kChannels * sizeof(float);

    // The data fed into the stream from frame `start` on belongs to track; -1 marks the end of the album.
    struct Segment {
        int track;
        uint64_t start;
        double length;
    };

    void feed();
    bool start(int track, bool fresh, std::unique_lock<std::mutex>& lock);
    int following(int track) const;
    uint64_t playedFrames() const;
    void pump(std::unique_lock<std::mutex>& lock);  // decodes one chunk of the current track

    SDL_AudioStream* stream_ = nullptr;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;

    // Shared with the feeder, guarded by mutex_.
    std::vector<std::string> tracks_;
    std::vector<bool> failed_;
    std::deque<Segment> segments_;
    uint64_t fed_ = 0, generation_ = 0;  // fed_: frames put into stream_ since the last clear
    int request_ = -1;
    bool paused_ = true, quit_ = false, ending_ = false, unavailable_ = false;
    Loop loop_ = Loop::Off;

    // Feeder only.
    std::unique_ptr<Decoder> decoder_;
    SDL_AudioStream* convert_ = nullptr;
};
