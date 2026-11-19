#include "nca.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "crypto.h"
#include "nsp.h"

namespace nx2a {
namespace {

constexpr std::uint64_t kHeaderSize = 0xC00;
constexpr std::uint64_t kMediaSize = 0x200;
constexpr std::uint64_t kSectionHeaderOffset = 0x400;
constexpr std::uint32_t kEmpty = 0xFFFFFFFFu;
constexpr std::size_t kMaxMeta = 64u << 20;

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t le64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(le32(p)) | (static_cast<std::uint64_t>(le32(p + 4)) << 32);
}

bool readAt(FILE* f, std::uint64_t offset, void* dst, std::size_t n) {
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) return false;
    return std::fread(dst, 1, n, f) == n;
}

std::string hexIdReversed(const std::uint8_t* p, std::size_t n) {
    static const char* digits = "0123456789ABCDEF";
    std::string s;
    s.reserve(n * 2);
    for (std::size_t i = n; i > 0; --i) {
        const std::uint8_t b = p[i - 1];
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 0xF]);
    }
    return s;
}

bool loadTitleKey(const std::string& nspPath, const NspInfo& info, const KeySet& keys,
                  int revision, std::uint8_t titleKey[16], std::string* error) {
    const NspEntry* ticket = nullptr;
    for (const NspEntry& e : info.entries) {
        if (e.kind == "ticket" && e.size >= 0x2C0) {
            ticket = &e;
            break;
        }
    }
    if (ticket == nullptr) {
        if (error) *error = "no ticket in the NSP";
        return false;
    }
    std::vector<std::uint8_t> titlekek;
    const std::string name = "titlekek_" + generationSuffix(revision);
    if (!keys.get(name, &titlekek) || titlekek.size() < 16) {
        if (error) *error = "missing " + name;
        return false;
    }
    std::uint8_t block[0x100];
    FILE* f = std::fopen(nspPath.c_str(), "rb");
    if (f == nullptr) {
        if (error) *error = "cannot open the NSP";
        return false;
    }
    const bool read = readAt(f, info.dataStart + ticket->offset + 0x180, block, sizeof(block));
    std::fclose(f);
    if (!read) {
        if (error) *error = "cannot read the ticket title-key block";
        return false;
    }
    aes128EcbDecrypt(titlekek.data(), block, sizeof(block));
    std::memcpy(titleKey, block, 16);
    std::memset(block, 0, sizeof(block));
    return true;
}

bool decryptHeader(const std::uint8_t headerKey[32], const std::uint8_t in[kHeaderSize],
                   std::uint8_t out[kHeaderSize]) {
    if (std::memcmp(in + 0x200, "NCA", 3) == 0) {
        std::memcpy(out, in, kHeaderSize);
        return true;
    }
    std::memcpy(out, in, kHeaderSize);
    for (std::uint64_t sector = 0; sector < kHeaderSize / 0x200; ++sector) {
        aes128XtsDecrypt(headerKey, sector, out + sector * 0x200, 0x200);
    }
    return std::memcmp(out + 0x200, "NCA", 3) == 0;
}

struct SectionReader {
    FILE* file = nullptr;
    std::uint64_t abs = 0;
    std::uint64_t size = 0;
    std::uint8_t key[16] = {};
    std::uint8_t ctr[8] = {};
    bool encrypted = false;
    std::uint64_t ctrBase = 0;  // section start relative to the NCA

    bool read(std::uint64_t rel, std::uint8_t* out, std::size_t n) {
        if (rel > size || n > size - rel) return false;
        if (n == 0) return true;
        const std::uint64_t aligned = rel & ~static_cast<std::uint64_t>(0xF);
        const std::size_t skip = static_cast<std::size_t>(rel - aligned);
        const std::size_t total = skip + n;
        std::vector<std::uint8_t> buf(total);
        if (!readAt(file, abs + aligned, buf.data(), total)) return false;
        if (encrypted) {
            std::uint8_t counter[16];
            for (int j = 0; j < 8; ++j) counter[j] = ctr[7 - j];
            std::uint64_t ofs = (ctrBase + aligned) >> 4;
            for (int j = 0; j < 8; ++j) {
                counter[15 - j] = static_cast<std::uint8_t>(ofs & 0xFF);
                ofs >>= 8;
            }
            aes128CtrCrypt(key, counter, buf.data(), total);
        }
        std::memcpy(out, buf.data() + skip, n);
        return true;
    }
};

std::string safeComponent(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20) {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    if (out.empty() || out == "." || out == "..") out = "_";
    return out;
}

}  // namespace

bool inspectNca(const std::string& nspPath, const KeySet& keys, NcaInfo* out) {
    *out = NcaInfo{};
    NspInfo info;
    if (!inspectNspFile(nspPath, &info) || !info.ok) {
        out->error = "cannot read the NSP";
        return false;
    }
    const NspEntry* program = nullptr;
    for (const NspEntry& e : info.entries) {
        if (e.kind == "nca" && e.size > kHeaderSize &&
            (program == nullptr || e.size > program->size)) {
            program = &e;
        }
    }
    if (program == nullptr) {
        out->error = "no program NCA";
        return false;
    }
    std::vector<std::uint8_t> headerKey;
    if (!keys.get("header_key", &headerKey) || headerKey.size() < 32) {
        out->error = "missing header_key";
        return false;
    }
    std::uint8_t rawHeader[kHeaderSize];
    std::uint8_t header[kHeaderSize];
    FILE* f = std::fopen(nspPath.c_str(), "rb");
    if (f == nullptr) {
        out->error = "cannot open the NSP";
        return false;
    }
    if (!readAt(f, info.dataStart + program->offset, rawHeader, sizeof(rawHeader))) {
        std::fclose(f);
        out->error = "cannot read the NCA header";
        return false;
    }
    if (!decryptHeader(headerKey.data(), rawHeader, header)) {
        std::fclose(f);
        out->error = "the NCA header does not decrypt";
        return false;
    }
    out->magic.assign(reinterpret_cast<const char*>(header + 0x200), 4);
    out->contentType = header[0x205];
    out->titleId = hexIdReversed(header + 0x210, 8);
    out->ncaBase = info.dataStart + program->offset;
    // LibHac: key generation = max(KeyGeneration1, KeyGeneration2); the
    // titlekek index is that generation minus one (master-key revision).
    const int generation = std::max<int>(header[0x206], header[0x220]);
    out->keyRevision = generation == 0 ? 0 : generation - 1;
    const std::uint8_t* rights = header + 0x230;
    for (int i = 0; i < 16; ++i) {
        if (rights[i] != 0) out->rightsId = true;
    }
    if (!out->rightsId) {
        std::fclose(f);
        out->error = "NCA has no rights id (key area path not implemented)";
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        const std::uint8_t* e = header + 0x240 + i * 0x10;
        const std::uint32_t start = le32(e);
        const std::uint32_t end = le32(e + 4);
        if (start == 0 || end <= start) continue;
        NcaSection& s = out->sections[i];
        s.present = true;
        s.relativeOffset = static_cast<std::uint64_t>(start) * kMediaSize;
        s.offset = out->ncaBase + s.relativeOffset;
        s.size = static_cast<std::uint64_t>(end - start) * kMediaSize;
        const std::uint8_t* fh = header + kSectionHeaderOffset + i * 0x200;
        s.partition = fh[2];
        s.fsType = fh[3];
        s.cryptType = fh[4];
        std::memcpy(s.ctr, fh + 0x140, 8);
        if (s.partition == 1 && s.fsType == 2) {
            s.pfs0Offset = le64(fh + 0x8 + 0x38);
        }
        if (s.partition == 0 && s.fsType == 3) {
            s.romfs = true;
            const std::uint8_t* ivfc = fh + 0x8;
            if (std::memcmp(ivfc, "IVFC", 4) == 0 && le32(ivfc + 12) >= 6) {
                const std::uint8_t* level5 = ivfc + 0x10 + 5 * 0x18;
                s.romfsOffset = le64(level5);
            }
        }
    }
    std::fclose(f);
    if (!out->sections[0].present && !out->sections[1].present) {
        out->error = "no NCA sections";
        return false;
    }
    out->ok = true;
    return true;
}

namespace {

struct RomfsHeader {
    std::uint64_t headerSize, dirHashOffset, dirHashSize, dirMetaOffset, dirMetaSize;
    std::uint64_t fileHashOffset, fileHashSize, fileMetaOffset, fileMetaSize, dataOffset;
};

bool readRomfsHeader(SectionReader& reader, std::uint64_t romfsOffset, RomfsHeader* h) {
    std::uint8_t b[0x50];
    if (!reader.read(romfsOffset, b, sizeof(b))) return false;
    h->headerSize = le64(b + 0x00);
    h->dirHashOffset = le64(b + 0x08);
    h->dirHashSize = le64(b + 0x10);
    h->dirMetaOffset = le64(b + 0x18);
    h->dirMetaSize = le64(b + 0x20);
    h->fileHashOffset = le64(b + 0x28);
    h->fileHashSize = le64(b + 0x30);
    h->fileMetaOffset = le64(b + 0x38);
    h->fileMetaSize = le64(b + 0x40);
    h->dataOffset = le64(b + 0x48);
    return h->headerSize == 0x50 && h->dirMetaSize <= kMaxMeta &&
           h->fileMetaSize <= kMaxMeta && h->dirMetaSize > 0 && h->fileMetaSize > 0;
}

}  // namespace

bool listRomfs(const std::string& nspPath, const KeySet& keys, RomfsListing* out) {
    *out = RomfsListing{};
    NcaInfo nca;
    if (!inspectNca(nspPath, keys, &nca)) {
        out->error = nca.error;
        return false;
    }
    int sectionIndex = -1;
    for (int i = 0; i < 4; ++i) {
        if (nca.sections[i].present && nca.sections[i].romfs &&
            nca.sections[i].romfsOffset != 0) {
            sectionIndex = i;
            break;
        }
    }
    if (sectionIndex < 0) {
        out->error = "no RomFS section with an IVFC tree";
        return false;
    }
    const NcaSection& section = nca.sections[sectionIndex];
    std::uint8_t titleKey[16];
    NspInfo info;
    std::string why;
    if (!inspectNspFile(nspPath, &info) || !info.ok ||
        !loadTitleKey(nspPath, info, keys, nca.keyRevision, titleKey, &why)) {
        out->error = why.empty() ? "cannot load the title key" : why;
        return false;
    }

    FILE* f = std::fopen(nspPath.c_str(), "rb");
    if (f == nullptr) {
        out->error = "cannot open the NSP";
        return false;
    }
    SectionReader reader;
    reader.file = f;
    reader.abs = section.offset;
    reader.ctrBase = section.relativeOffset;
    reader.size = section.size;
    reader.encrypted = section.cryptType == 3;
    if (section.cryptType != 1 && section.cryptType != 3) {
        std::fclose(f);
        out->error = "unsupported NCA section encryption";
        return false;
    }
    std::memcpy(reader.key, titleKey, 16);
    std::memcpy(reader.ctr, section.ctr, 8);
    std::memset(titleKey, 0, sizeof(titleKey));

    RomfsHeader h{};
    if (!readRomfsHeader(reader, section.romfsOffset, &h)) {
        std::fclose(f);
        out->error = "cannot read the RomFS header";
        return false;
    }

    std::vector<std::uint8_t> dirs(h.dirMetaSize);
    std::vector<std::uint8_t> files(h.fileMetaSize);
    if (!reader.read(section.romfsOffset + h.dirMetaOffset, dirs.data(), dirs.size()) ||
        !reader.read(section.romfsOffset + h.fileMetaOffset, files.data(), files.size())) {
        std::fclose(f);
        out->error = "cannot read the RomFS metadata tables";
        return false;
    }

    struct Walker {
        const std::vector<std::uint8_t>& dirs;
        const std::vector<std::uint8_t>& files;
        RomfsListing* out;
        bool ok = true;
        int depth = 0;

        void walkDir(std::uint32_t offset, const std::string& prefix) {
            if (!ok) return;
            if (depth++ > 64 || offset == kEmpty || offset + 0x18 > dirs.size()) {
                ok = false;
                return;
            }
            const std::uint8_t* e = dirs.data() + offset;
            const std::uint32_t child = le32(e + 8);
            const std::uint32_t file = le32(e + 12);
            const std::uint32_t nameSize = le32(e + 20);
            if (nameSize > 0x1000 || offset + 0x18 + nameSize > dirs.size()) {
                ok = false;
                return;
            }
            std::string name(reinterpret_cast<const char*>(e + 24), nameSize);
            const std::string here =
                (prefix.empty() || name.empty()) ? prefix : prefix + "/" + safeComponent(name);
            if (file != kEmpty) walkFile(file, here);
            if (child != kEmpty) walkDir(child, here);
            const std::uint32_t sibling = le32(e + 4);
            if (sibling != kEmpty) walkDir(sibling, prefix);
            --depth;
        }

        void walkFile(std::uint32_t offset, const std::string& dir) {
            while (offset != kEmpty) {
                if (offset + 0x20 > files.size()) {
                    ok = false;
                    return;
                }
                const std::uint8_t* e = files.data() + offset;
                const std::uint32_t sibling = le32(e + 4);
                const std::uint64_t off = le64(e + 8);
                const std::uint64_t size = le64(e + 16);
                const std::uint32_t nameSize = le32(e + 28);
                if (nameSize > 0x1000 || offset + 0x20 + nameSize > files.size()) {
                    ok = false;
                    return;
                }
                std::string name(reinterpret_cast<const char*>(e + 32), nameSize);
                if (out->files.size() >= 200000) {
                    ok = false;
                    return;
                }
                const std::string safe = safeComponent(name);
                out->files.push_back(
                    RomfsFile{dir.empty() ? safe : dir + "/" + safe, off, size});
                out->totalBytes += size;
                offset = sibling;
            }
        }
    } walker{dirs, files, out};
    walker.walkDir(0, "");
    std::fclose(f);
    if (!walker.ok) {
        out->ok = false;
        out->error = "the RomFS metadata is malformed or too large";
        out->files.clear();
        return false;
    }
    out->romfsSize = section.size;
    out->section = sectionIndex;
    out->ok = !out->files.empty();
    if (!out->ok) out->error = "RomFS tree is empty";
    return out->ok;
}

bool extractRomfs(const std::string& nspPath, const KeySet& keys,
                  const std::string& outDir,
                  const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                  const std::function<bool()>& cancelled, RomfsListing* out) {
    if (!listRomfs(nspPath, keys, out)) return false;
    NcaInfo nca;
    if (!inspectNca(nspPath, keys, &nca)) {
        out->error = nca.error;
        return false;
    }
    int sectionIndex = -1;
    for (int i = 0; i < 4; ++i) {
        if (nca.sections[i].present && nca.sections[i].romfs &&
            nca.sections[i].romfsOffset != 0) {
            sectionIndex = i;
            break;
        }
    }
    if (sectionIndex < 0) {
        out->error = "no RomFS section";
        return false;
    }
    const NcaSection& section = nca.sections[sectionIndex];
    std::uint8_t titleKey[16];
    NspInfo info;
    std::string why;
    if (!inspectNspFile(nspPath, &info) || !info.ok ||
        !loadTitleKey(nspPath, info, keys, nca.keyRevision, titleKey, &why)) {
        out->error = why.empty() ? "cannot load the title key" : why;
        return false;
    }
    FILE* f = std::fopen(nspPath.c_str(), "rb");
    if (f == nullptr) {
        std::memset(titleKey, 0, sizeof(titleKey));
        out->error = "cannot open the NSP";
        return false;
    }
    SectionReader reader;
    reader.file = f;
    reader.abs = section.offset;
    reader.ctrBase = section.relativeOffset;
    reader.size = section.size;
    reader.encrypted = section.cryptType == 3;
    std::memcpy(reader.key, titleKey, 16);
    std::memcpy(reader.ctr, section.ctr, 8);
    std::memset(titleKey, 0, sizeof(titleKey));

    RomfsHeader h{};
    if (!readRomfsHeader(reader, section.romfsOffset, &h)) {
        std::fclose(f);
        out->error = "cannot read the RomFS header";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        std::fclose(f);
        out->error = "cannot create the output directory";
        return false;
    }

    std::uint64_t done = 0;
    std::vector<std::uint8_t> buffer(4u << 20);
    for (const RomfsFile& file : out->files) {
        if (cancelled && cancelled()) {
            std::fclose(f);
            out->ok = false;
            out->error = "cancelled";
            return false;
        }
        const std::filesystem::path path =
            std::filesystem::path(outDir) / std::filesystem::path(file.path);
        std::error_code mk;
        std::filesystem::create_directories(path.parent_path(), mk);
        FILE* dst = std::fopen(path.string().c_str(), "wb");
        if (dst == nullptr) {
            std::fclose(f);
            out->ok = false;
            out->error = "cannot write " + file.path;
            return false;
        }
        const std::uint64_t base = section.romfsOffset + h.dataOffset + file.offset;
        std::uint64_t left = file.size;
        std::uint64_t at = 0;
        while (left > 0) {
            if (cancelled && cancelled()) {
                std::fclose(dst);
                std::fclose(f);
                out->ok = false;
                out->error = "cancelled";
                return false;
            }
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, buffer.size()));
            if (!reader.read(base + at, buffer.data(), n) ||
                std::fwrite(buffer.data(), 1, n, dst) != n) {
                std::fclose(dst);
                std::fclose(f);
                out->ok = false;
                out->error = "cannot extract " + file.path;
                return false;
            }
            at += n;
            left -= n;
            done += n;
            if (progress) progress(done, out->totalBytes);
        }
        std::fclose(dst);
    }
    std::fclose(f);
    out->ok = true;
    return true;
}

bool extractExefs(const std::string& nspPath, const KeySet& keys,
                  const std::string& outDir,
                  const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                  const std::function<bool()>& cancelled, ExefsListing* out) {
    *out = ExefsListing{};
    NcaInfo nca;
    if (!inspectNca(nspPath, keys, &nca)) {
        out->error = nca.error;
        return false;
    }
    std::uint8_t titleKey[16];
    NspInfo info;
    std::string why;
    if (!inspectNspFile(nspPath, &info) || !info.ok ||
        !loadTitleKey(nspPath, info, keys, nca.keyRevision, titleKey, &why)) {
        out->error = why.empty() ? "cannot load the title key" : why;
        return false;
    }
    FILE* f = std::fopen(nspPath.c_str(), "rb");
    if (f == nullptr) {
        std::memset(titleKey, 0, sizeof(titleKey));
        out->error = "cannot open the NSP";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    std::vector<std::uint8_t> buffer(4u << 20);
    std::uint64_t done = 0;
    bool any = false;

    for (int si = 0; si < 4; ++si) {
        const NcaSection& section = nca.sections[si];
        if (!section.present || section.fsType != 2 || section.cryptType == 2) continue;
        SectionReader reader;
        reader.file = f;
        reader.abs = section.offset;
        reader.ctrBase = section.relativeOffset;
        reader.size = section.size;
        reader.encrypted = section.cryptType == 3;
        std::memcpy(reader.key, titleKey, 16);
        std::memcpy(reader.ctr, section.ctr, 8);

        // pfs0_offset is relative to the section (pfs0_superblock.pfs0_offset).
        const std::uint64_t pfs0Offset = section.pfs0Offset;
        if (pfs0Offset >= section.size) continue;
        std::uint8_t h[0x10];
        if (!reader.read(pfs0Offset, h, sizeof(h)) || std::memcmp(h, "PFS0", 4) != 0) continue;
        const std::uint32_t count = le32(h + 4);
        const std::uint32_t strSize = le32(h + 8);
        if (count == 0 || count > 4096 || strSize > (1u << 20)) continue;
        const std::uint64_t tableSize = 0x10ull + static_cast<std::uint64_t>(count) * 0x18;
        const std::uint64_t dataStart = tableSize + strSize;
        if (pfs0Offset + dataStart > section.size) continue;
        std::vector<std::uint8_t> table(tableSize + strSize);
        if (!reader.read(pfs0Offset, table.data(), table.size())) continue;
        const std::uint8_t* strings = table.data() + tableSize;
        any = true;

        for (std::uint32_t i = 0; i < count; ++i) {
            if (cancelled && cancelled()) {
                std::fclose(f);
                std::memset(titleKey, 0, sizeof(titleKey));
                out->ok = false;
                out->error = "cancelled";
                return false;
            }
            const std::uint8_t* e = table.data() + 0x10 + i * 0x18;
            const std::uint64_t offset = le64(e);
            const std::uint64_t size = le64(e + 8);
            const std::uint32_t nameOffset = le32(e + 16);
            if (nameOffset >= strSize || offset > section.size ||
                size > section.size - offset) {
                continue;
            }
            std::string name(reinterpret_cast<const char*>(strings + nameOffset));
            name = safeComponent(name.substr(name.find_last_of("/\\") + 1));
            const std::string rel = "s" + std::to_string(si) + "/" + name;
            const std::filesystem::path path =
                std::filesystem::path(outDir) / std::filesystem::path(rel);
            std::error_code mk;
            std::filesystem::create_directories(path.parent_path(), mk);
            FILE* dst = std::fopen(path.string().c_str(), "wb");
            if (dst == nullptr) continue;
            std::uint64_t at = 0;
            std::uint64_t left = size;
            while (left > 0) {
                const std::size_t n =
                    static_cast<std::size_t>(std::min<std::uint64_t>(left, buffer.size()));
                if (!reader.read(pfs0Offset + dataStart + offset + at, buffer.data(), n) ||
                    std::fwrite(buffer.data(), 1, n, dst) != n) {
                    std::fclose(dst);
                    std::fclose(f);
                    std::memset(titleKey, 0, sizeof(titleKey));
                    out->ok = false;
                    out->error = "cannot extract " + rel;
                    return false;
                }
                at += n;
                left -= n;
                done += n;
                if (progress) progress(done, 0);
            }
            std::fclose(dst);
            out->files.push_back(RomfsFile{rel, offset, size});
            out->totalBytes += size;
        }
    }
    std::fclose(f);
    std::memset(titleKey, 0, sizeof(titleKey));
    out->ok = any;
    if (!any) out->error = "no PFS0 (ExeFS) section";
    return out->ok;
}

}  // namespace nx2a