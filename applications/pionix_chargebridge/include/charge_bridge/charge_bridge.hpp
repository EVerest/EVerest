// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "everest/io/event/timer_fd.hpp"
#include <atomic>
#include <charge_bridge/bsp_bridge.hpp>
#include <charge_bridge/can_bridge.hpp>
#include <charge_bridge/discovery.hpp>
#include <charge_bridge/firmware_update/sync_fw_updater.hpp>
#include <charge_bridge/heartbeat_service.hpp>
#include <charge_bridge/io_bridge.hpp>
#include <charge_bridge/plc_bridge.hpp>
#include <charge_bridge/serial_bridge.hpp>
#include <charge_bridge/utilities/print_status.hpp>
#include <charge_bridge/utilities/symlink.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/mqtt/mosquitto_cpp.hpp>
#include <everest/io/serial/event_pty.hpp>
#include <everest/io/tun_tap/tap_client.hpp>
#include <everest/util/async/monitor.hpp>

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <thread>

namespace charge_bridge {

struct charge_bridge_status {
    bool is_connected{false};
    bool discovery_pending{false};
};

struct telemetry_config {
    std::string cb;
    std::string item;
    std::string mqtt_remote;
    std::string mqtt_bind;
    std::uint32_t mqtt_ping_interval_ms;
    std::uint16_t mqtt_port;
    std::string telemetry_topic;
};

struct charge_bridge_config {
    std::string cb_name;
    std::uint16_t cb_port;
    std::string cb_remote;
    std::optional<telemetry_config> telemetry;
    std::optional<can_bridge_config> can0;
    std::optional<serial_bridge_config> serial1;
    std::optional<serial_bridge_config> serial2;
    std::optional<serial_bridge_config> serial3;
    std::optional<plc_bridge_config> plc;
    std::optional<bsp_bridge_config> bsp;
    std::optional<heartbeat_config> heartbeat;
    std::optional<io_config> io;
    firmware_update::fw_update_config firmware;
};

enum class endpoint_intent {
    fixed_ip,
    any_evse_mdns,
    any_ev_mdns,
};

struct endpoint_intent_info {
    endpoint_intent value{endpoint_intent::fixed_ip};
    std::set<std::string> interfaces;
    bool excluding_interfaces{false};
};

void print_charge_bridge_config(charge_bridge_config const& config);

class charge_bridge : public everest::lib::io::event::fd_event_register_interface {
public:
    charge_bridge(charge_bridge_config const& config,
                  std::function<void(utilities::chargebridge_status)> status_sink = {},
                  std::function<void(utilities::chargebridge_status)> tick_sink = {});
    ~charge_bridge();

    bool update_firmware(bool force);

    std::string get_pty_1_slave_path();
    std::string get_pty_2_slave_path();
    std::string get_pty_3_slave_path();

    void print_config();

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;

    void manage(everest::lib::io::event::fd_event_handler& handler, std::atomic_bool const& exit, bool force_update);

private:
    std::future<bool> start_internal_runtime();
    void create_internal_runtime();
    void cleanup_internal_runtime();
    void disconnect_internal_runtime_endpoints();
    bool unregister_internal_runtime_events(everest::lib::io::event::fd_event_handler& handler);
    std::future<bool> stop_internal_runtime();
    void init_discovery(discovery_device_type type, std::set<std::string> const& interfaces, bool excluding);
    bool is_mdns_endpoint() const;
    discovery_device_type mdns_device_type() const;
    std::set<std::string> select_discovery_interfaces() const;
    void start_discovery_attempt(std::set<std::string> const& interfaces);
    void stop_discovery();
    void set_discovery_pending(bool pending);
    void set_discovery_pending(charge_bridge_status& status, bool pending);
    void handle_discovery(everest::lib::io::mdns::mDNS_discovery const& info);
    void handle_ready();
    void handle_tick();
    bool register_internal_events(everest::lib::io::event::fd_event_handler& handler);
    bool unregister_internal_events(everest::lib::io::event::fd_event_handler& handler);
    bool register_manage_events(everest::lib::io::event::fd_event_handler& handler);
    bool unregister_manage_events(everest::lib::io::event::fd_event_handler& handler);
    void publish_status(utilities::chargebridge_status const& status);
    utilities::chargebridge_status get_status();

private:
    std::unique_ptr<can_bridge> m_can_0_client;
    std::unique_ptr<serial_bridge> m_pty_1;
    std::unique_ptr<serial_bridge> m_pty_2;
    std::unique_ptr<serial_bridge> m_pty_3;
    std::unique_ptr<bsp_bridge> m_bsp;
    std::unique_ptr<plc_bridge> m_plc;
    std::unique_ptr<heartbeat_service> m_heartbeat;
    std::unique_ptr<io_bridge> m_io;
    std::unique_ptr<discovery> m_discovery;

    everest::lib::io::event::fd_event_handler* m_event_handler{nullptr};
    everest::lib::io::event::event_fd m_ready_notify;
    everest::lib::io::event::timer_fd m_1s_tick;
    bool m_force_firmware_update{false};
    everest::lib::util::monitor<charge_bridge_status> m_cb_status;
    bool m_was_connected{false};
    bool m_discovery_active{false};
    bool m_internal_runtime_started{false};
    std::thread m_manager;
    endpoint_intent_info m_endpoint_intent;
    // Network identity of the discovered endpoint (hostname, service instance, TXT records). Empty
    // for fixed-IP configs (no mDNS discovery); the IP itself always lives in m_config.cb_remote.
    std::optional<everest::lib::io::mdns::mDNS_discovery> m_discovery_info;
    std::function<void(utilities::chargebridge_status)> m_status_sink;
    std::function<void(utilities::chargebridge_status)> m_tick_sink;

    charge_bridge_config m_config;
    std::unique_ptr<everest::lib::io::mqtt::mqtt_client> m_mqtt;
};

} // namespace charge_bridge
