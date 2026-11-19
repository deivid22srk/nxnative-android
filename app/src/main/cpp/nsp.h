// NSP (PFS0) container inspection: entries, ticket rights ID, title ID and
// key generation. Reads a few kilobytes; never decrypts anything.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nx2a {

struct ByteSource {
    virtual ~ByteSource() = default;
    virtual bool read(void* dst, std::size_t n, std::uint64_t offset) = 0;
    virtual std::uint64_t size() = 0;
};

struct NspEntry {
    std::string name;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::string kind;  // program | control | meta | manual | data | ticket | cert | nca | other
};

struct NspInfo {
    bool ok = false;
    std::string code;       // ERR_* token, for localized UI text
    std::string error;      // English fallback detail (no secrets)
    std::string magic;      // "PFS0"
    std::uint64_t fileSize = 0;
    // Absolute offset of the first entry payload. NspEntry::offset is relative
    // to this position, as stored in the PFS0 table.
    std::uint64_t dataStart = 0;
    std::vector<NspEntry> entries;
    bool hasTicket = false;
    std::string ticketName;
    std::string titleId;    // 16 uppercase hex chars
    int keyGeneration = -1;
};

bool inspectNsp(ByteSource& source, NspInfo* out);
bool inspectNspFile(const std::string& path, NspInfo* out);
bool inspectNspFd(int fd, NspInfo* out);

}  // namespace nx2a
