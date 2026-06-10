// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "protocol/cb_config.h"
#include <charge_bridge/charge_bridge.hpp>
#include <charge_bridge/discovery.hpp>
#include <charge_bridge/firmware_update/sync_fw_updater.hpp>
#include <charge_bridge/gpio_bridge.hpp>
#include <charge_bridge/heartbeat_service.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/print_config.hpp>
#include <charge_bridge/utilities/string.hpp>
#include <charge_bridge/utilities/sync_udp_client.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/netlink/vcan_netlink_manager.hpp>
#include <everest/util/misc/bind.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

namespace charge_bridge {

namespace {
std::pair<bool, std::set<std::string>> make_interface_list(std::string const& str, std::string const& pattern) {
    if (str == pattern) {
        return {false, {}};
    };
    auto raw = utilities::string_after_pattern(str, pattern).substr(1);
    if (raw.size() <= 2) {
        return {false, {}};
    }
    auto exclude = raw.substr(0, 1) == "!";
    auto items = utilities::csv_to_set(raw.substr(exclude ? 1 : 0));
    for (auto const& elem : items) {
        std::cout << elem << ", ";
    }
    std::cout << std::endl;
    return {exclude, items};
}

const int mqtt_reconnect_timeout_ms = 1000;

} // namespace

charge_bridge::charge_bridge(charge_bridge_config const& config) : m_config(config) {
    std::cout << "CB CONSTRUCT" << std::endl;

    if (utilities::string_starts_with(config.cb_remote, "ANY_EVSE")) {
        auto params = make_interface_list(config.cb_remote, "ANY_EVSE");
        init_discovery(discovery_device_type::CB_EVSE, params.second, params.first);
    } else if (utilities::string_starts_with(config.cb_remote, "ANY_EV")) {
        auto params = make_interface_list(config.cb_remote, "ANY_EV");
        init_discovery(discovery_device_type::CB_EV, params.second, params.first);
    } else {
        init();
    }
}

void charge_bridge::init_discovery(discovery_device_type type, std::set<std::string> const& interfaces,
                                   bool excluding) {
    using namespace everest::lib::util;
    utilities::print_error(m_config.cb_name, "DISCOVERY", -1) << "Discovery pending" << std::endl;

    m_discovery = std::make_unique<discovery>(type, interfaces, excluding);
    m_discovery->set_discovery_callback(bind_obj(&charge_bridge::handle_discovery, this));
    {
        auto handle = m_cb_status.handle();
        handle->discovery_pending = true;
    }
    m_cb_status.notify_one();
}

void charge_bridge::handle_discovery(std::string const& ip) {
    utilities::print_error(m_config.cb_name, "DISCOVERY", 0) << "Discovered at: " + ip << std::endl;

    m_config.cb_remote = ip;
    if (m_config.can0) {
        m_config.can0->cb_remote = ip;
    }
    if (m_config.serial1) {
        m_config.serial1->cb_remote = ip;
    }
    if (m_config.serial2) {
        m_config.serial2->cb_remote = ip;
    }
    if (m_config.serial3) {
        m_config.serial3->cb_remote = ip;
    }
    if (m_config.plc) {
        m_config.plc->cb_remote = ip;
    }
    if (m_config.bsp) {
        m_config.bsp->cb_remote = ip;
    }
    if (m_config.heartbeat) {
        m_config.heartbeat->cb_remote = ip;
    }
    if (m_config.gpio) {
        m_config.gpio->cb_remote = ip;
    }
    m_config.firmware.cb_remote = ip;

    m_event_handler->add_action([this]() {
        std::unique_ptr<discovery> tmp;
        std::swap(m_discovery, tmp);

        init();
        {
            auto handle = m_cb_status.handle();
            handle->discovery_pending = false;
        }
        m_cb_status.notify_one();
    });
}

void charge_bridge::init() {
    if (m_config.can0.has_value()) {
        m_can_0_client = std::make_unique<can_bridge>(m_config.can0.value(), m_ready_notify);
    }
    if (m_config.serial1.has_value()) {
        m_pty_1 = std::make_unique<serial_bridge>(m_config.serial1.value(), m_ready_notify);
    }
    if (m_config.serial2.has_value()) {
        m_pty_2 = std::make_unique<serial_bridge>(m_config.serial2.value(), m_ready_notify);
    }
    if (m_config.serial3.has_value()) {
        m_pty_3 = std::make_unique<serial_bridge>(m_config.serial3.value(), m_ready_notify);
    }
    if (m_config.plc.has_value()) {
        m_plc = std::make_unique<plc_bridge>(m_config.plc.value(), m_ready_notify);
    }
    if (m_config.bsp.has_value()) {
        m_bsp = std::make_unique<bsp_bridge>(m_config.bsp.value(), m_ready_notify);
    }
    if (m_config.gpio.has_value()) {
        m_gpio = std::make_unique<gpio_bridge>(m_config.gpio.value(), m_ready_notify);
    }
    if (m_config.heartbeat.has_value()) {
        m_heartbeat = std::make_unique<heartbeat_service>(
            m_config.heartbeat.value(),
            [this](bool connected) {
                {
                    auto handle = m_cb_status.handle();
                    handle->is_connected = connected;
                }
                if(m_plc){
                    m_plc->set_cb_connection_status(connected);
                }
                if(m_gpio){
                    m_gpio->set_cb_connection_status(connected);
                }

                m_cb_status.notify_one();
            },
            m_ready_notify);
    }
}

charge_bridge::~charge_bridge() {
    m_cb_status.notify_one();
}

void charge_bridge::manage(everest::lib::io::event::fd_event_handler& handler, std::atomic_bool const& run,
                           bool force_update) {
    using namespace std::chrono_literals;
    m_event_handler = &handler;
    m_force_firmware_update = force_update;

    m_event_handler->add_action([this]() {
        if (m_config.telemetry.has_value()) {
            m_1s_tick.disarm();
            m_mqtt = std::make_unique<everest::lib::io::mqtt::mqtt_client>(mqtt_reconnect_timeout_ms);
            m_mqtt->connect(m_config.telemetry->mqtt_bind, m_config.telemetry->mqtt_remote,
                            m_config.telemetry->mqtt_port, m_config.telemetry->mqtt_ping_interval_ms);

            m_mqtt->set_callback_connect(
                [this](auto&, auto, auto, auto const&) { m_1s_tick.set_timeout(std::chrono::seconds(1)); });
            register_manage_events(*m_event_handler);
        }
    });

    auto action = [this](bool is_connected, bool discovery_pending, int& error_count) {
        if (discovery_pending) {
            if (m_discovery_active) {
                return;
            }
            m_discovery_active = true;
            m_event_handler->add_action([this]() { register_internal_events(*m_event_handler); });
            return;
        }
        if (m_was_connected and not is_connected) {
            if (error_count > 1) {
                m_event_handler->add_action([this]() { unregister_internal_events(*m_event_handler); });
                m_was_connected = false;
            } else {
                error_count++;
            }
        }
        if (not m_was_connected) {
            if (update_firmware(m_force_firmware_update)) {
                m_event_handler->add_action([this]() { register_internal_events(*m_event_handler); });
                m_was_connected = true;
                error_count = 0;
            }
        }
    };

    std::thread manager([&run, action, this]() {
        auto handle = m_cb_status.handle();
        bool last_is_connected = handle->is_connected;
        bool last_discovery_pending = handle->discovery_pending;
        int error_count = 0;
        auto condition = [&] {
            if (handle->is_connected not_eq last_is_connected) {
                return true;
            }
            if (handle->discovery_pending not_eq last_discovery_pending) {
                return true;
            }
            if (not run.load()) {
                return true;
            }
            return false;
        };
        while (run.load()) {
            action(handle->is_connected, handle->discovery_pending, error_count);
            handle.wait_for(condition, 10s);
            last_is_connected = handle->is_connected;
            last_discovery_pending = handle->discovery_pending;
        }
    });
    manager.detach();
}

bool charge_bridge::update_firmware(bool force) {
    firmware_update::sync_fw_updater updater(m_config.firmware);
    auto is_connected = updater.quick_check_connection();
    if (not is_connected) {
        return false;
    }
    updater.print_fw_version();

    auto do_update = force or (m_config.firmware.fw_update_on_start and not updater.check_if_correct_fw_installed());

    if (not do_update) {
        return true;
    }
    auto result = updater.upload_fw() && updater.check_connection();
    if (not result) {
        std::cout << "Error: could not install correct firmware version" << std::endl;
    }
    return result;
}

std::string charge_bridge::get_pty_1_slave_path() {
    if (m_pty_1) {
        return m_pty_1->get_slave_path();
    }
    return "";
}

std::string charge_bridge::get_pty_2_slave_path() {
    if (m_pty_2) {
        return m_pty_2->get_slave_path();
    }
    return "";
}

std::string charge_bridge::get_pty_3_slave_path() {
    if (m_pty_3) {
        return m_pty_3->get_slave_path();
    }
    return "";
}

void charge_bridge::handle_ready() {
    if (not m_config.telemetry.has_value()) {
        return;
    }

    bool status = true;
    auto publish = [this](std::string_view component, std::string_view item, bool status) {
        std::stringstream topic;
        topic << m_config.telemetry->telemetry_topic << "/" << m_config.cb_name << "/" << component << "/" << item;
        std::string_view payload = status ? "true" : "false";
        m_mqtt->publish(topic.str(), payload);
    };

    {
        auto handle = m_cb_status.handle();
        publish("chargebridge", "connected", handle->is_connected);
        publish("chargebridge", "discovered", not handle->discovery_pending);
        status = not handle->discovery_pending;
    }

    auto publish_status = [publish](std::string_view component, bool status) { publish(component, "status", status); };
    if (m_can_0_client) {
        auto available = m_can_0_client->available();
        status = status && available;
        publish_status("can_0", available);
    }
    if (m_pty_1) {
        auto available = m_pty_1->available();
        status = status && available;
        publish_status("serial_1", available);
    }
    if (m_pty_2) {
        auto available = m_pty_2->available();
        status = status && available;
        publish_status("serial_2", available);
    }
    if (m_pty_3) {
        auto available = m_pty_3->available();
        status = status && available;
        publish_status("serial_3", available);
    }
    if (m_bsp) {
        auto available = m_bsp->available();
        status = status && available;
        publish_status("bsp", available);
    }
    if (m_plc) {
        auto available = m_plc->available();
        status = status && available;
        publish_status("plc", available);
    }
    if (m_heartbeat) {
        auto available = m_heartbeat->available();
        status = status && available;
        publish_status("heatbeat", available);
    }
    if (m_gpio) {
        auto available = m_gpio->available();
        status = status && available;
        publish_status("gpio", available);
    }
    publish_status("chargebridge", status);
}

void charge_bridge::handle_tick() {
    handle_ready();
}

bool charge_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    std::cout << "register Events" << std::endl;
    auto result = true;
    result = register_internal_events(handler) && result;
    result = register_manage_events(handler) && result;

    return result;
}
bool charge_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    std::cout << "UNregister Events" << std::endl;
    auto result = true;
    result = unregister_internal_events(handler) && result;
    result = unregister_manage_events(handler) && result;

    return result;
}
bool charge_bridge::register_manage_events(everest::lib::io::event::fd_event_handler& handler) {
    std::cout << "register manage Events" << std::endl;
    auto result = true;
    result =
        handler.register_event_handler(&m_1s_tick, everest::lib::util::bind_obj(&charge_bridge::handle_tick, this)) &&
        result;

    if (m_mqtt) {
        result = handler.register_event_handler(m_mqtt.get()) && result;
        result = handler.register_event_handler(&m_ready_notify,
                                                everest::lib::util::bind_obj(&charge_bridge::handle_ready, this)) &&
                 result;
    }

    return result;
}
bool charge_bridge::unregister_manage_events(everest::lib::io::event::fd_event_handler& handler) {
    std::cout << "UNregister manage Events" << std::endl;
    auto result = true;
    result = handler.unregister_event_handler(&m_1s_tick) && result;
    if (m_mqtt) {
        result = handler.unregister_event_handler(m_mqtt.get()) && result;
        result = handler.unregister_event_handler(&m_ready_notify) && result;
    }

    return result;
}

bool charge_bridge::register_internal_events(everest::lib::io::event::fd_event_handler& handler) {
    std::cout << "register INTERNAL Events" << std::endl;
    auto result = true;
    if (m_can_0_client) {
        result = handler.register_event_handler(m_can_0_client.get()) && result;
    }
    if (m_pty_1) {
        result = handler.register_event_handler(m_pty_1.get()) && result;
    }
    if (m_pty_2) {
        result = handler.register_event_handler(m_pty_2.get()) && result;
    }
    if (m_pty_3) {
        result = handler.register_event_handler(m_pty_3.get()) && result;
    }
    if (m_bsp) {
        result = handler.register_event_handler(m_bsp.get()) && result;
    }
    if (m_plc) {
        result = handler.register_event_handler(m_plc.get()) && result;
    }
    if (m_heartbeat) {
        result = handler.register_event_handler(m_heartbeat.get()) && result;
    }
    if (m_gpio) {
        result = handler.register_event_handler(m_gpio.get()) && result;
    }
    if (m_discovery) {
        result = handler.register_event_handler(m_discovery.get()) && result;
    }

    return result;
}
bool charge_bridge::unregister_internal_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    std::cout << "UNregister INTERNAL Events" << std::endl;

    if (m_can_0_client) {
        result = handler.unregister_event_handler(m_can_0_client.get()) && result;
    }
    if (m_pty_1) {
        result = handler.unregister_event_handler(m_pty_1.get()) && result;
    }
    if (m_pty_2) {
        result = handler.unregister_event_handler(m_pty_2.get()) && result;
    }
    if (m_pty_3) {
        result = handler.unregister_event_handler(m_pty_3.get()) && result;
    }
    if (m_bsp) {
        result = handler.unregister_event_handler(m_bsp.get()) && result;
    }
    if (m_plc) {
        result = handler.unregister_event_handler(m_plc.get()) && result;
    }
    if (m_heartbeat) {
        result = handler.unregister_event_handler(m_heartbeat.get()) && result;
    }
    if (m_gpio) {
        result = handler.unregister_event_handler(m_gpio.get()) && result;
    }
    if (m_discovery) {
        result = handler.unregister_event_handler(m_discovery.get()) && result;
    }

    return result;
}

void charge_bridge::print_config() {
    print_charge_bridge_config(m_config);
}

void print_charge_bridge_config(charge_bridge_config const& c) {
    using namespace utilities;
    std::cout << "ChargeBridge: " << c.cb_name << std::endl;
    std::cout << " * remote:    " << c.cb_remote << std::endl;
    if (c.serial1) {
        std::cout << " * serial 1:  " << c.serial1->serial_device;
        if (c.heartbeat.has_value() && CB_NUMBER_OF_UARTS >= 1) {
            std::cout << " " << to_string(c.heartbeat->cb_config.uarts[0]);
        }
        std::cout << std::endl;
    }
    if (c.serial2) {
        std::cout << " * serial 2:  " << c.serial2->serial_device;
        if (c.heartbeat.has_value() && CB_NUMBER_OF_UARTS >= 2) {
            std::cout << " " << to_string(c.heartbeat->cb_config.uarts[1]);
        }
        std::cout << std::endl;
    }
    if (c.serial3) {
        std::cout << " * serial 3:  " << c.serial3->serial_device;
        if (c.heartbeat.has_value() && CB_NUMBER_OF_UARTS >= 3) {
            std::cout << " " << to_string(c.heartbeat->cb_config.uarts[2]);
        }
        std::cout << std::endl;
    }
    if (c.can0) {
        std::cout << " * can 0:     " << c.can0->can_device;
        if (c.heartbeat.has_value()) {
            std::cout << " " << to_string(c.heartbeat->cb_config.can.baudrate) << "bps" << std::endl;
        }
    }
    if (c.plc) {
        std::cout << " * plc:       " << c.plc->plc_tap << std::flush;
        std::cout << " " << c.cb_remote << ":" << c.plc->cb_port;
        std::cout << " adress " << c.plc->plc_ip;
        std::cout << " netmask " << c.plc->plc_netmaks;
        std::cout << " MTU " << c.plc->plc_mtu << std::endl;
    }
    if (c.bsp) {
        if (c.bsp->api.evse.enabled) {
            std::cout << " * evse_bsp:  ";
        } else if (c.bsp->api.ev.enabled) {
            std::cout << " * ev_bsp:    ";
        }
        std::cout << c.bsp->cb_remote << ":" << c.bsp->cb_port;
        std::cout << " module " << c.bsp->api.evse.module_id;
        std::cout << " MQTT " << c.bsp->api.mqtt_remote << ":" << c.bsp->api.mqtt_port;
        if (not c.bsp->api.mqtt_bind.empty()) {
            std::cout << " on " << c.bsp->api.mqtt_bind;
        }
        std::cout << " ping " << c.bsp->api.mqtt_ping_interval_ms << "ms";
        if (c.bsp->api.ovm.enabled) {
            std::cout << " OVM module " << c.bsp->api.ovm.module_id;
        }
        std::cout << std::endl;
    }
    if (c.heartbeat) {
        std::cout << " * heartbeat: " << c.cb_remote << ":" << c.cb_port;
        std::cout << " heartbeat interval " << c.heartbeat->interval_s << "s" << std::endl;
    }
    if (c.gpio) {
        std::cout << " * gpio:      " << c.cb_remote << ":" << c.cb_port;
        std::cout << " MQTT " << c.gpio->mqtt_remote << ":" << c.gpio->mqtt_port;
        if (not c.gpio->mqtt_bind.empty()) {
            std::cout << " on " << c.gpio->mqtt_bind;
        }
        std::cout << " send interval " << c.gpio->interval_s << "s" << std::endl;
    }

    std::cout << "\n" << std::endl;
}

} // namespace charge_bridge
