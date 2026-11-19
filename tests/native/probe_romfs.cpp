// Host-side RomFS probe: lists the tree of the program NCA without writing
// game data. usage: probe_romfs <game.nsp> <prod.keys>
// It prints counts and sizes only, never file names or key values.
#include <cstdio>
#include <string>

#include "keys.h"
#include "nca.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <game.nsp> <prod.keys>\n", argv[0]);
        return 2;
    }
    nx2a::KeySet keys;
    std::string error;
    if (!keys.loadFile(argv[2], &error)) {
        std::fprintf(stderr, "keys: %s\n", error.c_str());
        return 2;
    }
    nx2a::NcaInfo info;
    if (!nx2a::inspectNca(argv[1], keys, &info)) {
        std::printf("nca: %s\n", info.error.c_str());
        return 1;
    }
    std::printf("nca: magic=%s type=%d title=%s sections=", info.magic.c_str(),
                info.contentType, info.titleId.c_str());
    for (const auto& s : info.sections) {
        std::printf("%s", s.present ? "1" : "0");
    }
    std::printf("\n");
    for (int i = 0; i < 4; ++i) {
        const auto& s = info.sections[i];
        if (s.present) {
            std::printf("  s%d off=%llu size=%llu part=%d fs=%d crypt=%d romfs=%d romfsOff=%llu\n",
                        i, static_cast<unsigned long long>(s.offset),
                        static_cast<unsigned long long>(s.size), s.partition, s.fsType,
                        s.cryptType, s.romfs ? 1 : 0,
                        static_cast<unsigned long long>(s.romfsOffset));
        }
    }
    nx2a::RomfsListing listing;
    if (!nx2a::listRomfs(argv[1], keys, &listing)) {
        std::printf("romfs: %s\n", listing.error.c_str());
        return 1;
    }
    std::printf("romfs: files=%zu bytes=%llu section_size=%llu\n", listing.files.size(),
                static_cast<unsigned long long>(listing.totalBytes),
                static_cast<unsigned long long>(listing.romfsSize));
    return 0;
}