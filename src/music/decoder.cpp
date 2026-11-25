#include "decoder.h"

#include <algorithm>
#include <cstdio>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "stb_vorbis.c"
#pragma GCC diagnostic pop

#include "util.h"

namespace {

std::string extensionOf(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? "" : lower(name.substr(dot + 1));
}

class Mp3 : public Decoder {
public:
    ~Mp3() override {
        if (open_) drmp3_uninit(&mp3_);
    }
    bool open(const std::string& path) {
        if (!drmp3_init_file(&mp3_, path.c_str(), nullptr)) return false;
        open_ = true;
        rate = (int)mp3_.sampleRate;
        channels = (int)mp3_.channels;
        if (mp3_.totalPCMFrameCount != DRMP3_UINT64_MAX) {
            uint64_t skip = (uint64_t)mp3_.delayInPCMFrames + mp3_.paddingInPCMFrames;
            frames = mp3_.totalPCMFrameCount > skip ? mp3_.totalPCMFrameCount - skip : 0;
        } else {
            frames = estimate(path);
        }
        return rate > 0 && channels > 0;
    }
    size_t read(float* out, size_t n) override { return (size_t)drmp3_read_pcm_frames_f32(&mp3_, n, out); }

private:
    // Without a Xing or Info header: the length at the bitrate of the first frame (exact for CBR). Counting
    // the frames would read the whole file.
    uint64_t estimate(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return 0;
        uint8_t hdr[4] = {};
        bool ok = fseek(f, (long)mp3_.streamStartOffset, SEEK_SET) == 0 && fread(hdr, 1, 4, f) == 4;
        fclose(f);
        if (!ok || !drmp3_hdr_valid(hdr)) return 0;
        unsigned kbps = drmp3_hdr_bitrate_kbps(hdr);
        uint64_t bytes = mp3_.streamLength > mp3_.streamStartOffset ? mp3_.streamLength - mp3_.streamStartOffset : 0;
        return kbps ? bytes * 8 * (uint64_t)rate / (kbps * 1000ull) : 0;
    }

    drmp3 mp3_{};
    bool open_ = false;
};

class Flac : public Decoder {
public:
    ~Flac() override {
        if (flac_) drflac_close(flac_);
    }
    bool open(const std::string& path) {
        flac_ = drflac_open_file(path.c_str(), nullptr);
        if (!flac_) return false;
        rate = (int)flac_->sampleRate;
        channels = flac_->channels;
        frames = flac_->totalPCMFrameCount;
        return rate > 0 && channels > 0;
    }
    size_t read(float* out, size_t n) override { return (size_t)drflac_read_pcm_frames_f32(flac_, n, out); }

private:
    drflac* flac_ = nullptr;
};

class Wav : public Decoder {
public:
    ~Wav() override {
        if (open_) drwav_uninit(&wav_);
    }
    bool open(const std::string& path) {
        if (!drwav_init_file(&wav_, path.c_str(), nullptr)) return false;
        open_ = true;
        rate = (int)wav_.sampleRate;
        channels = wav_.channels;
        frames = wav_.totalPCMFrameCount;
        return rate > 0 && channels > 0;
    }
    size_t read(float* out, size_t n) override { return (size_t)drwav_read_pcm_frames_f32(&wav_, n, out); }

private:
    drwav wav_{};
    bool open_ = false;
};

class Vorbis : public Decoder {
public:
    ~Vorbis() override {
        if (vorbis_) stb_vorbis_close(vorbis_);
    }
    bool open(const std::string& path) {
        int error = 0;
        vorbis_ = stb_vorbis_open_filename(path.c_str(), &error, nullptr);
        if (!vorbis_) return false;
        stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
        rate = (int)info.sample_rate;
        channels = info.channels;
        frames = stb_vorbis_stream_length_in_samples(vorbis_);
        return rate > 0 && channels > 0;
    }
    size_t read(float* out, size_t n) override {
        int got = stb_vorbis_get_samples_float_interleaved(vorbis_, channels, out, (int)std::min<size_t>(n * channels, 1 << 20));
        return got > 0 ? (size_t)got : 0;
    }

private:
    stb_vorbis* vorbis_ = nullptr;
};

template <class T>
std::unique_ptr<Decoder> tryOpen(const std::string& path) {
    auto d = std::make_unique<T>();
    if (!d->open(path)) return nullptr;
    return d;
}

}  // namespace

std::unique_ptr<Decoder> openDecoder(const std::string& path) {
    std::string ext = extensionOf(path);
    if (ext == "mp3") return tryOpen<Mp3>(path);
    if (ext == "flac") return tryOpen<Flac>(path);
    if (ext == "wav") return tryOpen<Wav>(path);
    if (ext == "ogg" || ext == "oga") return tryOpen<Vorbis>(path);
    return nullptr;
}

bool isAudioFile(const std::string& name) {
    std::string ext = extensionOf(name);
    return ext == "mp3" || ext == "flac" || ext == "wav" || ext == "ogg" || ext == "oga";
}
