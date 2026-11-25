#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// An audio file decoded to interleaved float frames.
class Decoder {
public:
    virtual ~Decoder() = default;
    // Reads up to `frames` frames (channels floats each) into out; 0 at the end or on an error.
    virtual size_t read(float* out, size_t frames) = 0;

    int rate = 0, channels = 0;
    uint64_t frames = 0;  // length in frames, 0 when unknown
};

// MP3, Ogg Vorbis, FLAC or WAV, chosen by the extension; null when the file cannot be decoded.
std::unique_ptr<Decoder> openDecoder(const std::string& path);
bool isAudioFile(const std::string& name);
