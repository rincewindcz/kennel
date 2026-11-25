#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "input.h"
#include "player.h"

// Album list (folders with audio files under the library folders) and the player for one album.
class Music {
public:
    // path: an audio file (its folder is the album, playback starts with it), a folder, or empty for
    // the album list of library (folders separated by ';').
    int run(char** argv, const std::string& path, const std::string& library);

private:
    struct Album {
        std::string dir, name;
        int tracks = 0;
    };
    struct Hint {
        Key key;
        std::string label;
    };

    void scanAlbums();
    void openAlbum(const std::string& dir, const std::string& name, const std::string& select);
    void closeAlbum();
    void loadCover();
    void startAudio();
    void loadSettings();
    void saveSettings();

    void input(const KeyEvent& ev);
    void listInput(const KeyEvent& ev);
    void playerInput(const KeyEvent& ev);
    void step(int dir);
    void poll();
    void automate(std::vector<KeyEvent>& keys);

    void draw();
    void drawList();
    void drawPlayer();
    void drawRows(float top, float bottom, int count, int cursor, int& scroll, float rowH,
                  const std::function<void(int, const SDL_FRect&, bool)>& row);
    void drawBar(const std::vector<Hint>& hints);

    Player player_;
    bool audio_ = false, audioTried_ = false;  // the device opens after the first frame
    Loop loop_ = Loop::Off;
    std::string settingsPath_;

    std::vector<std::string> roots_;
    std::vector<Album> albums_;
    int albumCursor_ = 0, albumScroll_ = 0;

    bool inAlbum_ = false, fromList_ = false;
    std::string albumDir_, albumName_;
    std::vector<std::string> tracks_, names_;
    std::vector<bool> failed_;
    int cursor_ = 0, scroll_ = 0, followed_ = 0;
    Player::Status status_;
    long shownSecond_ = -1;
    SDL_Texture* cover_ = nullptr;

    bool running_ = true, dirty_ = true;
    std::vector<std::string> script_;
    double scriptAt_ = 0;
    std::string shot_;
};
