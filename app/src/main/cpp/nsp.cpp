#include "nsp.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <memory>

namespace nx2a {
namespace {

constexpr std::uint64_t kMaxStringTable = 1u << 20;
constexpr std::uint64_t kTicketSize = 0x2C0;

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t le64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(le32(p)) | (static_cast<std::uint64_t>(le32(p + 4)) << 32);
}

std::string hexId(const std::uint8_t* p, std::size_t n) {
    static const char* digits = "0123456789ABCDEF";
    std::string s;
    s.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(digits[p[i] >> 4]);
        s.push_back(digits[p[i] & 0xF]);
    }
    return s;
}

std::string kindOf(const std::string& name) {
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".tik") == 0) return "ticket";
    if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".cert") == 0) return "cert";
    if (name.size() >= 9 && name.compare(name.size() - 9, 9, ".cnmt.nca") == 0) return "meta";
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".nca") == 0) return "nca";
    return "other";
}

void fail(NspInfo* out, const char* code, const std::string& detail) {
    out->ok = false;
    out->code = code;
    out->error = detail;
}

}  // namespace

bool inspectNsp(ByteSource& src, NspInfo* out) {
    *out = NspInfo{};
    out->fileSize = src.size();
    if (out->fileSize < 0x10) {
        fail(out, "ERR_TRUNCATED", "file too small for a PFS0 header");
        return false;
    }

    std::uint8_t header[0x10];
    if (!src.read(header, sizeof(header), 0)) {
        fail(out, "ERR_IO", "cannot read the container header");
        return false;
    }
    out->magic.assign(reinterpret_cast<const char*>(header), 4);

    if (std::memcmp(header, "HEAD", 4) == 0) {
        fail(out, "ERR_XCI", "XCI cartridges are not supported; use an .nsp dump");
        return false;
    }
    if (std::memcmp(header, "PFS0", 4) != 0) {
        fail(out, "ERR_MAGIC", "not a PFS0 container (expected .nsp)");
        return false;
    }

    const std::uint32_t count = le32(header + 4);
    const std::uint32_t stringTableSize = le32(header + 8);
    if (count == 0 || count > 4096) {
        fail(out, "ERR_ENTRIES", "implausible NSP entry count");
        return false;
    }
    if (stringTableSize > kMaxStringTable) {
        fail(out, "ERR_ENTRIES", "implausible NSP string table size");
        return false;
    }

    const std::uint64_t tableBytes = static_cast<std::uint64_t>(count) * 0x18;
    const std::uint64_t stringTableOffset = 0x10 + tableBytes;
    const std::uint64_t dataStart = stringTableOffset + stringTableSize;
    out->dataStart = dataStart;
    if (dataStart > out->fileSize) {
        fail(out, "ERR_TRUNCATED", "NSP table extends past the end of the file");
        return false;
    }

    std::vector<std::uint8_t> table(static_cast<std::size_t>(tableBytes));
    if (!src.read(table.data(), table.size(), 0x10)) {
        fail(out, "ERR_IO", "cannot read the NSP entry table");
        return false;
    }
    std::vector<char> strings(static_cast<std::size_t>(stringTableSize) + 1, '\0');
    if (stringTableSize && !src.read(strings.data(), stringTableSize, stringTableOffset)) {
        fail(out, "ERR_IO", "cannot read the NSP string table");
        return false;
    }

    out->entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* e = table.data() + static_cast<std::size_t>(i) * 0x18;
        NspEntry entry;
        entry.offset = le64(e);
        entry.size = le64(e + 8);
        const std::uint32_t nameOffset = le32(e + 16);
        if (nameOffset >= stringTableSize) {
            fail(out, "ERR_ENTRIES", "entry name outside the string table");
            return false;
        }
        entry.name = std::string(strings.data() + nameOffset);
        entry.kind = kindOf(entry.name);
        if (entry.size > out->fileSize || entry.offset > out->fileSize - entry.size ||
            dataStart + entry.offset > out->fileSize) {
            fail(out, "ERR_TRUNCATED", "entry '" + entry.name + "' extends past the end of the file");
            return false;
        }
        out->entries.push_back(std::move(entry));
    }

    // The ticket carries the rights ID: title id + key generation. It needs
    // no key to be read.
    for (const NspEntry& entry : out->entries) {
        if (entry.kind != "ticket") continue;
        if (entry.size < kTicketSize) continue;
        std::uint8_t ticket[kTicketSize];
        if (!src.read(ticket, sizeof(ticket), dataStart + entry.offset)) continue;
        const std::uint8_t* rightsId = ticket + 0x2A0;
        out->hasTicket = true;
        out->ticketName = entry.name;
        out->titleId = hexId(rightsId, 8);
        const std::uint32_t generation = le32(rightsId + 12);
        if (generation <= 0x40) {
            out->keyGeneration = static_cast<int>(generation);
        }
        break;
    }

    // A ticket named after the title id is also a fallback when the ticket
    // body is unusual: 16 hex chars, 8 more, then the key generation.
    if (out->titleId.empty() || out->keyGeneration < 0) {
        for (const NspEntry& entry : out->entries) {
            if (entry.kind != "ticket" || entry.name.size() < 32) continue;
            const std::string stem = entry.name.substr(0, 32);
            bool hex = true;
            for (char c : stem) {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) {
                    hex = false;
                    break;
                }
            }
            if (!hex) continue;
            if (out->titleId.empty()) {
                out->titleId = stem.substr(0, 16);
                for (char& c : out->titleId) {
                    if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
                }
            }
            if (out->keyGeneration < 0) {
                int value = 0;
                for (std::size_t i = 24; i < 32; ++i) {
                    const char c = stem[i];
                    const int digit = (c >= '0' && c <= '9') ? c - '0'
                                    : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                                                             : c - 'A' + 10;
                    value = (value << 4) | digit;
                }
                if (value <= 0x40) out->keyGeneration = value;
            }
            break;
        }
    }

    if (!out->hasTicket) {
        fail(out, "ERR_TICKET", "NSP has no ticket: not a game dump, or a malformed container");
        return false;
    }
    if (out->titleId.empty() || out->keyGeneration < 0) {
        fail(out, "ERR_TICKET", "cannot read the title id / key generation from the ticket");
        return false;
    }

    out->ok = true;
    out->code = "OK";
    return true;
}

namespace {

struct FileSource : ByteSource {
    FILE* f = nullptr;
    std::uint64_t length = 0;
    bool read(void* dst, std::size_t n, std::uint64_t offset) override {
        if (offset > length || n > length - offset) return false;
        if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) return false;
        return std::fread(dst, 1, n, f) == n;
    }
    std::uint64_t size() override { return length; }
};

struct FdSource : ByteSource {
    int fd = -1;
    std::uint64_t length = 0;
    bool read(void* dst, std::size_t n, std::uint64_t offset) override {
        if (offset > length || n > length - offset) return false;
        std::uint8_t* p = static_cast<std::uint8_t*>(dst);
        std::size_t done = 0;
        while (done < n) {
            const ssize_t r = ::pread(fd, p + done, n - done,
                                      static_cast<off_t>(offset + done));
            if (r <= 0) return false;
            done += static_cast<std::size_t>(r);
        }
        return true;
    }
    std::uint64_t size() override { return length; }
};

}  // namespace

bool inspectNspFile(const std::string& path, NspInfo* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        *out = NspInfo{};
        fail(out, "ERR_IO", "cannot open the container");
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long long end = std::ftell(f);
    FileSource src;
    src.f = f;
    src.length = end > 0 ? static_cast<std::uint64_t>(end) : 0;
    const bool ok = inspectNsp(src, out);
    std::fclose(f);
    return ok;
}

bool inspectNspFd(int fd, NspInfo* out) {
    struct stat st {};
    FdSource src;
    src.fd = fd;
    if (fstat(fd, &st) != 0 || st.st_size < 0) {
        *out = NspInfo{};
        fail(out, "ERR_IO", "cannot stat the selected file (is it seekable?)");
        return false;
    }
    src.length = static_cast<std::uint64_t>(st.st_size);
    return inspectNsp(src, out);
}

}  // namespace nx2a
