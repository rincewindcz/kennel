#include "player.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>

#include "util.h"

namespace {

constexpr size_t kChunkFrames = 2048;
constexpr double kAhead = 1.0;  // seconds decoded ahead of the device
constexpr auto kRefill = std::chrono::milliseconds(250);

}  // namespace

Player::~Player() { close(); }

void Player::open() { thread_ = std::thread([this] { feed(); }); }

void Player::close() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    cv_.notify_one();
    thread_.join();
    if (stream_) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
}


void Player::load(std::vector<std::string> tracks) {
    {
        std::lock_guard lock(mutex_);
        tracks_ = std::move(tracks);
        failed_.assign(tracks_.size(), false);
        ++generation_;
        paused_ = true;
        if (stream_) SDL_PauseAudioStreamDevice(stream_);
        request_ = tracks_.empty() ? -1 : 0;
    }
    cv_.notify_one();
}

void Player::play(int track) {
    {
        std::lock_guard lock(mutex_);
        if (track < 0 || track >= (int)tracks_.size()) return;
        request_ = track;
        paused_ = false;
        if (stream_) SDL_ResumeAudioStreamDevice(stream_);
    }
    cv_.notify_one();
}

void Player::skip(int track) {
    {
        std::lock_guard lock(mutex_);
        if (track < 0 || track >= (int)tracks_.size()) return;
        request_ = track;
    }
    cv_.notify_one();
}

void Player::setPaused(bool paused) {
    {
        std::lock_guard lock(mutex_);
        paused_ = paused;
        if (stream_ && paused) SDL_PauseAudioStreamDevice(stream_);
        else if (stream_) SDL_ResumeAudioStreamDevice(stream_);
    }
    cv_.notify_one();
}

void Player::setLoop(Loop loop) {
    std::lock_guard lock(mutex_);
    loop_ = loop;
}

Player::Status Player::status() {
    std::lock_guard lock(mutex_);
    Status s;
    s.paused = paused_;
    s.unavailable = unavailable_;
    if (request_ >= 0) {
        s.track = request_;
        return s;
    }
    uint64_t played = playedFrames();
    while (segments_.size() > 1 && segments_[1].start <= played) segments_.pop_front();
    if (segments_.empty()) {
        s.track = -1;
        return s;
    }
    const Segment& seg = segments_.front();
    s.track = seg.track;
    s.length = seg.length;
    s.position = played > seg.start ? (double)(played - seg.start) / kRate : 0;
    if (s.length > 0) s.position = std::min(s.position, s.length);
    return s;
}

std::vector<bool> Player::failed() {
    std::lock_guard lock(mutex_);
    return failed_;
}

uint64_t Player::playedFrames() const {
    if (!stream_) return 0;
    uint64_t queued = (uint64_t)std::max(0, SDL_GetAudioStreamQueued(stream_)) / kFrameBytes;
    return fed_ > queued ? fed_ - queued : 0;
}

int Player::following(int track) const {
    if (loop_ == Loop::Track) return track;
    if (track + 1 < (int)tracks_.size()) return track + 1;
    return loop_ == Loop::Album ? 0 : -1;
}

void Player::feed() {
    // Opening the device can take over a second (the A23 codec powers up), so it happens here.
    SDL_AudioSpec spec{SDL_AUDIO_F32, kChannels, kRate};
    uint64_t t0 = SDL_GetTicksNS();
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream) logf("audio: %s, device opened in %.0f ms", SDL_GetCurrentAudioDriver(), (SDL_GetTicksNS() - t0) / 1e6);
    else logf("audio: %s", SDL_GetError());
    std::unique_lock lock(mutex_);
    if (!stream) {
        unavailable_ = true;
        return;
    }
    stream_ = stream;
    if (!paused_) SDL_ResumeAudioStreamDevice(stream_);
    while (!quit_) {
        if (request_ >= 0) {
            int track = request_;
            request_ = -1;
            start(track, true, lock);
            continue;
        }
        // The album ended and the device has played everything: back to the first track, paused.
        if (ending_ && SDL_GetAudioStreamQueued(stream_) <= 0) {
            ending_ = false;
            paused_ = true;
            SDL_PauseAudioStreamDevice(stream_);
            if (!tracks_.empty()) start(0, true, lock);
            continue;
        }
        if (decoder_ && SDL_GetAudioStreamQueued(stream_) < kAhead * kRate * kFrameBytes) {
            pump(lock);
            continue;
        }
        if (decoder_ && !paused_) cv_.wait_for(lock, kRefill);
        else if (ending_) cv_.wait_for(lock, kRefill);
        else cv_.wait(lock);
    }
    decoder_.reset();
    if (convert_) SDL_DestroyAudioStream(convert_);
    convert_ = nullptr;
}

// Opens track and appends its segment. A track chosen by the caller (fresh) clears what is queued and
// gives way to the ones after it when it cannot be decoded; at the end of a track the loop mode
// decides. Returns false when nothing could be opened.
bool Player::start(int track, bool fresh, std::unique_lock<std::mutex>& lock) {
    if (fresh) {
        SDL_ClearAudioStream(stream_);
        fed_ = 0;
        segments_.clear();
        ending_ = false;
    }
    decoder_.reset();
    if (convert_) SDL_DestroyAudioStream(convert_);
    convert_ = nullptr;

    int n = (int)tracks_.size();
    for (int tries = 0; tries < n && track >= 0; ++tries) {
        std::string path = tracks_[track];
        uint64_t generation = generation_;
        lock.unlock();
        std::unique_ptr<Decoder> d = openDecoder(path);
        lock.lock();
        if (quit_ || request_ >= 0 || generation != generation_) return false;
        if (d) {
            SDL_AudioSpec src{SDL_AUDIO_F32, d->channels, d->rate}, dst{SDL_AUDIO_F32, kChannels, kRate};
            convert_ = SDL_CreateAudioStream(&src, &dst);
            if (convert_) {
                segments_.push_back({track, fed_, d->frames ? (double)d->frames / d->rate : 0});
                decoder_ = std::move(d);
                return true;
            }
            logf("audio: %s: %s", path.c_str(), SDL_GetError());
        } else {
            logf("audio: cannot decode %s", path.c_str());
        }
        failed_[track] = true;
        track = fresh ? (track + 1) % n : following(track);
    }
    segments_.push_back({-1, fed_, 0});
    if (!fresh) ending_ = true;
    return false;
}

void Player::pump(std::unique_lock<std::mutex>& lock) {
    Decoder* d = decoder_.get();
    int track = segments_.empty() ? -1 : segments_.back().track;
    lock.unlock();
    std::vector<float> in(kChunkFrames * d->channels);
    size_t got = d->read(in.data(), kChunkFrames);
    if (got) SDL_PutAudioStreamData(convert_, in.data(), (int)(got * d->channels * sizeof(float)));
    else SDL_FlushAudioStream(convert_);
    std::vector<uint8_t> out((size_t)std::max(0, SDL_GetAudioStreamAvailable(convert_)));
    int bytes = out.empty() ? 0 : SDL_GetAudioStreamData(convert_, out.data(), (int)out.size());
    lock.lock();
    if (quit_ || request_ >= 0) return;
    if (bytes > 0) {
        SDL_PutAudioStreamData(stream_, out.data(), bytes);
        fed_ += (uint64_t)bytes / kFrameBytes;
    }
    if (got) return;
    int next = following(track);
    if (next < 0) {
        decoder_.reset();
        segments_.push_back({-1, fed_, 0});
        ending_ = true;
        return;
    }
    start(next, false, lock);
}
