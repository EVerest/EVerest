// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Opaque: including everest/tls/tls.hpp instead would chain libtls, and with it the OpenSSL
// headers, into every consumer.
namespace tls {
class Connection;
}

namespace everest::lib::io::tls {

// Shared fd_event_client policy logic for the TLS client and server sockets.
// Derived (client/server) provides three public hooks the base calls:
//   ::tls::Connection* connection() const;
//   io_result step_handshake();
//   void reset_connection();
// Member definitions live in tls_socket_base_impl.hpp.
template <class Derived> class tls_socket_base {
public:
    using PayloadT = std::vector<std::uint8_t>;

    bool handshake_step();

    bool handshake_complete() const {
        return m_handshake_done;
    }

    bool tx(PayloadT& payload);

    // Invariant: a false return from want_read/want_write must leave the error state untouched. The
    // event client maps every false rx() to action_status::fail and reports get_error(), so an error
    // recorded for a clean would-block would surface a spurious failure on every idle wakeup.
    bool rx(PayloadT& buffer);

    bool is_open() const;

    int get_fd() const;

    int get_error() const {
        return m_last_error;
    }

    // OpenSSL error text captured at the last failure, empty when none was reported.
    const std::string& get_error_string() const {
        return m_last_error_text;
    }

    void close();

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
    // Set on a terminal TLS result. The Connection stays alive (its fd stays open) until the
    // endpoint's fail() defers the close, is_open() reports closed so the endpoint routes there.
    bool m_failed{false};
};

} // namespace everest::lib::io::tls
