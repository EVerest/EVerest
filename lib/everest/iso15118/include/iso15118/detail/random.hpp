// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>

namespace iso15118 {

/// \brief Fill \p len bytes at \p data with cryptographically secure random bytes.
///
/// Security-relevant values (GenChallenge, session IDs, nonces) must come from a CSPRNG, not from
/// std::mt19937 (see ISO 15118-2 [V2G2-826]/[V2G2-835]). Backed by OpenSSL RAND_bytes.
/// \throws std::runtime_error if the underlying CSPRNG fails to produce random data.
void fill_random(uint8_t* data, size_t len);

} // namespace iso15118
