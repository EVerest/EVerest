// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_socket_base_impl.hpp>

#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/io/tls/tls_server_socket.hpp>

namespace everest::lib::io::tls {

template class tls_socket_base<tls_client_socket>;
template class tls_socket_base<tls_server_socket>;

} // namespace everest::lib::io::tls
