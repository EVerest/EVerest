// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cerrno>

namespace everest::lib::io::tls {

// Not ::tls::Connection::result_t: that type is nested, so it cannot be forward declared.
enum class io_result {
    success,
    want_read,
    want_write,
    closed,
    timeout,
    failed,
};

// Map a terminal TLS result to an errno-style code, so get_error() reports a POSIX error, not -1.
inline int errno_from_result(io_result r) {
    switch (r) {
    case io_result::closed:
        return ECONNRESET;
    case io_result::timeout:
        return ETIMEDOUT;
    default:
        return EPROTO;
    }
}

} // namespace everest::lib::io::tls
