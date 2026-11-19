// Minimal, dependency-free AES-128 (ECB / CTR / XTS). Used by the on-device
// pipeline to prove that prod.keys can actually unlock the ticket and the NCA
// header. Keys and decrypted material stay in memory; nothing is logged.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nx2a {

// FIPS-197 AES-128. `key` is 16 bytes.
void aes128EncryptBlock(const std::uint8_t key[16], const std::uint8_t in[16],
                        std::uint8_t out[16]);
void aes128DecryptBlock(const std::uint8_t key[16], const std::uint8_t in[16],
                        std::uint8_t out[16]);

// ECB over whole 16-byte blocks (no padding). `data.size() % 16 == 0`.
void aes128EcbDecrypt(const std::uint8_t key[16], std::uint8_t* data, std::size_t size);
void aes128EcbEncrypt(const std::uint8_t key[16], std::uint8_t* data, std::size_t size);

// Big-endian 128-bit counter, as used by NCA section encryption.
void aes128CtrCrypt(const std::uint8_t key[16], const std::uint8_t counter[16],
                    std::uint8_t* data, std::size_t size);

// IEEE 1619 XTS with a 32-byte key (two AES-128 keys). The 128-bit data unit
// number is encoded big-endian, as Nintendo's NCA headers do (verified
// against real headers); `size` should be a multiple of 16.
void aes128XtsDecrypt(const std::uint8_t key[32], std::uint64_t dataUnit,
                      std::uint8_t* data, std::size_t size);
void aes128XtsEncrypt(const std::uint8_t key[32], std::uint64_t dataUnit,
                      std::uint8_t* data, std::size_t size);

}  // namespace nx2a