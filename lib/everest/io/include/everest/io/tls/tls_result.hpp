// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/tls/tls.hpp>

#include <cerrno>

namespace everest::lib::io::tls {

// Map a terminal TLS result to an errno-style code so get_error() reports a
// meaningful POSIX error rather than a bare -1.
inline int errno_from_result(::tls::Connection::result_t r) {
    switch (r) {
    case ::tls::Connection::result_t::closed:
        return ECONNRESET;
    case ::tls::Connection::result_t::timeout:
        return ETIMEDOUT;
    default:
        return EPROTO;
    }
}

} // namespace everest::lib::io::tls
