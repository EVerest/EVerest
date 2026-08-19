// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_endpoint_base_impl.hpp>

#include <everest/io/tls/tls_server_socket.hpp>

namespace everest::lib::io::tls {

template class tls_endpoint_base<tls_server_socket>;

} // namespace everest::lib::io::tls
