// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <charge_bridge/everest_api/ev_bsp_api.hpp>
#include <charge_bridge/everest_api/evse_bsp_api.hpp>
#include <charge_bridge/everest_api/ovm_api.hpp>
#include <chrono>
#include <cstdint>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/mqtt/mqtt_client.hpp>
#include <everest/util/misc/observable.hpp>
#include <everest_api_types/evse_board_support/API.hpp>
#include <everest_api_types/evse_manager/API.hpp>
#include <everest_api_types/utilities/Topics.hpp>
#include <functional>
#include <protocol/cb_common.h>
#include <protocol/evse_bsp_cb_to_host.h>
#include <protocol/evse_bsp_host_to_cb.h>
#include <string>
#include <vector>

namespace charge_bridge::evse_bsp {

namespace API_BSP = everest::lib::API::V1_0::types::evse_board_support;

struct everest_api_config {
    std::string mqtt_remote;
    std::string mqtt_bind;
    uint16_t mqtt_port;
    uint32_t mqtt_ping_interval_ms;
    evse_bsp_config evse;
    evse_ovm_config ovm;
    evse_ev_bsp_config ev;
};

class api_connector : public everest::lib::io::event::fd_event_register_interface {
    using tx_ftor = std::function<void(evse_bsp_host_to_cb const&)>;
    using rx_ftor = std::function<void(evse_bsp_cb_to_host const&)>;
    using error_ftor = std::function<void(bool /*status [true=noerror, false=error]*/)>;

public:
    using mqtt_message = everest::lib::io::mqtt::mqtt_client::message;

    api_connector(everest_api_config const& config, std::string const& cb_identifier);
    void set_cb_tx(tx_ftor const& handler);
    void set_cb_message(evse_bsp_cb_to_host const& msg);
    // Heartbeat-verified connection state. On the up edge the host status goes out at once: the MCU
    // opens its BSP slot only after a host packet, so waiting for the sync tick would delay the first
    // BSP packet, and with it the connected report, by up to one tick.
    void notify_cb_connection(bool connected);
    // The clear for every error this connector may have raised, rendered instead of sent. A connector
    // that is about to be destroyed cannot send anything: libmosquitto only queues a publish, and the
    // queue is drained by the event loop, which the connector is not registered with by the time it
    // is replaced. The successor sends them, see publish_once_connected.
    std::vector<mqtt_message> render_clear_messages();
    // Publishes the handed-over messages, once, on the next MQTT connect (which is also the first:
    // the connection is only established once the connector is registered with the event loop).
    void publish_once_connected(std::vector<mqtt_message> messages);
    // CbLinkTechnology from the heartbeat link status; only the EVSE API acts on it so far.
    void set_link_technology(std::uint8_t technology);
    void forget_link_technology();
    void set_error_handler(error_ftor const& handler);

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;

private:
    void publish(std::string const& topic, std::string const& payload);
    void clear_raised_errors();
    void handle_mqtt_connect();
    void handle_cb_connection_state();
    void sync_cb_connection_state();
    bool check_cb_heartbeat();

    std::string m_cb_identifier;
    everest::lib::io::mqtt::mqtt_client m_mqtt;
    tx_ftor m_tx;
    std::chrono::steady_clock::time_point m_last_cb_heartbeat;
    everest::lib::io::event::timer_fd m_sync_timer;
    // Set while render_clear_messages() runs: publish() records into it instead of sending.
    std::vector<mqtt_message>* m_render_sink{nullptr};
    std::vector<mqtt_message> m_publish_once_connected;

    std::string m_evse_bsp_receive_topic;
    std::string m_evse_bsp_send_topic;
    std::string m_ovm_receive_topic;
    std::string m_ovm_send_topic;
    std::string m_ev_bsp_receive_topic;
    std::string m_ev_bsp_send_topic;
    bool m_evse_bsp_enabled{false};
    bool m_ovm_enabled{false};
    bool m_ev_bsp_enabled{false};
    bool m_cb_initial_comm_check{true};
    bool m_cb_connected{false};
    evse_bsp_host_to_cb m_host_status;

    evse_bsp_api m_evse_bsp;
    ovm_api m_ovm;
    ev_bsp_api m_ev_bsp;
    everest::lib::util::observable<bool> m_ready{false};
};
} // namespace charge_bridge::evse_bsp
