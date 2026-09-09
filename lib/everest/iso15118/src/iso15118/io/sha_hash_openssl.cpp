// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// OpenSSL-backed implementation of io::sha512(), built instead of sha_hash_stub.cpp when
// ISO15118_ENABLE_TLS is ON (see src/iso15118/CMakeLists.txt and io/sha_hash.hpp).
#include <iso15118/io/sha_hash.hpp>

#include <openssl/evp.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::io {

sha512_hash_t sha512(const uint8_t* data, std::size_t len) {
    sha512_hash_t digest{};
    unsigned int digestlen{0};

    const auto result = EVP_Digest(data, len, digest.data(), &digestlen, EVP_sha512(), nullptr);
    if (not result) {
        logf_error("EVP_Digest failed");
        return sha512_hash_t{};
    }

    return digest;
}

} // namespace iso15118::io
