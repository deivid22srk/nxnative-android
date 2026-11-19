// NCA section decryption and RomFS reading. This is the first half of M3:
// the app can now open the program NCA, decrypt its CTR sections with the
// ticket title key and walk the RomFS tree. No key value leaves this module.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "keys.h"

namespace nx2a {

struct NcaSection {
    bool present = false;
    std::uint64_t offset = 0;   // absolute in the NSP
    std::uint64_t relativeOffset = 0;  // relative to the NCA (CTR offset)
    std::uint64_t size = 0;
    int partition = -1;
    int fsType = -1;
    int cryptType = -1;
    std::uint8_t ctr[8] = {};
    bool romfs = false;
    std::uint64_t romfsOffset = 0;
    std::uint64_t pfs0Offset = 0;
};

struct NcaInfo {
    bool ok = false;
    std::string error;
    std::string magic;
    int contentType = -1;
    std::string titleId;
    int keyRevision = -1;       // master-key revision: header generation - 1
    std::uint64_t ncaBase = 0;  // absolute NSP offset of this NCA
    std::array<NcaSection, 4> sections;
    bool rightsId = false;
};

struct RomfsFile {
    std::string path;
    std::uint64_t offset = 0;  // relative to the RomFS data area
    std::uint64_t size = 0;
};

struct RomfsListing {
    bool ok = false;
    std::string error;
    std::vector<RomfsFile> files;
    std::uint64_t totalBytes = 0;
    std::uint64_t romfsSize = 0;
    int section = -1;
};

// Opens the largest NCA (the program) and reads its encrypted header.
bool inspectNca(const std::string& nspPath, const KeySet& keys, NcaInfo* out);

// Walks the RomFS metadata of the program NCA. Reads the tree but no file data.
bool listRomfs(const std::string& nspPath, const KeySet& keys, RomfsListing* out);

// Extracts every RomFS file. `progress(done,total)` runs after each chunk;
// `cancelled` is polled between files and chunks. Paths are sanitised.
bool extractRomfs(const std::string& nspPath, const KeySet& keys,
                  const std::string& outDir,
                  const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                  const std::function<bool()>& cancelled, RomfsListing* out);

struct ExefsListing {
    bool ok = false;
    std::string error;
    std::vector<RomfsFile> files;
    std::uint64_t totalBytes = 0;
};

// Extracts the PFS0 (ExeFS) sections, one subdirectory per section.
bool extractExefs(const std::string& nspPath, const KeySet& keys,
                  const std::string& outDir,
                  const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                  const std::function<bool()>& cancelled, ExefsListing* out);

}  // namespace nx2a