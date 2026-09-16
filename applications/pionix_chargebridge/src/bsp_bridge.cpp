// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <charge_bridge/bsp_bridge.hpp>
#include <charge_bridge/mcs_bsp.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <cstring>
#include <everest/io/udp/udp_payload.hpp>
#include <iostream>
#include <protocol/evse_bsp_cb_to_host.h>
#include <protocol/evse_bsp_host_to_cb.h>

namespace {
const char* cp_state_to_string(CpState state) {
    switch (state) {
    case CpState_A:
        return "A";
    case CpState_B:
        return "B";
    case CpState_C:
        return "C";
    case CpState_D:
        return "D";
    case CpState_E:
        return "E";
    case CpState_F:
        return "F";
    case CpState_DF:
        return "DF";
    case CpState_INVALID:
    default:
        return "INVALID";
    }
}
} // namespace

namespace charge_bridge {

bsp_bridge::bsp_bridge(bsp_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify) :
    m_api(config.api, config.cb + "/" + config.item),
    m_identifier(config.cb + "/" + config.item),
    m_udp_port(config.cb_port),
    m_udp_remote(config.cb_remote),
    m_ready_notify(ready_notify) {
    using namespace std::chrono_literals;
    m_timer.set_timeout(5s);
    auto identifier = config.cb + "/" + config.item;
    m_identifier = identifier;
    create_udp_client(config.cb_remote, config.cb_port, identifier);

    m_api.set_cb_tx([this](auto& data) {
        if (m_udp) {
            everest::lib::io::udp::udp_payload pl;
            pl.set_message(&data, sizeof(data));
            m_udp->tx(pl);
        }
    });

    m_api.set_error_handler([this](bool good) {
        m_api_good = good;
        handle_status();
    });

    m_ready.setCallback([this](auto&, auto&) { m_ready_notify.notify(); });
}

void bsp_bridge::create_udp_client(std::string const& remote, uint16_t remote_port, std::string const& identifier) {
    m_udp = std::make_unique<everest::lib::io::udp::udp_client>(remote, remote_port);
    m_udp_ready = false;
    m_udp_on_error = false;
    m_udp->set_rx_handler([this](auto const& data, auto&) {
        // The datagram must carry exactly one fixed-size packet. Drop anything else: a larger
        // datagram would otherwise overflow the stack struct, a shorter one would leave it
        // partially uninitialized and publish garbage CP state. (UDP is unauthenticated, so this
        // is reachable from any local sender.)
        if (data.size() != sizeof(evse_bsp_cb_to_host)) {
            return;
        }
        evse_bsp_cb_to_host msg;
        std::memcpy(&msg, data.buffer.data(), sizeof(msg));
        m_cp_state = cp_state_to_string(msg.cp_state);
        // MCS basic-signalling diagnostics. Recorded only when the MCU actually reports them: a CCS
        // board sends NotApplicable for both, and a line saying "n/a" in every CCS dashboard is
        // noise. lock_state follows the same rule - it is UNDEFINED unless lock hardware is fitted.
        if (msg.ce_state not_eq CeState_NotApplicable) {
            m_ce_state = ce_state_name(msg.ce_state);
        }
        if (msg.id_state not_eq IdState_NotApplicable) {
            m_id_state = id_state_name(msg.id_state);
        }
        if (msg.lock_state not_eq LockState_UNDEFINED) {
            m_lock_state = lock_state_name(msg.lock_state);
        }
        // Raw value, every packet: the listener (the plc bridge's carrier gate) dedups itself, and
        // NotApplicable must reach it too - fail-safe there is "not mated".
        if (m_ce_state_listener) {
            m_ce_state_listener(msg.ce_state);
        }
        m_api.set_cb_message(msg);
    });
    m_udp->set_error_handler([this, identifier](auto id, auto const& msg) {
        utilities::print_error(identifier, "BSP/UDP", id) << msg << std::endl;
        m_udp_ready = id == 0;
        m_udp_on_error = id not_eq 0;
        if (not m_udp_ready and m_udp) {
            m_udp->reset();
        }
        handle_status();
    });
}

void bsp_bridge::disconnect_cb_endpoint() {
    m_udp_ready = false;
    m_udp_on_error = true;
    // Everything below describes the device that was on the other end, and connect_cb_endpoint comes
    // through here, so the next one may be a different board. The latched board class and the MCS
    // diagnostics are therefore dropped: keeping them would let one device's connector type decide
    // whether another device publishes PP, and would leave a stale CE/ID reading on the dashboard.
    m_api.forget_link_technology();
    m_ce_state.reset();
    m_id_state.reset();
    m_lock_state.reset();
    // The stale-CE rule applies to the carrier gate exactly as it does to the dashboard fields
    // above: the next endpoint may be a different board, whose mate must be proven afresh.
    if (m_ce_state_listener) {
        m_ce_state_listener(CeState_NotApplicable);
    }
    if (m_udp) {
        m_udp->reset();
    }
    m_udp.reset();
    handle_status();
}

void bsp_bridge::connect_cb_endpoint(std::string const& remote) {
    m_udp_remote = remote;
    disconnect_cb_endpoint();
    create_udp_client(m_udp_remote, m_udp_port, m_identifier);
    handle_status();
}

void bsp_bridge::handle_timer_event() {
    if (m_udp_on_error) {
        if (m_udp) {
            m_udp->reset();
        }
    }
}

void bsp_bridge::handle_status() {
    auto status = m_api_good && m_udp_ready && not m_udp_on_error;
    m_ready.set(status);
}

bool bsp_bridge::available() const {
    return m_ready;
}

std::optional<std::string> bsp_bridge::cp_state() const {
    return m_cp_state;
}

std::optional<std::string> bsp_bridge::ce_state() const {
    return m_ce_state;
}

void bsp_bridge::set_ce_state_listener(std::function<void(std::uint8_t)> listener) {
    m_ce_state_listener = std::move(listener);
}

std::optional<std::string> bsp_bridge::id_state() const {
    return m_id_state;
}

std::optional<std::string> bsp_bridge::lock_state() const {
    return m_lock_state;
}

void bsp_bridge::set_link_technology(std::uint8_t technology) {
    m_api.set_link_technology(technology);
}

bool bsp_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.register_event_handler(&m_api) && result;
    result = handler.register_event_handler(m_udp.get()) && result;
    result = handler.register_event_handler(&m_timer, [this](auto&) { handle_timer_event(); }) && result;
    return result;
}

bool bsp_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.unregister_event_handler(&m_api) && result;
    result = handler.unregister_event_handler(m_udp.get()) && result;
    result = handler.unregister_event_handler(&m_timer) && result;
    return result;
}

} // namespace charge_bridge
