// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <charge_bridge/bsp_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <cstring>
#include <everest/io/udp/udp_payload.hpp>
#include <iomanip>
#include <iostream>
#include <protocol/evse_bsp_cb_to_host.h>
#include <protocol/evse_bsp_host_to_cb.h>
#include <sstream>

namespace {
const int default_udp_timeout_ms = 1000;
}

namespace charge_bridge {

bsp_bridge::bsp_bridge(bsp_bridge_config const& config) :
    m_api(config.api, config.cb + "/" + config.item), m_udp(config.cb_remote, config.cb_port, default_udp_timeout_ms) {
    using namespace std::chrono_literals;
    m_timer.set_timeout(5s);

    auto rx_identifier = config.cb + "/" + config.item;

    m_api.set_cb_tx([this](auto& data) {
        everest::lib::io::udp::udp_payload pl;
        pl.set_message(&data, sizeof(data));
        m_udp.tx(pl);
    });

    m_udp.set_rx_handler([this, rx_identifier](auto const& data, auto&) {
        // Diagnostic: trace the raw packet from the hardware module. The struct is
        // memcpy'd below without a size check, so a short/oversized frame would leave
        // trailing fields (e.g. stop_charging, cp_duty_cycle) as stack garbage.
        const auto received = data.size();
        const auto expected = sizeof(evse_bsp_cb_to_host);
        std::ostringstream hex;
        hex << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < received; ++i) {
            hex << std::setw(2) << static_cast<unsigned>(data.buffer[i]) << ' ';
        }
        if (received != expected) {
            utilities::print_error(rx_identifier, "BSP/UDP", -1)
                << "RX size mismatch: received " << received << " bytes, expected " << expected
                << " bytes. Raw: " << hex.str() << std::endl;
        } else {
            utilities::print_error(rx_identifier, "BSP/UDP", 0)
                << "RX " << received << " bytes: " << hex.str() << std::endl;
        }

        evse_bsp_cb_to_host msg;
        std::memcpy(&msg, data.buffer.data(), data.size());
        m_api.set_cb_message(msg);
    });

    auto identifier = config.cb + "/" + config.item;
    m_udp.set_error_handler([this, identifier](auto id, auto const& msg) {
        utilities::print_error(identifier, "BSP/UDP", id) << msg << std::endl;
        m_udp_on_error = id not_eq 0;
    });
}

void bsp_bridge::handle_timer_event() {
    if (m_udp_on_error) {
        m_udp.reset();
    }
}

bool bsp_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.register_event_handler(&m_api) && result;
    result = handler.register_event_handler(&m_udp) && result;
    result = handler.register_event_handler(&m_timer, [this](auto&) { handle_timer_event(); }) && result;
    return result;
}

bool bsp_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.unregister_event_handler(&m_api) && result;
    result = handler.unregister_event_handler(&m_udp) && result;
    result = handler.unregister_event_handler(&m_timer) && result;
    return result;
}

} // namespace charge_bridge
