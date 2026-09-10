// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <charge_bridge/everest_api/api_connector.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/util/misc/observable.hpp>
#include <everest_api_types/evse_board_support/API.hpp>
#include <memory>
#include <optional>
#include <string>

namespace charge_bridge {

struct bsp_bridge_config {
    std::string cb;
    std::string item;
    std::uint16_t cb_port;
    std::string cb_remote;
    evse_bsp::everest_api_config api;
};

class bsp_bridge : public everest::lib::io::event::fd_event_register_interface {
public:
    bsp_bridge(bsp_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify);
    ~bsp_bridge() = default;

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;
    void disconnect_cb_endpoint();
    void connect_cb_endpoint(std::string const& remote);
    bool available() const;
    // Latest CP state reported by the MCU ("A".."F", "DF", "INVALID"); empty until the first packet.
    std::optional<std::string> cp_state() const;

private:
    void handle_timer_event();
    void handle_status();
    void create_udp_client(std::string const& remote, uint16_t remote_port, std::string const& identifier);
    bool m_api_good{false};

    evse_bsp::api_connector m_api;
    std::unique_ptr<everest::lib::io::udp::udp_client> m_udp;
    std::string m_identifier;
    bool m_udp_ready{false};
    std::uint16_t m_udp_port{0};
    std::string m_udp_remote;
    everest::lib::io::event::timer_fd m_timer;
    bool m_udp_on_error{false};
    std::optional<std::string> m_cp_state;
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::io::event::event_fd& m_ready_notify;
};

} // namespace charge_bridge
