// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Dependency-free SHA-512 (FIPS 180-4) implementation of io::sha512() - see io/sha_hash.hpp.
// Replaces the previous OpenSSL-backed implementation (and its no-OpenSSL stub): this needs
// neither OpenSSL nor a platform split, since it's the same portable C++ on every target this
// library supports. Verified against sha512sum on inputs spanning every padding boundary (0, 1,
// 55-57, 111-113, 127-129, and several multi-block lengths).
#include <iso15118/io/sha_hash.hpp>

#include <cstring>

namespace iso15118::io {

namespace {

constexpr std::size_t BLOCK_SIZE = 128; // bytes, 1024 bits

// FIPS 180-4 §4.2.3: first 64 bits of the fractional parts of the cube roots of the first 80
// primes.
constexpr uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

uint64_t rotr(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64 - n));
}

// SHA-512 compression: absorbs 128-byte (1024-bit) blocks into an 8x64-bit running hash state.
struct Sha512State {
    // FIPS 180-4 §5.3.5: initial hash value, first 64 bits of the fractional parts of the square
    // roots of the first 8 primes.
    uint64_t h[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
    };

    void process_block(const uint8_t block[BLOCK_SIZE]) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint64_t{block[i * 8 + 0]} << 56) | (uint64_t{block[i * 8 + 1]} << 48) |
                   (uint64_t{block[i * 8 + 2]} << 40) | (uint64_t{block[i * 8 + 3]} << 32) |
                   (uint64_t{block[i * 8 + 4]} << 24) | (uint64_t{block[i * 8 + 5]} << 16) |
                   (uint64_t{block[i * 8 + 6]} << 8) | (uint64_t{block[i * 8 + 7]});
        }
        for (int i = 16; i < 80; ++i) {
            const uint64_t s0 = rotr(w[i - 15], 1) ^ rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = rotr(w[i - 2], 19) ^ rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 80; ++i) {
            const uint64_t S1 = rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41);
            const uint64_t ch = (e & f) ^ (~e & g);
            const uint64_t temp1 = hh + S1 + ch + K[i] + w[i];
            const uint64_t S0 = rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39);
            const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint64_t temp2 = S0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
};

} // namespace

sha512_hash_t sha512(const uint8_t* data, std::size_t len) {
    Sha512State state;

    const std::size_t full_blocks = len / BLOCK_SIZE;
    for (std::size_t i = 0; i < full_blocks; ++i) {
        state.process_block(data + i * BLOCK_SIZE);
    }

    // Padding (FIPS 180-4 §5.1.2): a 0x80 byte, zeros, then the message length as a 128-bit
    // big-endian bit count, filling out one or two final 128-byte blocks depending on how much
    // room is left after the remaining message bytes.
    uint8_t tail[BLOCK_SIZE * 2] = {};
    const std::size_t remaining = len - full_blocks * BLOCK_SIZE;
    std::memcpy(tail, data + full_blocks * BLOCK_SIZE, remaining);
    tail[remaining] = 0x80;

    const std::size_t tail_blocks = (remaining < BLOCK_SIZE - 16) ? 1 : 2;
    const std::size_t total_tail_len = tail_blocks * BLOCK_SIZE;

    // The bit length fits in 64 bits for every input this library ever hashes, so the upper 8
    // bytes of the 128-bit length field are left at zero.
    const uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[total_tail_len - 1 - i] = static_cast<uint8_t>(bit_len >> (8 * i));
    }

    for (std::size_t i = 0; i < tail_blocks; ++i) {
        state.process_block(tail + i * BLOCK_SIZE);
    }

    sha512_hash_t digest{};
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            digest[i * 8 + j] = static_cast<uint8_t>(state.h[i] >> (56 - 8 * j));
        }
    }
    return digest;
}

} // namespace iso15118::io
