#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "decoder.h"
#include "music.h"

namespace {

// Decodes each file to the end and prints its format, the length the decoder reports and the length
// decoded; exit status 1 when a file cannot be opened.
int probe(int argc, char** argv) {
    int status = 0;
    std::vector<float> buf;
    for (int i = 2; i < argc; ++i) {
        auto d = openDecoder(argv[i]);
        if (!d) {
            printf("%s: cannot decode\n", argv[i]);
            status = 1;
            continue;
        }
        buf.resize(4096 * d->channels);
        uint64_t total = 0;
        while (size_t n = d->read(buf.data(), 4096)) total += n;
        printf("%s: %d Hz, %d ch, length %.2f s, decoded %.2f s\n", argv[i], d->rate, d->channels,
               d->frames ? (double)d->frames / d->rate : 0.0, (double)total / d->rate);
    }
    return status;
}

}  // namespace

// kennel-music [--library DIR[;DIR...]]   album list of the folders (default ~/music)
// kennel-music FILE|FOLDER                plays the album of FILE (from FILE) or FOLDER
// kennel-music --probe FILE...            decodes the files and prints their lengths (development)
int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "--probe")) return probe(argc, argv);
    std::string path, library;
    if (argc == 3 && !strcmp(argv[1], "--library")) library = argv[2];
    else if (argc == 2 && argv[1][0] != '-') path = argv[1];
    else if (argc != 1) {
        fprintf(stderr, "usage: kennel-music [--library DIR[;DIR...]] | FILE | FOLDER | --probe FILE...\n");
        return 2;
    }
    static Music music;
    return music.run(argv, path, library);
}
