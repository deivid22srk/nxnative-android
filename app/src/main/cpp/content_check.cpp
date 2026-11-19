#include "content_check.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#include "crypto.h"
#include "nsp.h"

namespace nx2a {
namespace {

constexpr std::size_t kTicketSize = 0x2C0;
constexpr std::size_t kHeaderSize = 0x400;
constexpr std::size_t kTicketKeyBlock = 0x180;

std::string escape(const std::string& in) {
    std::string out;
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
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

bool readAt(FILE* f, std::uint64_t offset, void* dst, std::size_t n) {
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) return false;
    return std::fread(dst, 1, n, f) == n;
}

bool allZero(const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (p[i] != 0) return false;
    }
    return true;
}

bool findBytes(const std::uint8_t* hay, std::size_t hayLen, const std::uint8_t* needle,
               std::size_t needleLen, std::size_t* at) {
    if (needleLen == 0 || needleLen > hayLen) return false;
    for (std::size_t i = 0; i + needleLen <= hayLen; ++i) {
        if (std::memcmp(hay + i, needle, needleLen) == 0) {
            if (at) *at = i;
            return true;
        }
    }
    return false;
}

void fail(ContentCheck* out, const char* code, const std::string& detail) {
    out->ok = false;
    out->code = code;
    out->error = detail;
}

}  // namespace

ContentCheck checkContentKeys(const std::string& nspPath, const KeySet& keys) {
    ContentCheck out;
    NspInfo info;
    if (!inspectNspFile(nspPath, &info) || !info.ok) {
        fail(&out, "ERR_NSP", "cannot read the NSP container");
        return out;
    }

    const int gen = info.keyGeneration;
    const std::string genSuffix = generationSuffix(gen);

    const NspEntry* ticket = nullptr;
    const NspEntry* program = nullptr;
    for (const NspEntry& e : info.entries) {
        if (e.kind == "ticket" && e.size >= kTicketSize && ticket == nullptr) ticket = &e;
        if (e.kind == "nca" && e.size > kHeaderSize &&
            (program == nullptr || e.size > program->size)) {
            program = &e;
        }
    }
    if (ticket == nullptr) {
        fail(&out, "ERR_TICKET", "the NSP has no readable ticket");
        return out;
    }
    if (program == nullptr) {
        fail(&out, "ERR_NCA", "the NSP has no NCA larger than a header");
        return out;
    }

    FILE* f = std::fopen(nspPath.c_str(), "rb");
    if (f == nullptr) {
        fail(&out, "ERR_IO", "cannot open the container");
        return out;
    }

    // ---- ticket title key: AES-128-ECB with titlekek_<gen> ----
    std::vector<std::uint8_t> ticketBytes(kTicketSize);
    if (!readAt(f, info.dataStart + ticket->offset, ticketBytes.data(), kTicketSize)) {
        std::fclose(f);
        fail(&out, "ERR_IO", "cannot read the ticket");
        return out;
    }
    out.titleId = hexId(ticketBytes.data() + 0x2A0, 8);

    std::vector<std::uint8_t> titlekek;
    const std::string titlekekName = "titlekek_" + genSuffix;
    if (!keys.get(titlekekName, &titlekek) || titlekek.size() < 16) {
        std::fclose(f);
        fail(&out, "ERR_KEY_TITLEKEK", "missing " + titlekekName);
        return out;
    }
    out.titleKeyName = titlekekName;

    std::vector<std::uint8_t> titleKey(ticketBytes.begin() + kTicketKeyBlock,
                                       ticketBytes.begin() + kTicketKeyBlock + 0x100);
    const std::vector<std::uint8_t> cipherFirst(titleKey.begin(), titleKey.begin() + 16);
    aes128EcbDecrypt(titlekek.data(), titleKey.data(), titleKey.size());
    out.titleKeyOk = !allZero(titleKey.data(), 16) &&
                     std::memcmp(titleKey.data(), cipherFirst.data(), 16) != 0;
    std::memset(titleKey.data(), 0, titleKey.size());  // drop the key material

    // ---- NCA header: AES-128-XTS with header_key (two 16-byte keys) ----
    std::vector<std::uint8_t> headerKey;
    const std::string headerKeyName = "header_key";
    if (!keys.get(headerKeyName, &headerKey) || headerKey.size() < 32) {
        std::fclose(f);
        fail(&out, "ERR_KEY_HEADER", "missing header_key");
        return out;
    }
    out.headerKeyName = headerKeyName;

    std::uint8_t header[kHeaderSize];
    if (!readAt(f, info.dataStart + program->offset, header, kHeaderSize)) {
        std::fclose(f);
        fail(&out, "ERR_IO", "cannot read the NCA header");
        return out;
    }
    std::fclose(f);

    std::uint8_t work[kHeaderSize];
    const bool plaintext = std::memcmp(header + 0x200, "NCA", 3) == 0;
    if (plaintext) {
        std::memcpy(work, header, kHeaderSize);
        out.headerPlaintext = true;
    } else {
        // Sector 0 uses data unit 0 and sector 1 uses unit 1; a couple of
        // other conventions are tried as a safety net.
        struct Attempt { std::uint64_t first; std::uint64_t second; };
        const Attempt attempts[] = {{0, 1}, {1, 2}, {0, 0}};
        bool decrypted = false;
        for (const Attempt& a : attempts) {
            std::uint8_t sector0[0x200];
            std::uint8_t sector1[0x200];
            std::memcpy(sector0, header, 0x200);
            std::memcpy(sector1, header + 0x200, 0x200);
            aes128XtsDecrypt(headerKey.data(), a.first, sector0, 0x200);
            aes128XtsDecrypt(headerKey.data(), a.second, sector1, 0x200);
            if (std::memcmp(sector1, "NCA", 3) == 0) {
                std::memcpy(work, sector0, 0x200);
                std::memcpy(work + 0x200, sector1, 0x200);
                decrypted = true;
                break;
            }
        }
        if (!decrypted) {
            fail(&out, "ERR_NCA", "the NCA header does not decrypt with header_key");
            return out;
        }
    }

    out.headerOk = true;
    out.headerMagic.assign(reinterpret_cast<const char*>(work + 0x200), 4);
    out.contentType = work[0x205];

    std::size_t at = 0;
    if (findBytes(work + 0x200, kHeaderSize - 0x200, ticketBytes.data() + 0x2A0, 8, &at)) {
        out.titleIdInHeader = true;
    }

    out.ok = out.titleKeyOk && out.headerOk;
    out.code = out.ok ? "OK" : "ERR_CONTENT";
    if (!out.ok) out.error = "content keys did not verify";
    return out;
}

std::string contentCheckJson(const ContentCheck& check) {
    std::ostringstream j;
    j << '{';
    j << "\"ok\":" << (check.ok ? "true" : "false") << ',';
    j << "\"code\":\"" << escape(check.code) << "\",";
    j << "\"error\":\"" << escape(check.error) << "\",";
    j << "\"titleKeyOk\":" << (check.titleKeyOk ? "true" : "false") << ',';
    j << "\"titleKey\":\"" << escape(check.titleKeyName) << "\",";
    j << "\"headerOk\":" << (check.headerOk ? "true" : "false") << ',';
    j << "\"headerKey\":\"" << escape(check.headerKeyName) << "\",";
    j << "\"headerPlaintext\":" << (check.headerPlaintext ? "true" : "false") << ',';
    j << "\"headerMagic\":\"" << escape(check.headerMagic) << "\",";
    j << "\"contentType\":" << check.contentType << ',';
    j << "\"titleId\":\"" << escape(check.titleId) << "\",";
    j << "\"titleIdInHeader\":" << (check.titleIdInHeader ? "true" : "false");
    j << '}';
    return j.str();
}

}  // namespace nx2a