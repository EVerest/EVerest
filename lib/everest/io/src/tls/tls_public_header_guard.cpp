// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

// Compiled without libtls on the include path, so re-adding an everest/tls/tls.hpp include to any
// header below fails here. The impl and config headers need ::tls complete, so they are not listed.

#include <everest/io/tls/tls_client.hpp>
#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/io/tls/tls_endpoint_base.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_result.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/io/tls/tls_socket_base.hpp>

// A header may reach OpenSSL without naming libtls, which the include path alone would not catch.
#if defined(OPENSSL_VERSION_NUMBER) || defined(HEADER_SSL_H)
#error "a public everest_io tls header pulled in OpenSSL"
#endif
