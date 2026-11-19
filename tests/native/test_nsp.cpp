// Host unit test for the platform-neutral core (keys + NSP + content keys +
// import check). Compiled in CI with the system C++ compiler; no Android
// dependencies and no real game files.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

#include "content_check.h"
#include "crypto.h"
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

// Test key material is synthetic and public: it is not a console key.
const std::uint8_t kHeaderKey[32] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
    0xcc, 0xdd, 0xee, 0xff, 0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78,
    0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0};
const std::uint8_t kTitleKek[16] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0xef, 0xcd, 0xab, 0x89,
    0x67, 0x45, 0x23, 0x01};
const std::uint8_t kTitleKey[16] = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe, 0x01, 0x23, 0x45, 0x67,
    0x89, 0xab, 0xcd, 0xef};

std::string buildNsp(bool encrypted = true) {
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
    const std::uint8_t rightsId[8] = {0x01, 0x00, 0xAA, 0xAA, 0xBB, 0xBB, 0x00, 0x00};
    std::memcpy(v.data() + t + 0x2A0, rightsId, 8);
    put32(&v, t + 0x2AC, 0x0A);

    if (encrypted) {
        // Title key block (0x100 bytes) encrypted with titlekek_0a.
        std::uint8_t block[0x100] = {};
        std::memcpy(block, kTitleKey, 16);
        nx2a::aes128EcbEncrypt(kTitleKek, block, sizeof(block));
        std::memcpy(v.data() + t + 0x180, block, sizeof(block));

        // NCA header: plaintext then XTS sectors 0 and 1.
        std::uint8_t header[0x400] = {};
        std::memcpy(header + 0x200, "NCA3", 4);
        header[0x205] = 0;  // Program
        std::memcpy(header + 0x210, rightsId, 8);
        nx2a::aes128XtsEncrypt(kHeaderKey, 0, header, 0x200);
        nx2a::aes128XtsEncrypt(kHeaderKey, 1, header + 0x200, 0x200);
        std::memcpy(v.data() + dataStart + ticketSize, header, sizeof(header));
    }
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

std::string writeTemp(const std::string& bytes) {
    char path[] = "/tmp/nx2a-test-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) return {};
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t n = write(fd, bytes.data() + done, bytes.size() - done);
        if (n <= 0) break;
        done += static_cast<std::size_t>(n);
    }
    close(fd);
    return path;
}

void testCrypto() {
    // FIPS-197 appendix C.1.
    const std::uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    const std::uint8_t plain[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const std::uint8_t expected[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                       0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    std::uint8_t out[16];
    nx2a::aes128EncryptBlock(key, plain, out);
    check(std::memcmp(out, expected, 16) == 0, "AES-128 FIPS-197 vector");
    std::uint8_t back[16];
    nx2a::aes128DecryptBlock(key, expected, back);
    check(std::memcmp(back, plain, 16) == 0, "AES-128 decrypt round-trip");

    // ECB round-trip over several blocks.
    std::uint8_t ecb[64];
    for (int i = 0; i < 64; ++i) ecb[i] = static_cast<std::uint8_t>(i * 3 + 1);
    std::uint8_t ecbCipher[64];
    std::memcpy(ecbCipher, ecb, 64);
    nx2a::aes128EcbEncrypt(key, ecbCipher, 64);
    check(std::memcmp(ecbCipher, ecb, 64) != 0, "ECB changes the data");
    nx2a::aes128EcbDecrypt(key, ecbCipher, 64);
    check(std::memcmp(ecbCipher, ecb, 64) == 0, "ECB round-trip");

    // CTR round-trip.
    std::uint8_t ctr[64];
    std::memcpy(ctr, ecb, 64);
    const std::uint8_t counter[16] = {0};
    nx2a::aes128CtrCrypt(key, counter, ctr, 64);
    check(std::memcmp(ctr, ecb, 64) != 0, "CTR changes the data");
    nx2a::aes128CtrCrypt(key, counter, ctr, 64);
    check(std::memcmp(ctr, ecb, 64) == 0, "CTR round-trip");

    // XTS round-trip, including a non-zero data unit.
    std::uint8_t xts[0x400];
    for (int i = 0; i < 0x400; ++i) xts[i] = static_cast<std::uint8_t>(i * 7 + 3);
    std::uint8_t xtsCopy[0x400];
    std::memcpy(xtsCopy, xts, sizeof(xts));
    for (std::uint64_t unit = 0; unit < 2; ++unit) {
        nx2a::aes128XtsEncrypt(kHeaderKey, unit, xts + unit * 0x200, 0x200);
    }
    check(std::memcmp(xts, xtsCopy, sizeof(xts)) != 0, "XTS changes the data");
    for (std::uint64_t unit = 0; unit < 2; ++unit) {
        nx2a::aes128XtsDecrypt(kHeaderKey, unit, xts + unit * 0x200, 0x200);
    }
    check(std::memcmp(xts, xtsCopy, sizeof(xts)) == 0, "XTS round-trip");
}

}  // namespace

int main() {
    testCrypto();

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
    check(info.dataStart > 0, "data start recorded");

    // 2. key parsing: values are kept, malformed lines are rejected without
    //    echoing the value.
    nx2a::KeySet keys;
    std::string error;
    const std::string keyText =
        "header_key = 00112233445566778899aabbccddeeff0f1e2d3c4b5a69788796a5b4c3d2e1f0\n"
        "master_key_0a = 0123456789abcdef0123456789abcdef\n"
        "key_area_key_application_0a = 0123456789abcdef0123456789abcdef\n"
        "titlekek_0a = 1032547698badcfeefcdab8967452301\n";
    check(keys.loadText(keyText, &error), "parse valid keys text");
    check(keys.size() == 4, "four keys loaded");
    check(keys.maxGeneration("master_key") == 0x0A, "max generation is 0x0A");

    nx2a::KeySet withoutTitleKek;
    check(withoutTitleKek.loadText(
              "header_key = 00112233445566778899aabbccddeeff0f1e2d3c4b5a69788796a5b4c3d2e1f0\n"
              "master_key_0a = 0123456789abcdef0123456789abcdef\n"
              "key_area_key_application_0a = 0123456789abcdef0123456789abcdef\n",
              &error),
          "parse keys without titlekek");
    const std::vector<std::string> missing = withoutTitleKek.missingForGeneration(0x0A);
    check(missing.size() == 1 && missing[0] == "titlekek_0a",
          "missing list names only titlekek_0a");

    const std::string json = nx2a::analyzeImport(info, withoutTitleKek);
    check(json.find("\"titleId\":\"0100AAAABBBB0000\"") != std::string::npos,
          "JSON carries the title id");
    check(json.find("\"keyGeneration\":10") != std::string::npos,
          "JSON carries the key generation");
    check(json.find("\"supported\":false") != std::string::npos,
          "JSON marks the NSP unsupported while a key is missing");
    check(json.find("\"titlekek_0a\"") != std::string::npos,
          "JSON lists the missing key by name");
    check(json.find("00112233445566778899aabbccddeeff") == std::string::npos,
          "JSON never contains header_key value");
    check(json.find("0123456789abcdef") == std::string::npos,
          "JSON never contains a key value");

    check(nx2a::analyzeImport(info, keys).find("\"supported\":true") != std::string::npos,
          "complete keys mark the NSP supported");

    // 3. Content keys: ticket title key + NCA header.
    const std::string path = writeTemp(raw);
    check(!path.empty(), "temporary NSP written");
    if (!path.empty()) {
        const nx2a::ContentCheck good = nx2a::checkContentKeys(path, keys);
        check(good.ok, "content keys verify on the synthetic NSP");
        check(good.titleKeyOk, "ticket title key decrypts");
        check(good.headerOk, "NCA header decrypts");
        check(good.headerMagic == "NCA3", "NCA magic is NCA3");
        check(good.titleIdInHeader, "title id found in the header");
        const std::string cj = nx2a::contentCheckJson(good);
        check(cj.find("\"ok\":true") != std::string::npos, "content JSON reports ok");
        check(cj.find("deadbeef") == std::string::npos, "content JSON has no title key");
        check(cj.find("00112233445566778899aabbccddeeff") == std::string::npos,
              "content JSON has no header_key");
        unlink(path.c_str());
    }

    // A plaintext header is accepted (some dumps carry crypto type 0).
    const std::string plainRaw = buildNsp(false);
    const std::string plainPath = writeTemp(plainRaw);
    if (!plainPath.empty()) {
        nx2a::NspInfo plainInfo;
        MemorySource plainSource;
        plainSource.bytes.assign(plainRaw.begin(), plainRaw.end());
        nx2a::inspectNsp(plainSource, &plainInfo);
        // No encrypted title key here: the check must fail at the ticket, not
        // crash, and must never echo material.
        const nx2a::ContentCheck plain = nx2a::checkContentKeys(plainPath, keys);
        check(!plain.ok, "synthetic plaintext ticket without title key fails cleanly");
        unlink(plainPath.c_str());
    }

    // 4. Malformed keys: line number, never the value.
    nx2a::KeySet broken;
    std::string why;
    const bool parsed = broken.loadText("header_key = zzzz\n", &why);
    check(!parsed, "malformed hex is rejected");
    check(why.find("linha 1") != std::string::npos, "error names the line");
    check(why.find("zzzz") == std::string::npos, "error does not echo the value");

    // 5. A cartridge dump is rejected with a specific code.
    MemorySource xci;
    xci.bytes.assign(0x100, 0);
    std::memcpy(xci.bytes.data(), "HEAD", 4);
    nx2a::NspInfo head;
    check(!nx2a::inspectNsp(xci, &head), "XCI magic rejected");
    check(head.code == "ERR_XCI", "XCI error code");

    // 6. Truncated containers do not read out of bounds.
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