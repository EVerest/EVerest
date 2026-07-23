// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/random.hpp>

#include <stdexcept>

#include <openssl/rand.h>

namespace iso15118 {

void fill_random(uint8_t* data, size_t len) {
    if (len == 0) {
        return;
    }
    if (RAND_bytes(data, static_cast<int>(len)) != 1) {
        throw std::runtime_error("RAND_bytes failed to generate secure random data");
    }
}

} // namespace iso15118
