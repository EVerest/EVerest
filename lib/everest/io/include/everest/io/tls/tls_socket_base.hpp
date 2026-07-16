// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_result.hpp>
#include <everest/tls/tls.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace everest::lib::io::tls {

// Shared fd_event_client policy logic for the TLS client and server sockets.
// Derived (client/server) provides three public hooks the base calls:
//   ::tls::Connection* connection() const;          // current connection or nullptr
//   ::tls::Connection::result_t step_handshake();   // connect(0) or accept(0)
//   void reset_connection();                         // release the held connection
template <class Derived> class tls_socket_base {
public:
    using PayloadT = std::vector<std::uint8_t>;

    bool handshake_step() {
        auto* c = self().connection();
        if (c == nullptr || m_handshake_done) {
            return false;
        }
        using result_t = ::tls::Connection::result_t;
        const auto res = self().step_handshake();
        switch (res) {
        case result_t::success:
            m_handshake_done = true;
            m_desired = event::poll_events::read;
            return true;
        case result_t::want_read:
            m_desired = event::poll_events::read;
            return true;
        case result_t::want_write:
            m_desired = event::poll_events::write;
            return true;
        default:
            m_last_error = errno_from_result(res);
            m_last_error_text = c->last_error();
            // Keep the Connection (and its fd) alive; the endpoint's fail() defers
            // the close so the fd number stays reserved until it is unregistered.
            m_failed = true;
            return false;
        }
    }

    bool handshake_complete() const {
        return m_handshake_done;
    }

    bool tx(PayloadT& payload) {
        if (payload.empty()) {
            return true;
        }
        auto* c = self().connection();
        if (c == nullptr) {
            return false;
        }
        using result_t = ::tls::Connection::result_t;
        std::size_t written = 0;
        const auto res = c->write(reinterpret_cast<std::byte const*>(payload.data()), payload.size(), written, 0);
        switch (res) {
        case result_t::success:
            payload.erase(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(written));
            return payload.empty();
        case result_t::want_read:
            if (written > 0) {
                payload.erase(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(written));
            }
            m_desired = event::poll_events::read;
            return false;
        case result_t::want_write:
            if (written > 0) {
                payload.erase(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(written));
            }
            m_desired = event::poll_events::write;
            return false;
        default:
            m_last_error = errno_from_result(res);
            m_last_error_text = c->last_error();
            m_failed = true;
            return false;
        }
    }

    bool rx(PayloadT& buffer) {
        auto* c = self().connection();
        if (c == nullptr) {
            return false;
        }
        using result_t = ::tls::Connection::result_t;
        std::byte tmp[c_read_chunk_size];
        std::size_t n = 0;
        const auto res = c->read(tmp, sizeof tmp, n, 0);
        buffer.clear();
        switch (res) {
        case result_t::success:
            buffer.assign(reinterpret_cast<std::uint8_t*>(tmp), reinterpret_cast<std::uint8_t*>(tmp) + n);
            // libtls/OpenSSL can buffer several decrypted records under
            // level-triggered epoll. Drain SSL_pending() so the whole message is
            // delivered in one rx() call rather than one record per loop wakeup.
            while (self().connection() != nullptr && self().connection()->pending() > 0) {
                std::size_t more = 0;
                const auto next = self().connection()->read(tmp, sizeof tmp, more, 0);
                if (next != result_t::success || more == 0) {
                    break;
                }
                buffer.insert(buffer.end(), reinterpret_cast<std::uint8_t*>(tmp),
                              reinterpret_cast<std::uint8_t*>(tmp) + more);
            }
            m_desired = event::poll_events::read;
            return not buffer.empty();
        case result_t::want_read:
            m_desired = event::poll_events::read;
            return false;
        case result_t::want_write:
            m_desired = event::poll_events::write;
            return false;
        default:
            m_last_error = errno_from_result(res);
            m_last_error_text = c->last_error();
            m_failed = true;
            return false;
        }
    }

    bool is_open() const {
        return not m_failed and self().connection() != nullptr;
    }

    int get_fd() const {
        auto* c = self().connection();
        return c != nullptr ? c->socket() : -1;
    }

    int get_error() const {
        return m_last_error;
    }

    // OpenSSL error text captured from the Connection that the last failure
    // destroyed; empty when no error text was reported.
    const std::string& get_error_string() const {
        return m_last_error_text;
    }

    void close() {
        if (auto* c = self().connection()) {
            c->shutdown(0);
        }
        self().reset_connection();
        m_desired = event::poll_events::read;
        m_handshake_done = false;
        m_failed = false;
        m_last_error_text.clear();
    }

    event::poll_events desired_events() const {
        return m_desired;
    }

protected:
    static constexpr std::size_t c_read_chunk_size = 4096;

    Derived& self() {
        return static_cast<Derived&>(*this);
    }
    Derived const& self() const {
        return static_cast<Derived const&>(*this);
    }

    void reset_error_state() {
        m_last_error = 0;
        m_last_error_text.clear();
        m_failed = false;
    }

    event::poll_events m_desired{event::poll_events::read};
    int m_last_error{0};
    std::string m_last_error_text;
    bool m_handshake_done{false};
    // Set on a terminal TLS result. The Connection is kept alive (so its fd stays
    // open) until the endpoint's fail() defers the close; is_open() reports the
    // socket as closed so the endpoint routes through fail().
    bool m_failed{false};
};

} // namespace everest::lib::io::tls
