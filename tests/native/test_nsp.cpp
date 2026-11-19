// Host unit test for the platform-neutral core (keys + NSP + import check).
// Compiled in CI with the system C++ compiler; no Android dependencies.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "import_check.h"
#include "keys.h"
#include "nsp.h"

namespace {

struct MemorySource : nx2a::ByteSource {
    std::vector<std::uint8_t> bytes;
    bool read(void* dst, std::size_t n, std::uint64_t offset) override {
        if (offset > bytes.size() || n > bytes.size() - offset) return false;
        std::memcpy(dst, bytes.data() + offset, n);
        return true;
    }
    std::uint64_t size() override { return bytes.size(); }
};

void put32(std::vector<std::uint8_t>* v, std::size_t at, std::uint32_t value) {
    (*v)[at + 0] = static_cast<std::uint8_t>(value);
    (*v)[at + 1] = static_cast<std::uint8_t>(value >> 8);
    (*v)[at + 2] = static_cast<std::uint8_t>(value >> 16);
    (*v)[at + 3] = static_cast<std::uint8_t>(value >> 24);
}

void put64(std::vector<std::uint8_t>* v, std::size_t at, std::uint64_t value) {
    put32(v, at, static_cast<std::uint32_t>(value));
    put32(v, at + 4, static_cast<std::uint32_t>(value >> 32));
}

std::string buildNsp() {
    const std::string ticketName = "0100aaaabbbb0000000000000000000a.tik";
    const std::string ncaName = "feedfacecafebeef.nca";
    std::string strings = ticketName + '\0' + ncaName + '\0';
    const std::uint32_t count = 2;
    const std::uint32_t tableSize = count * 0x18;
    std::uint32_t dataStart = 0x10 + tableSize + static_cast<std::uint32_t>(strings.size());

    const std::uint32_t ticketSize = 0x2C0;
    const std::uint32_t ncaSize = 0x1000;

    std::vector<std::uint8_t> v(dataStart + ticketSize + ncaSize, 0);
    std::memcpy(v.data(), "PFS0", 4);
    put32(&v, 4, count);
    put32(&v, 8, static_cast<std::uint32_t>(strings.size()));
    put32(&v, 12, 0);

    std::size_t e = 0x10;
    put64(&v, e, 0);  // ticket offset within data
    put64(&v, e + 8, ticketSize);
    put32(&v, e + 16, 0);  // name offset
    e += 0x18;
    put64(&v, e, ticketSize);
    put64(&v, e + 8, ncaSize);
    put32(&v, e + 16, static_cast<std::uint32_t>(ticketName.size() + 1));

    std::memcpy(v.data() + 0x10 + tableSize, strings.data(), strings.size());

    std::size_t t = dataStart;
    const std::uint8_t rightsId[16] = {0x01, 0x00, 0xAA, 0xAA, 0xBB, 0xBB, 0x00, 0x00,
                                       0,    0,    0,    0,    0x0A, 0,    0,    0};
    // rights ID layout: title id (8), 4 zeroes, key generation (u32 LE)
    std::memcpy(v.data() + t + 0x2A0, rightsId, 8);
    put32(&v, t + 0x2AC, 0x0A);
    return std::string(reinterpret_cast<const char*>(v.data()), v.size());
}

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok: %s\n", what);
    }
}

}  // namespace

int main() {
    // 1. A well-formed NSP: title id and key generation come from the ticket.
    const std::string raw = buildNsp();
    MemorySource source;
    source.bytes.assign(raw.begin(), raw.end());

    nx2a::NspInfo info;
    check(nx2a::inspectNsp(source, &info), "inspect synthetic NSP");
    check(info.ok, "synthetic NSP is ok");
    check(info.titleId == "0100AAAABBBB0000", "title id parsed from rights id");
    check(info.keyGeneration == 0x0A, "key generation parsed from rights id");
    check(info.entries.size() == 2, "two entries");
    check(info.hasTicket, "ticket found");

    // 2. key parsing: values are kept, malformed lines are rejected without
    //    echoing the value.
    nx2a::KeySet keys;
    std::string error;
    const std::string keyText =
        "header_key = 00112233445566778899aabbccddeeff\n"
        "master_key_0a = 0123456789abcdef0123456789abcdef\n"
        "key_area_key_application_0a = 0123456789abcdef0123456789abcdef\n";
    check(keys.loadText(keyText, &error), "parse valid keys text");
    check(keys.size() == 3, "three keys loaded");
    check(keys.maxGeneration("master_key") == 0x0A, "max generation is 0x0A");

    const std::vector<std::string> missing = keys.missingForGeneration(0x0A);
    check(missing.size() == 1 && missing[0] == "titlekek_0a",
          "missing list names only titlekek_0a");

    const std::string json = nx2a::analyzeImport(info, keys);
    check(json.find("\"titleId\":\"0100AAAABBBB0000\"") != std::string::npos,
          "JSON carries the title id");
    check(json.find("\"keyGeneration\":10") != std::string::npos,
          "JSON carries the key generation");
    check(json.find("\"supported\":false") != std::string::npos,
          "JSON marks the NSP unsupported while a key is missing");
    check(json.find("\"titlekek_0a\"") != std::string::npos,
          "JSON lists the missing key by name");
    check(json.find("0123456789abcdef") == std::string::npos,
          "JSON never contains a key value");
    check(json.find("0011223344556677") == std::string::npos,
          "JSON never contains header_key value");

    nx2a::KeySet complete = keys;
    std::string ignored;
    check(complete.loadText("titlekek_0a = 0123456789abcdef0123456789abcdef\n", &ignored),
          "add the missing key");
    check(nx2a::analyzeImport(info, complete).find("\"supported\":true") != std::string::npos,
          "complete keys mark the NSP supported");

    // 3. Malformed keys: line number, never the value.
    nx2a::KeySet broken;
    std::string why;
    const bool parsed = broken.loadText("header_key = zzzz\n", &why);
    check(!parsed, "malformed hex is rejected");
    check(why.find("linha 1") != std::string::npos, "error names the line");
    check(why.find("zzzz") == std::string::npos, "error does not echo the value");

    // 4. A cartridge dump is rejected with a specific code.
    MemorySource xci;
    xci.bytes.assign(0x100, 0);
    std::memcpy(xci.bytes.data(), "HEAD", 4);
    nx2a::NspInfo head;
    check(!nx2a::inspectNsp(xci, &head), "XCI magic rejected");
    check(head.code == "ERR_XCI", "XCI error code");

    // 5. Truncated containers do not read out of bounds.
    MemorySource truncated;
    truncated.bytes.assign(0x20, 0);
    std::memcpy(truncated.bytes.data(), "PFS0", 4);
    put32(&truncated.bytes, 4, 100);
    put32(&truncated.bytes, 8, 100);
    nx2a::NspInfo bad;
    check(!nx2a::inspectNsp(truncated, &bad), "truncated table rejected");
    check(bad.code == "ERR_TRUNCATED", "truncated error code");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all native core checks passed\n");
    return 0;
}
