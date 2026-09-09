// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

// Built instead of connection_ssl.cpp when the CMake option ISO15118_ENABLE_TLS is OFF, so that
// this library can be linked without OpenSSL/everest::tls. See io/connection_ssl.hpp for details.
#include <iso15118/io/connection_ssl.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::io {

std::unique_ptr<IConnection> make_tls_connection(PollManager&, const std::string&, const config::SSLConfig&) {
    logf_error("A TLS connection was requested, but this build of libiso15118 was compiled without TLS support "
               "(ISO15118_ENABLE_TLS=OFF).");
    return nullptr;
}

} // namespace iso15118::io
