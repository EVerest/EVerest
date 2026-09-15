// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "everest/slac/slac_event.hpp"
#include "everest/io/socket/socket.hpp"
#include <cstring>
#include <everest/util/misc/bind.hpp>
#include <linux/if_ether.h>

#include <net/if.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

namespace everest::lib::slac {
using util::bind_obj;
using namespace std::chrono_literals;

SlacEvent::SlacEvent(std::string const& if_name) : m_connection(if_name), m_if_name(if_name) {
    m_error_timer.set_timeout(2s);

    m_connection.set_error_handler(bind_obj(&SlacEvent::handle_socket_error, this));
    m_connection.set_rx_handler(bind_obj(&SlacEvent::handle_socket_rx, this));
    m_connection.set_on_ready_action(bind_obj(&SlacEvent::handle_socket_ready, this));
    // May fail when the interface is not up yet (device enumerating late); the MAC is then all
    // zero until the socket connects, and refreshed before the ready callback runs.
    try {
        m_mac_address = io::socket::get_mac_address(if_name);
    } catch (...) {
    }
}

void SlacEvent::refresh_mac_address() {
    auto const& raw_handler = m_connection.get_raw_handler();
    if (raw_handler) {
        m_mac_address = raw_handler->get_mac_address();
    }
}

void SlacEvent::handle_socket_error(int id, std::string const& msg) {
    auto was_on_error = m_on_error;
    auto previous_detail = m_error_detail;

    m_on_error = id not_eq 0;

    auto detail = msg;
    auto const& raw_handler = m_connection.get_raw_handler();
    if (raw_handler) {
        auto const socket_error_message = raw_handler->get_error_message();
        if (!socket_error_message.empty()) {
            detail = socket_error_message;
        }
        if (not m_on_error) {
            refresh_mac_address();
        }
    }

    if (m_on_error) {
        m_error_detail = detail;
    } else {
        m_error_detail.clear();
    }

    if (not m_on_error) {
        if (m_error_cb and (m_on_error not_eq was_on_error)) {
            m_error_cb(m_on_error, "");
        }
        return;
    }

    auto const changed_detail = detail != previous_detail;
    if (m_error_cb and (m_on_error not_eq was_on_error or changed_detail)) {
        m_error_cb(m_on_error, m_error_detail);
    }
}

void SlacEvent::handle_socket_rx(HomeplugMessage const& data, [[maybe_unused]] slac_client::interface& client) {
    if (m_callback) {
        m_callback(data);
    }
}

void SlacEvent::handle_error_timer() {
    if (m_on_error) {
        m_connection.reset();
    }
}

bool SlacEvent::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.register_event_handler(&m_connection) && result;
    result = handler.register_event_handler(&m_error_timer, bind_obj(&SlacEvent::handle_error_timer, this)) && result;
    m_handler = &handler;
    return result;
}

bool SlacEvent::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    m_handler = nullptr;
    auto result = true;
    result = handler.unregister_event_handler(&m_connection) && result;
    result = handler.unregister_event_handler(&m_error_timer) && result;
    return result;
}

void SlacEvent::set_callback(const HomeplugMessageHandler& callback) {
    m_callback = callback;
}

void SlacEvent::set_error_callback(const HomeplugErrorHandler& callback) {
    m_error_cb = callback;
}

void SlacEvent::set_ready_callback(const HomeplugReadyHandler& callback) {
    m_ready_cb = callback;
}

void SlacEvent::handle_socket_ready() {
    // Once per connection, first open and every reopen alike, whether or not a consumer listens.
    // The socket is open now, so the interface MAC is readable even if it was not at construction;
    // refresh before the consumer copies it and starts its state machine.
    refresh_mac_address();
    // A new connection starts with a clean transmit record: a fault on the old one has been
    // reported, and one on this one must be reported again.
    m_tx_failures = 0;
    m_tx_fault_reported = false;
    if (m_ready_cb) {
        m_ready_cb();
    }
}

bool SlacEvent::send(HomeplugMessage& msg) {
    msg.set_source(m_mac_address);
    if (m_connection.tx(msg)) {
        m_tx_failures = 0;
        return true;
    }
    // A socket already in error is reported by handle_socket_error; only count while it claims
    // to be fine. The report itself is deferred: this runs inside the consumer's state machine.
    if (m_on_error or m_tx_fault_reported or ++m_tx_failures < TX_FAILURE_THRESHOLD) {
        return false;
    }
    if (m_handler) {
        m_tx_fault_reported = true;
        m_handler->add_action([this]() { report_tx_fault(); });
    }
    return false;
}

void SlacEvent::report_tx_fault() {
    // Loop action queue, after the dispatch that hit the threshold has returned. Reported even if
    // the socket failed in between: the consumer replaces the message, and the detail names what
    // was seen first.
    if (m_error_cb) {
        m_error_cb(true, "PLC frame transmission failing: " + std::to_string(m_tx_failures) +
                             " consecutive sends rejected by the socket");
    }
    // The consumer tears its state machine down on the report, so nothing sends again and no
    // accepted frame could ever signal recovery. Reopen the socket instead: the queued payloads are
    // dropped, and the new connection's ready callback is what restarts the consumer, the same way
    // it recovers from a socket error (handle_error_timer). A socket that failed in the meantime is
    // already on that timer.
    if (not m_on_error) {
        m_connection.reset();
    }
}

const uint8_t* SlacEvent::get_mac_addr() {
    return m_mac_address.data();
}

} // namespace everest::lib::slac
