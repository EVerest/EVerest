// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "everest/io/udp/udp_payload.hpp"
#include <chrono>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/udp/udp_socket.hpp>
#include <functional>
#include <optional>

namespace charge_bridge::utilities {

class sync_udp_client {
public:
    using udp_payload = everest::lib::io::udp::udp_payload;
    using reply = std::optional<udp_payload>;
    /// Cancellation check consulted while waiting for a reply. An empty check makes the request
    /// non-cancellable, i.e. it runs its full timeout/retry budget.
    using abort_check = std::function<bool()>;
    /// Decides whether a received datagram is the reply to the request that was just sent. UDP has
    /// no request/reply correlation of its own, so without such a filter the first datagram that
    /// arrives is returned - which may be a late reply to a previous request. An empty filter keeps
    /// exactly that behaviour.
    using reply_filter = std::function<bool(udp_payload const&)>;
    sync_udp_client(std::string const& remote, std::uint16_t port);
    sync_udp_client(std::string const& remote, std::uint16_t port, std::uint16_t retries, std::uint16_t timeout_ms);
    /// @param abort_requested If set, it is polled while waiting for the reply and between retries.
    /// Once it returns true the request gives up and reports a missing reply, so a cancelled request
    /// looks like a failed request to the caller.
    /// @param accept_reply If set, datagrams it rejects are discarded and the wait continues on the
    /// same deadline, so a stale reply neither satisfies nor shortens this request.
    reply request_reply(udp_payload const& payload, abort_check const& abort_requested = {},
                        reply_filter const& accept_reply = {});
    reply request_reply(udp_payload const& payload, std::uint16_t timeout_ms, std::uint16_t retries,
                        abort_check const& abort_requested = {}, reply_filter const& accept_reply = {});
    bool tx(udp_payload const& payload);
    reply rx();
    reply rx(std::uint16_t timeout_ms);
    bool is_open();

private:
    void init(std::string const& remote, std::uint16_t port);
    void clear_socket();
    /// Wait up to \p timeout for a datagram accepted by \p accept_reply. Datagrams rejected by the
    /// filter are dropped and the wait resumes on the same deadline.
    /// @param[out] socket_failed Set when reading the socket itself failed, which - unlike a plain
    /// timeout - is not worth another transmission.
    reply wait_for_reply(std::chrono::milliseconds timeout, abort_check const& abort_requested,
                         reply_filter const& accept_reply, bool& socket_failed);
    /// Wait for readability until \p deadline. With an armed \p abort_requested the wait is sliced,
    /// so the check runs regularly instead of only after the full timeout has elapsed. Returns false
    /// once the deadline has passed or the abort check fired, even if the socket has data queued -
    /// that is what bounds the caller's discard loop when datagrams keep arriving.
    bool poll_for_reply(std::chrono::steady_clock::time_point const& deadline, abort_check const& abort_requested);

    std::uint16_t m_retries;
    std::uint16_t m_timeout_ms;
    everest::lib::io::udp::udp_client_socket m_udp;
    everest::lib::io::event::fd_event_handler m_handler;
};

} // namespace charge_bridge::utilities
