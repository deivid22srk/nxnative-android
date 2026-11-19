#include "crypto.h"

#include <array>
#include <cstring>
#include <mutex>

namespace nx2a {
namespace {

struct Tables {
    std::uint8_t sbox[256];
    std::uint8_t inv[256];
};

std::uint8_t gmul(std::uint8_t a, std::uint8_t b) {
    std::uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) p ^= a;
        const std::uint8_t hi = static_cast<std::uint8_t>(a & 0x80);
        a = static_cast<std::uint8_t>(a << 1);
        if (hi) a ^= 0x1B;
        b = static_cast<std::uint8_t>(b >> 1);
    }
    return p;
}

std::uint8_t ginv(std::uint8_t a) {
    if (a == 0) return 0;
    std::uint8_t r = 1;
    for (int i = 0; i < 254; ++i) r = gmul(r, a);
    return r;
}

std::uint8_t rotl8(std::uint8_t x, int n) {
    return static_cast<std::uint8_t>((x << n) | (x >> (8 - n)));
}

const Tables& tables() {
    static Tables t;
    static std::once_flag once;
    std::call_once(once, [] {
        for (int i = 0; i < 256; ++i) {
            const std::uint8_t x = ginv(static_cast<std::uint8_t>(i));
            const std::uint8_t s = static_cast<std::uint8_t>(
                x ^ rotl8(x, 1) ^ rotl8(x, 2) ^ rotl8(x, 3) ^ rotl8(x, 4) ^ 0x63);
            t.sbox[i] = s;
            t.inv[s] = static_cast<std::uint8_t>(i);
        }
    });
    return t;
}

struct RoundKeys {
    std::uint8_t rk[176];
};

void expandKey(const std::uint8_t key[16], RoundKeys* out) {
    static const std::uint8_t rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08,
                                          0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};
    const Tables& t = tables();
    std::memcpy(out->rk, key, 16);
    for (int i = 16; i < 176; i += 4) {
        std::uint8_t temp[4];
        std::memcpy(temp, out->rk + i - 4, 4);
        if (i % 16 == 0) {
            const std::uint8_t first = temp[0];
            temp[0] = t.sbox[temp[1]];
            temp[1] = t.sbox[temp[2]];
            temp[2] = t.sbox[temp[3]];
            temp[3] = t.sbox[first];
            temp[0] ^= rcon[i / 16];
        }
        for (int k = 0; k < 4; ++k) {
            out->rk[i + k] = static_cast<std::uint8_t>(out->rk[i - 16 + k] ^ temp[k]);
        }
    }
}

void addRoundKey(std::uint8_t s[16], const std::uint8_t* rk) {
    for (int i = 0; i < 16; ++i) s[i] ^= rk[i];
}

void subBytes(std::uint8_t s[16], const std::uint8_t box[256]) {
    for (int i = 0; i < 16; ++i) s[i] = box[s[i]];
}

void shiftRows(std::uint8_t s[16]) {
    std::uint8_t t[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            t[r + 4 * c] = s[r + 4 * ((c + r) & 3)];
        }
    }
    std::memcpy(s, t, 16);
}

void invShiftRows(std::uint8_t s[16]) {
    std::uint8_t t[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            t[r + 4 * ((c + r) & 3)] = s[r + 4 * c];
        }
    }
    std::memcpy(s, t, 16);
}

void mixColumns(std::uint8_t s[16]) {
    for (int c = 0; c < 4; ++c) {
        std::uint8_t* p = s + 4 * c;
        const std::uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = static_cast<std::uint8_t>(gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);
        p[1] = static_cast<std::uint8_t>(a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);
        p[2] = static_cast<std::uint8_t>(a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));
        p[3] = static_cast<std::uint8_t>(gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
    }
}

void invMixColumns(std::uint8_t s[16]) {
    for (int c = 0; c < 4; ++c) {
        std::uint8_t* p = s + 4 * c;
        const std::uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = static_cast<std::uint8_t>(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
        p[1] = static_cast<std::uint8_t>(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
        p[2] = static_cast<std::uint8_t>(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
        p[3] = static_cast<std::uint8_t>(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
    }
}

void incrementCounter(std::uint8_t counter[16]) {
    for (int i = 15; i >= 0; --i) {
        if (++counter[i] != 0) break;
    }
}

void xtsTweak(const std::uint8_t key[32], std::uint64_t unit, std::uint8_t tweak[16]) {
    // Nintendo's NCA headers feed the sector number as a big-endian 128-bit
    // data unit (verified against real NCA headers); XTS then uses the usual
    // little-endian multiply-by-alpha over the tweak bytes.
    std::memset(tweak, 0, 16);
    for (int i = 0; i < 8; ++i) tweak[15 - i] = static_cast<std::uint8_t>(unit >> (8 * i));
    aes128EncryptBlock(key + 16, tweak, tweak);
}

void xtsMulAlpha(std::uint8_t tweak[16]) {
    std::uint8_t carry = 0;
    for (int i = 0; i < 16; ++i) {
        const std::uint8_t next = static_cast<std::uint8_t>(tweak[i] >> 7);
        tweak[i] = static_cast<std::uint8_t>((tweak[i] << 1) | carry);
        carry = next;
    }
    if (carry) tweak[0] ^= 0x87;
}

}  // namespace

void aes128EncryptBlock(const std::uint8_t key[16], const std::uint8_t in[16],
                        std::uint8_t out[16]) {
    const Tables& t = tables();
    RoundKeys rk{};
    expandKey(key, &rk);
    std::uint8_t s[16];
    std::memcpy(s, in, 16);
    addRoundKey(s, rk.rk);
    for (int round = 1; round < 10; ++round) {
        subBytes(s, t.sbox);
        shiftRows(s);
        mixColumns(s);
        addRoundKey(s, rk.rk + round * 16);
    }
    subBytes(s, t.sbox);
    shiftRows(s);
    addRoundKey(s, rk.rk + 160);
    std::memcpy(out, s, 16);
}

void aes128DecryptBlock(const std::uint8_t key[16], const std::uint8_t in[16],
                        std::uint8_t out[16]) {
    const Tables& t = tables();
    RoundKeys rk{};
    expandKey(key, &rk);
    std::uint8_t s[16];
    std::memcpy(s, in, 16);
    addRoundKey(s, rk.rk + 160);
    for (int round = 9; round > 0; --round) {
        invShiftRows(s);
        subBytes(s, t.inv);
        addRoundKey(s, rk.rk + round * 16);
        invMixColumns(s);
    }
    invShiftRows(s);
    subBytes(s, t.inv);
    addRoundKey(s, rk.rk);
    std::memcpy(out, s, 16);
}

void aes128EcbDecrypt(const std::uint8_t key[16], std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i + 16 <= size; i += 16) {
        std::uint8_t block[16];
        aes128DecryptBlock(key, data + i, block);
        std::memcpy(data + i, block, 16);
    }
}

void aes128EcbEncrypt(const std::uint8_t key[16], std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i + 16 <= size; i += 16) {
        std::uint8_t block[16];
        aes128EncryptBlock(key, data + i, block);
        std::memcpy(data + i, block, 16);
    }
}

void aes128CtrCrypt(const std::uint8_t key[16], const std::uint8_t counter[16],
                    std::uint8_t* data, std::size_t size) {
    std::uint8_t ctr[16];
    std::memcpy(ctr, counter, 16);
    std::size_t done = 0;
    while (done < size) {
        std::uint8_t stream[16];
        aes128EncryptBlock(key, ctr, stream);
        const std::size_t n = (size - done < 16) ? (size - done) : 16;
        for (std::size_t i = 0; i < n; ++i) data[done + i] ^= stream[i];
        done += n;
        incrementCounter(ctr);
    }
}

void aes128XtsDecrypt(const std::uint8_t key[32], std::uint64_t dataUnit,
                      std::uint8_t* data, std::size_t size) {
    std::uint8_t tweak[16];
    xtsTweak(key, dataUnit, tweak);
    std::size_t done = 0;
    while (done < size) {
        const std::size_t n = (size - done < 16) ? (size - done) : 16;
        std::uint8_t block[16];
        std::memcpy(block, data + done, n);
        for (std::size_t i = 0; i < n; ++i) block[i] ^= tweak[i];
        std::uint8_t plain[16];
        aes128DecryptBlock(key, block, plain);
        for (std::size_t i = 0; i < n; ++i) data[done + i] = static_cast<std::uint8_t>(plain[i] ^ tweak[i]);
        xtsMulAlpha(tweak);
        done += n;
    }
}

void aes128XtsEncrypt(const std::uint8_t key[32], std::uint64_t dataUnit,
                      std::uint8_t* data, std::size_t size) {
    std::uint8_t tweak[16];
    xtsTweak(key, dataUnit, tweak);
    std::size_t done = 0;
    while (done < size) {
        const std::size_t n = (size - done < 16) ? (size - done) : 16;
        std::uint8_t block[16];
        std::memcpy(block, data + done, n);
        for (std::size_t i = 0; i < n; ++i) block[i] ^= tweak[i];
        std::uint8_t cipher[16];
        aes128EncryptBlock(key, block, cipher);
        for (std::size_t i = 0; i < n; ++i) data[done + i] = static_cast<std::uint8_t>(cipher[i] ^ tweak[i]);
        xtsMulAlpha(tweak);
        done += n;
    }
}

}  // namespace nx2a