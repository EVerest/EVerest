// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "charge_bridge/utilities/string.hpp"
#include <charge_bridge/discovery.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <sys/socket.h>
#include <type_traits>

namespace charge_bridge {

namespace {
// The board_type TXT record. On CCS boards it is the hardware name the firmware derives from its
// strap pins (board_type_name() in the firmware's NonSecure main.c); both CCS EVSE variants (LU
// and QCA modem) answer ANY_EVSE. MCS hardware has no role strapping, so there the firmware
// announces the role it boots with (persisted in its EEPROM identity, overridable once per boot
// by charge_bridge.type): CB-MCS-EVSE or CB-MCS-EV, and the neutral CB-MCS while it has never
// been provisioned, which either intent accepts so a fresh board can be reached by the host
// that is about to provision it. A board provisioned for the other role is invisible to this
// instance until it has been re-provisioned once with a fixed address.
bool is_cb_match(std::string const& board_type, discovery_device_type discriminator) {
    if (board_type == "CB-MCS") {
        return true;
    }
    switch (discriminator) {
    case discovery_device_type::CB_EV:
        return board_type == "CB-CCS-EV-LU" or board_type == "CB-MCS-EV";
    case discovery_device_type::CB_EVSE:
        return board_type == "CB-CCS-EVSE-LU" or board_type == "CB-CCS-EVSE-QCA" or board_type == "CB-MCS-EVSE";
    }
    return false;
}

} // namespace

const std::string discovery::discovery_id = "_chargebridge._udp.local";

discovery::discovery(discovery_device_type type, std::string instance_name) :
    m_type(type), m_instance_name(std::move(instance_name)) {
    using namespace std::chrono_literals;
    m_timer.set_timeout(1s);

    for (auto const& item : everest::lib::io::socket::get_all_interfaces()) {
        if (item.has_v4()) {
            add_client(item.name, AF_INET);
        }
        if (item.has_v6()) {
            add_client(item.name, AF_INET6);
        }
    }
}

discovery::discovery(discovery_device_type type, std::set<std::string> const& interfaces, bool excluding,
                     std::string instance_name) :
    m_type(type), m_instance_name(std::move(instance_name)) {
    using namespace std::chrono_literals;
    m_timer.set_timeout(1s);

    std::string used_interfaces;
    for (auto const& item : everest::lib::io::socket::get_all_interfaces()) {
        if (not interfaces.empty()) {
            if (interfaces.count(item.name) == 1 and excluding) {
                continue;
            }
            if (interfaces.count(item.name) == 0 and not excluding) {
                continue;
            }
        }
        if (not used_interfaces.empty()) {
            used_interfaces += ", ";
        }
        used_interfaces += item.name;
        if (item.has_v4()) {
            add_client(item.name, AF_INET);
        }
        if (item.has_v6()) {
            add_client(item.name, AF_INET6);
        }
    }

    // Discovery is restarted on every retry, so report the selected interfaces as a single line and
    // through print_info (not raw std::cout): in terminal mode it must land in the UI's message panel
    // instead of being painted over by the ftxui redraw.
    if (not used_interfaces.empty()) {
        utilities::print_info(m_instance_name, "DISCOVERY") << "using interfaces: " << used_interfaces << std::endl;
    }
}

void discovery::add_client(std::string const& interface, int family) {
    auto client = std::make_unique<everest::lib::io::mdns::mdns_client>(interface, family);
    // The socket is opened synchronously by the constructor. A family that is not
    // (or no longer) available on the interface must not abort the whole attempt.
    if (not client->get_raw_handler() or not client->get_raw_handler()->is_open()) {
        utilities::print_info(m_instance_name, "DISCOVERY")
            << "skipping " << (family == AF_INET6 ? "IPv6" : "IPv4") << " mdns on interface " << interface << std::endl;
        return;
    }
    client->set_rx_handler([this, interface](auto const& data, auto&) {
        auto discovery = everest::lib::io::mdns::parse_mdns_packet(data.buffer);
        if (discovery.has_value()) {
            auto& v6 = discovery->ipv6;
            if (not v6.empty() and v6.find('%') == std::string::npos and everest::lib::io::mdns::is_link_local_v6(v6)) {
                // Scope the link-local address to the interface it was heard on. The
                // registry is keyed by service_instance across interfaces, so for a
                // device on multiple links the last heard scope wins; each value is
                // valid on its own link and discovery tears down after first success.
                v6 += "%" + interface;
            }
            if (m_registry.update(discovery.value())) {
                query_registry();
            }
        }
    });
    m_mdns.push_back(std::move(client));
}

void discovery::query_registry() {
    auto obj = m_registry.get();
    for (auto const& [key, value] : obj) {
        if (not utilities::string_ends_with(key, discovery_id)) {
            continue;
        }
        if (not value.txt.count("board_type") or not is_cb_match(value.txt.at("board_type"), m_type)) {
            continue;
        }
        // Records of one announcement may be split across packets; only report an
        // entry once it carries a usable address (IPv4 preferred, IPv6 fallback).
        if (everest::lib::io::mdns::select_address(value).empty()) {
            continue;
        }
        if (not m_on_discover) {
            continue;
        }
        m_on_discover(value);
        return;
    }
}

void discovery::set_discovery_callback(discovery_cb const& cb) {
    m_on_discover = cb;
}

bool discovery::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    for (auto& item : m_mdns) {
        if (item) {
            result = handler.register_event_handler(item.get()) && result;
        }
    }
    handler.register_event_handler(&m_timer, [&](auto) {
        for (auto& item : m_mdns) {
            item->get_raw_handler()->query(discovery_id);
        }
    });

    return result;
}

bool discovery::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    for (auto& item : m_mdns) {
        if (item) {
            result = handler.unregister_event_handler(item.get()) && result;
        }
    }
    handler.unregister_event_handler(&m_timer);
    return result;
}

} // namespace charge_bridge
