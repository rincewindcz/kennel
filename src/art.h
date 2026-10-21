#pragma once

#include <SDL3/SDL.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "image.h"

struct Entry;
struct Category;

// Card and backdrop textures. Images are decoded and scaled on a worker thread and cached on disk
// as QOI; stock cards (no image, or a small icon) are composed on the GPU from the title, the
// category image and colour.
class ArtCache {
public:
    static constexpr int kW = 300, kH = 200, kRadius = 14;
    static constexpr int kBackW = 128, kBackH = 96;

    struct Card {
        SDL_Texture* stock = nullptr;
        SDL_Texture* art = nullptr;
        float artAlpha = 0;
    };

    void start(const std::string& cacheDir);
    void stop();

    Card card(const Entry& e, const Category* c);
    void prefetch(const Entry& e);
    SDL_Texture* backdrop(const Entry& e, const Category* c, float& alpha);
    void pump();
    bool busy();
    void clear();
    size_t textureCount() const { return slots_.size(); }
    double composeMs = 0;

private:
    enum Kind { kCover, kBackdrop };
    struct Job {
        std::string key, path;
        Kind kind;
    };
    struct Result {
        std::string key;
        Image img;
        bool icon = false;
        bool ok = false;
    };
    struct Slot {
        SDL_Texture* tex = nullptr;
        bool requested = false, failed = false, icon = false;
        double readyAt = 0;
        uint64_t used = 0;
    };

    void worker();
    void want(const std::string& key, const std::string& path, Kind kind);
    Result process(const Job& job);
    std::string diskPath(const std::string& path, const char* tag) const;
    Slot& slot(const std::string& key);
    SDL_Texture* compose(const Entry& e, const Category* c, SDL_Texture* icon, int iconW, int iconH);
    SDL_Texture* categoryImage(const std::string& path);
    SDL_Texture* gradientCard(uint32_t color);
    SDL_Texture* scrim();
    void evict();

    std::string dir_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::vector<Result> results_;
    std::vector<Job> wants_;
    std::unordered_map<std::string, bool> inflight_;
    bool quit_ = false;
    int composedThisFrame_ = 0;

    std::unordered_map<std::string, Slot> slots_;
    std::unordered_map<std::string, SDL_Texture*> fixed_;
};

extern ArtCache art;
