// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/detail/tls_result_from_libtls.hpp>
#include <everest/io/tls/tls_server_socket.hpp>

#include <utility>

namespace everest::lib::io::tls {

tls_server_socket::tls_server_socket(tls_server_socket&&) noexcept = default;
tls_server_socket& tls_server_socket::operator=(tls_server_socket&&) noexcept = default;
tls_server_socket::~tls_server_socket() = default;

bool tls_server_socket::open(std::unique_ptr<::tls::ServerConnection> conn) {
    m_conn = std::move(conn);
    m_handshake_done = false;
    m_desired = event::poll_events::read;
    reset_error_state();
    return static_cast<bool>(m_conn);
}

::tls::Connection* tls_server_socket::connection() const {
    return m_conn.get();
}

io_result tls_server_socket::step_handshake() {
    return detail::to_io_result(m_conn->accept(0));
}

void tls_server_socket::reset_connection() {
    m_conn.reset();
}

std::function<void()> tls_server_socket::release_closer() {
    // The BIO owns the fd, so deferring the release keeps the fd open until the closer runs.
    // shared_ptr, not the move-only unique_ptr, to stay copy-constructible for std::function.
    return [conn = std::shared_ptr<::tls::ServerConnection>(std::move(m_conn))]() mutable { conn.reset(); };
}

} // namespace everest::lib::io::tls
