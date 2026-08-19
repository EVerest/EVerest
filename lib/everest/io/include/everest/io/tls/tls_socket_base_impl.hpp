// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/tls/detail/tls_result_from_libtls.hpp>
#include <everest/io/tls/tls_socket_base.hpp>
#include <everest/tls/tls.hpp>

// Member definitions of tls_socket_base. Include to instantiate the base for a Derived of your own.

namespace everest::lib::io::tls {

template <class Derived> bool tls_socket_base<Derived>::handshake_step() {
    auto* c = self().connection();
    if (c == nullptr || m_handshake_done) {
        return false;
    }
    const auto res = self().step_handshake();
    switch (res) {
    case io_result::success:
        m_handshake_done = true;
        m_desired = event::poll_events::read;
        return true;
    case io_result::want_read:
        m_desired = event::poll_events::read;
        return true;
    case io_result::want_write:
        m_desired = event::poll_events::write;
        return true;
    default:
        m_last_error = errno_from_result(res);
        m_last_error_text = c->last_error();
        // The connection stays alive: the endpoint's fail() defers the close, keeping the fd reserved.
        m_failed = true;
        return false;
    }
}

template <class Derived> bool tls_socket_base<Derived>::tx(PayloadT& payload) {
    if (payload.empty()) {
        return true;
    }
    auto* c = self().connection();
    if (c == nullptr) {
        return false;
    }
    std::size_t written = 0;
    const auto res =
        detail::to_io_result(c->write(reinterpret_cast<std::byte const*>(payload.data()), payload.size(), written, 0));
    switch (res) {
    case io_result::success:
        payload.erase(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(written));
        // A want_write then a success must drop back to read, or desired_events() strands the rx side.
        m_desired = payload.empty() ? event::poll_events::read : event::poll_events::write;
        return payload.empty();
    // SSL_MODE_ENABLE_PARTIAL_WRITE is off: a would-block accepts nothing and must retry the same buffer.
    case io_result::want_read:
        m_desired = event::poll_events::read;
        return false;
    case io_result::want_write:
        m_desired = event::poll_events::write;
        return false;
    default:
        m_last_error = errno_from_result(res);
        m_last_error_text = c->last_error();
        m_failed = true;
        return false;
    }
}

template <class Derived> bool tls_socket_base<Derived>::rx(PayloadT& buffer) {
    auto* c = self().connection();
    if (c == nullptr) {
        return false;
    }
    std::byte tmp[c_read_chunk_size];
    std::size_t n = 0;
    const auto res = detail::to_io_result(c->read(tmp, sizeof tmp, n, 0));
    buffer.clear();
    switch (res) {
    case io_result::success:
        buffer.assign(reinterpret_cast<std::uint8_t*>(tmp), reinterpret_cast<std::uint8_t*>(tmp) + n);
        // A decrypted record's remainder sits in the record layer, not the socket: readability never returns.
        while (self().connection() != nullptr && self().connection()->has_pending()) {
            std::size_t more = 0;
            const auto next = detail::to_io_result(self().connection()->read(tmp, sizeof tmp, more, 0));
            if (next != io_result::success || more == 0) {
                break;
            }
            buffer.insert(buffer.end(), reinterpret_cast<std::uint8_t*>(tmp),
                          reinterpret_cast<std::uint8_t*>(tmp) + more);
        }
        m_desired = event::poll_events::read;
        return not buffer.empty();
    case io_result::want_read:
        m_desired = event::poll_events::read;
        return false;
    case io_result::want_write:
        m_desired = event::poll_events::write;
        return false;
    default:
        m_last_error = errno_from_result(res);
        m_last_error_text = c->last_error();
        m_failed = true;
        return false;
    }
}

template <class Derived> bool tls_socket_base<Derived>::is_open() const {
    return not m_failed and self().connection() != nullptr;
}

template <class Derived> int tls_socket_base<Derived>::get_fd() const {
    auto* c = self().connection();
    return c != nullptr ? c->socket() : -1;
}

template <class Derived> void tls_socket_base<Derived>::close() {
    if (auto* c = self().connection()) {
        c->shutdown(0);
    }
    self().reset_connection();
    m_desired = event::poll_events::read;
    m_handshake_done = false;
    m_failed = false;
    m_last_error_text.clear();
}

} // namespace everest::lib::io::tls
