// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include "everest/io/event/fd_event_handler.hpp"
#include <algorithm>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/sync_udp_client.hpp>
#include <chrono>
#include <optional>

namespace charge_bridge::utilities {
using reply = sync_udp_client::reply;
using namespace std::chrono_literals;

namespace {
// Upper bound for a single blocking poll while an abort check is armed. It caps how long a caller
// asking for cancellation (a shutdown, typically) has to wait for the retry loop to notice, without
// changing the request's timeout or retry budget.
constexpr auto poll_slice = 100ms;
} // namespace

sync_udp_client::sync_udp_client(std::string const& remote, std::uint16_t port) : m_retries(0), m_timeout_ms(1000) {
    init(remote, port);
}

sync_udp_client::sync_udp_client(std::string const& remote, std::uint16_t port, std::uint16_t retries,
                                 std::uint16_t timeout_ms) :
    m_retries(retries), m_timeout_ms(timeout_ms) {
    init(remote, port);
}

void sync_udp_client::init(std::string const& remote, std::uint16_t port) {
    m_udp.open_as_client(remote, port);
    m_handler.register_event_handler(
        m_udp.get_fd(), [this](auto) {}, everest::lib::io::event::poll_events::read);
}

reply sync_udp_client::request_reply(udp_payload const& payload, abort_check const& abort_requested,
                                     reply_filter const& accept_reply) {
    return request_reply(payload, m_timeout_ms, m_retries, abort_requested, accept_reply);
}

reply sync_udp_client::request_reply(udp_payload const& payload, std::uint16_t timeout_ms, std::uint16_t retries,
                                     abort_check const& abort_requested, reply_filter const& accept_reply) {
    auto const aborted = [&abort_requested]() { return abort_requested and abort_requested(); };

    clear_socket();
    if (aborted() or not m_udp.tx(payload)) {
        return std::nullopt;
    }
    for (std::uint16_t i = 0; i < retries; ++i) {
        bool socket_failed = false;
        auto result =
            wait_for_reply(std::chrono::milliseconds(timeout_ms), abort_requested, accept_reply, socket_failed);
        if (result) {
            return result;
        }
        if (socket_failed) {
            return std::nullopt;
        }
        // No reply within the timeout, or the abort check fired while waiting. Only a plain
        // timeout deserves another attempt; a cancelled request reports a missing reply.
        if (aborted() or not m_udp.tx(payload)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

reply sync_udp_client::wait_for_reply(std::chrono::milliseconds timeout, abort_check const& abort_requested,
                                      reply_filter const& accept_reply, bool& socket_failed) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    udp_payload result;
    std::size_t discarded = 0;
    // One line per request instead of one per drop: a sender flooding the socket produces tens of
    // thousands of rejected datagrams per second, and logging each of them is the more expensive
    // half of that problem.
    auto const report_discards = [&discarded]() {
        if (discarded > 0) {
            print_info("", "UDP") << "discarded " << discarded << " unexpected datagram(s) while waiting for a reply"
                                  << std::endl;
        }
    };
    while (poll_for_reply(deadline, abort_requested)) {
        if (not m_udp.rx(result)) {
            socket_failed = true;
            report_discards();
            return std::nullopt;
        }
        if (not accept_reply or accept_reply(result)) {
            report_discards();
            return result;
        }
        // Not an answer to this request: a late reply to a previous one, or a foreign datagram - UDP
        // carries no correlation of its own. Handing it out would make the caller parse it as this
        // request's reply, so it is dropped and the wait resumes on the same deadline, leaving both
        // the timeout and the retry budget untouched. poll_for_reply() re-evaluates the deadline and
        // the abort check before every pass, so a continuous stream of rejected datagrams cannot
        // keep this loop - and with it the caller's thread - alive past the timeout.
        ++discarded;
    }
    report_discards();
    return std::nullopt;
}

bool sync_udp_client::poll_for_reply(std::chrono::steady_clock::time_point const& deadline,
                                     abort_check const& abort_requested) {
    auto const aborted = [&abort_requested]() { return abort_requested and abort_requested(); };
    while (true) {
        auto const remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        // The deadline and the abort check are evaluated before the readiness test, not only after a
        // poll timed out: poll(0ms) keeps reporting readiness while datagrams are queued, so asking
        // the socket first would let the caller's discard loop run past the deadline - and never
        // reach a cancellation check - for as long as a sender keeps the queue non-empty.
        if (remaining <= 0ms or aborted()) {
            return false;
        }
        if (not abort_requested) {
            return m_handler.poll(remaining);
        }
        if (m_handler.poll(std::min(remaining, poll_slice))) {
            return true;
        }
    }
}

bool sync_udp_client::tx(udp_payload const& payload) {
    return m_udp.tx(payload);
}

reply sync_udp_client::rx() {
    return rx(m_timeout_ms);
}

reply sync_udp_client::rx(std::uint16_t timeout_ms) {
    udp_payload result;
    if (not m_handler.poll(std::chrono::milliseconds(timeout_ms))) {
        return std::nullopt;
    }
    if (not m_udp.rx(result)) {
        return std::nullopt;
    }
    return result;
}

bool sync_udp_client::is_open() {
    return m_udp.is_open();
}

void sync_udp_client::clear_socket() {
    udp_payload tmp;
    while (m_handler.poll(0ms)) {
        m_udp.rx(tmp);
    };
}

} // namespace charge_bridge::utilities
