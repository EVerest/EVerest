// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace iso15118::io {

constexpr std::size_t sha_512_hash_size = 64;
using sha512_hash_t = std::array<uint8_t, sha_512_hash_size>;

/**
 * \brief Computes the SHA-512 digest of data.
 *
 * A dependency-free FIPS 180-4 implementation (see io/sha_hash.cpp) - unlike the TLS connection,
 * this does not need OpenSSL/mbedTLS or a platform split: it's plain portable C++ on every target
 * this library supports, built unconditionally regardless of ISO15118_ENABLE_TLS.
 */
sha512_hash_t sha512(const uint8_t* data, std::size_t len);

} // namespace iso15118::io
