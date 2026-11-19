// Host-side probe: prints the same JSON the app's validator produces.
// usage: probe_nsp <game.nsp> [prod.keys]
// It never prints key values.
#include <cstdio>
#include <string>

#include "import_check.h"
#include "keys.h"
#include "nsp.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <game.nsp> [prod.keys]\n", argv[0]);
        return 2;
    }
    nx2a::NspInfo info;
    if (!nx2a::inspectNspFile(argv[1], &info)) {
        std::printf("%s\n", nx2a::analyzeImport(info, nx2a::KeySet{}).c_str());
        return 1;
    }
    nx2a::KeySet keys;
    std::string error;
    if (argc > 2 && !keys.loadFile(argv[2], &error)) {
        std::fprintf(stderr, "keys: %s\n", error.c_str());
        return 2;
    }
    std::printf("%s\n", nx2a::analyzeImport(info, keys).c_str());
    return info.ok ? 0 : 1;
}
