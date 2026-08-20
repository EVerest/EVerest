// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/tls/tls_result.hpp>
#include <everest/tls/tls.hpp>

namespace everest::lib::io::tls::detail {

inline io_result to_io_result(::tls::Connection::result_t r) {
    using result_t = ::tls::Connection::result_t;
    switch (r) {
    case result_t::success:
        return io_result::success;
    case result_t::want_read:
        return io_result::want_read;
    case result_t::want_write:
        return io_result::want_write;
    case result_t::closed:
        return io_result::closed;
    case result_t::timeout:
        return io_result::timeout;
    }
    return io_result::failed;
}

} // namespace everest::lib::io::tls::detail
