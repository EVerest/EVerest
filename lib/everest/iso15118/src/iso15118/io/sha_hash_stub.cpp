// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Built instead of sha_hash_openssl.cpp when ISO15118_ENABLE_TLS is OFF, so that this library can
// be linked without OpenSSL. See io/sha_hash.hpp for details and the follow-up this stub implies.
#include <iso15118/io/sha_hash.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::io {

sha512_hash_t sha512(const uint8_t*, std::size_t) {
    logf_error("A SHA-512 digest was requested, but this build of libiso15118 was compiled without "
               "OpenSSL (ISO15118_ENABLE_TLS=OFF); returning a zeroed digest.");
    return sha512_hash_t{};
}

} // namespace iso15118::io
