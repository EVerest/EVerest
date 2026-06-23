// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "everest/slac/slac_event.hpp"
#include "everest/io/socket/socket.hpp"
#include <cstring>
#include <everest/util/misc/bind.hpp>
#include <linux/if_ether.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <net/if.h>
#include <optional>
#include <sstream>
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
    try {
        m_mac_address = io::socket::get_mac_address(if_name);
    } catch (...) {
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
            m_mac_address = raw_handler->get_mac_address();
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
    return result;
}

bool SlacEvent::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
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
    m_connection.set_on_ready_action([callback]() {
        if (callback) {
            callback();
        }
    });
}

bool SlacEvent::send(HomeplugMessage& msg) {
    msg.set_source(m_mac_address);
    return m_connection.tx(msg);
}

const uint8_t* SlacEvent::get_mac_addr() {
    return m_mac_address.data();
}

} // namespace everest::lib::slac
