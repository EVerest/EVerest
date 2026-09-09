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
 * Implemented in sha_hash_openssl.cpp (ISO15118_ENABLE_TLS=ON, via OpenSSL's EVP_Digest - see
 * CMake option ISO15118_ENABLE_TLS) or sha_hash_stub.cpp (ISO15118_ENABLE_TLS=OFF: logs an error
 * and returns a zeroed digest, same degrade-and-log convention as io::make_tls_connection). A
 * build without OpenSSL therefore does not yet compute a spec-correct hash here; a portable (or
 * mbedTLS-backed) SHA-512 implementation is a follow-up, tracked together with the mbedTLS port
 * of the TLS connection itself.
 */
sha512_hash_t sha512(const uint8_t* data, std::size_t len);

} // namespace iso15118::io
