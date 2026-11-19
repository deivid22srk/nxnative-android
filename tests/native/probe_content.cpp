// Host-side content-key probe: prints the same JSON the app produces.
// usage: probe_content <game.nsp> <prod.keys>
// It never prints key values or decrypted material.
#include <cstdio>
#include <string>

#include "content_check.h"
#include "keys.h"

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
    const nx2a::ContentCheck c = nx2a::checkContentKeys(argv[1], keys);
    std::printf("%s\n", nx2a::contentCheckJson(c).c_str());
    return c.ok ? 0 : 1;
}