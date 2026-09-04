// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "everest/io/serial/event_pty.hpp"
#include <charge_bridge/serial_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <cstring>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/socket/socket.hpp>
#include <protocol/cb_can_message.h>

namespace {
const int default_udp_timeout_ms = 1000;
// TCP liveness is a backstop since cb-session-v1 (the heartbeat session drives reconnects). Must
// outlast congestion stalls: with pty backpressure the tx path runs against the MCU's closed
// window by design. User timeout 60 s, keepalive 10 s idle + 4 x 2 s.
const std::uint32_t tcp_user_timeout_ms = 60000;
const std::uint32_t tcp_keepalive_count = 4;
const std::uint32_t tcp_keepalive_idle_s = 10;
const std::uint32_t tcp_keepalive_interval_s = 2;
// Pause reading the pty once this many payloads wait in the TCP tx buffer, resume on drain. Well
// below max_buffered_tx_payloads (1024), large enough that the MCU never starves.
const std::size_t pty_pause_watermark = 64;
// The MCU advertises a 512 B window and takes one packet per UART transfer: coalesce to the
// window and disable Nagle, otherwise ~200 ms delayed ACK per mini segment (~1-3 kB/s).
const std::size_t serial_tcp_payload_max = 512;
} // namespace

namespace charge_bridge {

serial_bridge::serial_bridge(serial_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify) :
    m_pty(), m_tcp_port(config.cb_port), m_tcp_remote(config.cb_remote), m_ready_notify(ready_notify) {
    using namespace std::chrono_literals;

    auto link_ok = m_symlink.set_link(m_pty.get_slave_path(), config.serial_device);
    if (not link_ok) {
        throw std::runtime_error("Failed to setup symbolic links for serial ports");
    }

    m_identifier = config.cb + "/" + config.item;

    create_tcp_client(m_tcp_remote, m_tcp_port);
    m_ready.setCallback([this](auto&, auto&) { m_ready_notify.notify(); });

    m_pty.set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "SERIAL/PTY", id) << msg << std::endl;
        m_pty_ready = id == 0;
        if (not m_pty_ready) {
            m_pty.reset();
        }
        handle_ready();
    });
}

void serial_bridge::create_tcp_client(std::string const& remote, uint16_t remote_port) {
    // Dedup latch is per client instance.
    m_tcp_tx_rejected = false;
    m_tcp = std::make_unique<everest::lib::io::tcp::tcp_client>(remote, remote_port, default_udp_timeout_ms);
    m_tcp->set_on_ready_action([this]() {
        m_tcp->get_raw_handler()->set_keep_alive(tcp_keepalive_count, tcp_keepalive_idle_s, tcp_keepalive_interval_s);
        m_tcp->get_raw_handler()->set_user_timeout(tcp_user_timeout_ms);
        try {
            everest::lib::io::socket::enable_tcp_no_delay(m_tcp->get_raw_handler()->get_fd());
        } catch (std::exception const& e) {
            utilities::print_error(m_identifier, "SERIAL/TCP", -1) << e.what() << std::endl;
        }
        // Up edge: resume a pty paused against the previous connection; reset() does not drain.
        resume_pty();
    });

    m_pty.set_data_handler([this](auto const& data, auto&) {
        if (not m_tcp) {
            return;
        }
        auto accepted = m_tcp->tx_coalescing(data, serial_tcp_payload_max);
        if (not accepted and not m_tcp_tx_rejected) {
            utilities::print_error(m_identifier, "SERIAL/TCP", -1) << "Dropped serial data, tx rejected" << std::endl;
        }
        m_tcp_tx_rejected = not accepted;
        // Past the watermark stop reading the pty until the buffer drained; a rejected tx pauses too.
        // The on_ready action resumes.
        if (not m_pty_paused and (not accepted or m_tcp->tx_queue_depth() >= pty_pause_watermark)) {
            m_pty_paused = m_pty.pause_rx();
        }
    });
    m_tcp->set_tx_drained_action([this]() { resume_pty(); });
    m_tcp->set_rx_handler([this](auto const& data, auto&) { m_pty.tx(data); });
    m_tcp->set_error_handler([this](auto id, auto const& msg) {
        if (m_tcp_last_error_id not_eq id) {
            utilities::print_error(m_identifier, "SERIAL/TCP", id) << msg << std::endl;
            m_tcp_last_error_id = id;
        }
        m_tcp_ready = id == 0;
        if (not m_tcp_ready) {
            if (m_tcp) {
                m_tcp->reset();
            }
            // Stay paused across the outage: nothing is lost while the slave writer blocks; on_ready
            // resumes.
        }
        handle_ready();
    });
}

void serial_bridge::resume_pty() {
    if (m_pty_paused) {
        m_pty.resume_rx();
        m_pty_paused = false;
    }
}

void serial_bridge::disconnect_cb_endpoint() {
    m_tcp_ready = false;
    m_tcp_last_error_id = -1;
    if (m_tcp) {
        m_tcp->reset();
    }
    m_tcp.reset();
    // No TCP client left to drain the buffer that caused a pause.
    resume_pty();
    handle_ready();
}

void serial_bridge::connect_cb_endpoint(std::string const& remote) {
    m_tcp_remote = remote;
    disconnect_cb_endpoint();
    create_tcp_client(m_tcp_remote, m_tcp_port);
    handle_ready();
}

std::string serial_bridge::get_slave_path() {
    return m_pty.get_slave_path();
}

bool serial_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.register_event_handler(&m_pty) && result;
    result = handler.register_event_handler(m_tcp.get()) && result;
    return result;
}

bool serial_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.unregister_event_handler(&m_pty) && result;
    result = handler.unregister_event_handler(m_tcp.get()) && result;
    return result;
}

void serial_bridge::handle_ready() {
    m_ready.set(m_tcp_ready and m_pty_ready);
}

bool serial_bridge::available() const {
    return m_ready;
}

} // namespace charge_bridge
