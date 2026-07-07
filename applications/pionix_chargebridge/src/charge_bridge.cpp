// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "protocol/cb_config.h"
#include <charge_bridge/charge_bridge.hpp>
#include <charge_bridge/discovery.hpp>
#include <charge_bridge/firmware_update/sync_fw_updater.hpp>
#include <charge_bridge/heartbeat_service.hpp>
#include <charge_bridge/io_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/print_config.hpp>
#include <charge_bridge/utilities/string.hpp>
#include <charge_bridge/utilities/sync_udp_client.hpp>
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/netlink/vcan_netlink_manager.hpp>
#include <everest/io/socket/socket.hpp>
#include <everest/util/misc/bind.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

namespace charge_bridge {

namespace {
constexpr auto discovery_attempt_timeout = std::chrono::seconds(10);
constexpr auto discovery_retry_delay = std::chrono::seconds(1);
constexpr auto manager_base_cycle = std::chrono::seconds(10);

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

endpoint_intent_info parse_endpoint_intent(std::string const& cb_remote) {
    endpoint_intent_info result;

    if (utilities::string_starts_with(cb_remote, "ANY_EVSE")) {
        auto params = make_interface_list(cb_remote, "ANY_EVSE");
        result.value = endpoint_intent::any_evse_mdns;
        result.excluding_interfaces = params.first;
        result.interfaces = params.second;
    } else if (utilities::string_starts_with(cb_remote, "ANY_EV")) {
        auto params = make_interface_list(cb_remote, "ANY_EV");
        result.value = endpoint_intent::any_ev_mdns;
        result.excluding_interfaces = params.first;
        result.interfaces = params.second;
    }

    return result;
}

} // namespace

charge_bridge::charge_bridge(charge_bridge_config const& config,
                             std::function<void(utilities::chargebridge_status)> status_sink,
                             std::function<void(utilities::chargebridge_status)> tick_sink) :
    m_status_sink(std::move(status_sink)), m_tick_sink(std::move(tick_sink)), m_config(config) {
    m_endpoint_intent = parse_endpoint_intent(config.cb_remote);
}

void charge_bridge::init_discovery(discovery_device_type type, std::set<std::string> const& interfaces,
                                   bool excluding) {
    using namespace everest::lib::util;
    utilities::print_error(m_config.cb_name, "DISCOVERY", -1) << "Discovery pending" << std::endl;

    m_discovery = std::make_unique<discovery>(type, interfaces, excluding);
    m_discovery->set_discovery_callback(bind_obj(&charge_bridge::handle_discovery, this));
    set_discovery_pending(true);
}

bool charge_bridge::is_mdns_endpoint() const {
    return m_endpoint_intent.value != endpoint_intent::fixed_ip;
}

discovery_device_type charge_bridge::mdns_device_type() const {
    if (m_endpoint_intent.value == endpoint_intent::any_evse_mdns) {
        return discovery_device_type::CB_EVSE;
    }
    return discovery_device_type::CB_EV;
}

std::set<std::string> charge_bridge::select_discovery_interfaces() const {
    std::set<std::string> available_interfaces;
    try {
        for (auto const& item : everest::lib::io::socket::get_all_interfaces()) {
            available_interfaces.insert(item.name);
        }
    } catch (std::exception const&) {
        return {};
    }

    if (m_endpoint_intent.interfaces.empty()) {
        return available_interfaces;
    }

    std::set<std::string> selected_interfaces;
    if (m_endpoint_intent.excluding_interfaces) {
        for (auto const& item : available_interfaces) {
            if (m_endpoint_intent.interfaces.count(item) == 0) {
                selected_interfaces.insert(item);
            }
        }
        return selected_interfaces;
    }

    for (auto const& item : m_endpoint_intent.interfaces) {
        if (available_interfaces.count(item) > 0) {
            selected_interfaces.insert(item);
        }
    }
    return selected_interfaces;
}

void charge_bridge::start_discovery_attempt(std::set<std::string> const& interfaces) {
    if (not m_event_handler) {
        return;
    }
    auto type = mdns_device_type();
    m_event_handler->add_action([this, type, interfaces]() {
        try {
            if (m_discovery) {
                m_event_handler->unregister_event_handler(m_discovery.get());
            }
            m_discovery.reset();
            init_discovery(type, interfaces, false);
            auto registered = m_event_handler->register_event_handler(m_discovery.get());
            if (not registered) {
                m_event_handler->unregister_event_handler(m_discovery.get());
                utilities::print_error(m_config.cb_name, "DISCOVERY", -1)
                    << "Failed to register mDNS discovery handler" << std::endl;
                std::unique_ptr<discovery> tmp;
                std::swap(m_discovery, tmp);
                set_discovery_pending(true);
            }
        } catch (std::exception const& e) {
            utilities::print_error(m_config.cb_name, "DISCOVERY", -1)
                << "Failed to start mDNS discovery: " << e.what() << std::endl;
            if (m_discovery) {
                m_event_handler->unregister_event_handler(m_discovery.get());
            }
            std::unique_ptr<discovery> tmp;
            std::swap(m_discovery, tmp);
            set_discovery_pending(true);
        }
        m_cb_status.notify_one();
    });
}

void charge_bridge::stop_discovery() {
    if (not m_event_handler) {
        return;
    }
    m_event_handler->add_action([this]() {
        std::unique_ptr<discovery> tmp;
        if (m_discovery) {
            m_event_handler->unregister_event_handler(m_discovery.get());
        }
        std::swap(m_discovery, tmp);
    });
}

void charge_bridge::handle_discovery(everest::lib::io::mdns::mDNS_discovery const& info) {
    auto const& ip = info.ip;
    utilities::print_error(m_config.cb_name, "DISCOVERY", 0) << "Discovered at: " + ip << std::endl;

    m_discovery_info = info;
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
    if (m_config.io) {
        m_config.io->cb_remote = ip;
    }

    m_config.firmware.cb_remote = ip;

    m_event_handler->add_action([this]() {
        std::unique_ptr<discovery> tmp;
        if (m_discovery) {
            m_event_handler->unregister_event_handler(m_discovery.get());
        }
        std::swap(m_discovery, tmp);

        set_discovery_pending(false);
    });
}

void charge_bridge::set_discovery_pending(bool pending) {
    auto handle = m_cb_status.handle();
    set_discovery_pending(*handle, pending);
}

void charge_bridge::set_discovery_pending(charge_bridge_status& status, bool pending) {
    auto changed = status.discovery_pending != pending;
    status.discovery_pending = pending;
    m_cb_status.notify_one();
    if (changed) {
        m_ready_notify.notify();
    }
}

std::future<bool> charge_bridge::start_internal_runtime() {
    auto promise = std::make_shared<std::promise<bool>>();
    auto result = promise->get_future();
    auto preserve_runtime_objects =
        m_can_0_client || m_pty_1 || m_pty_2 || m_pty_3 || m_bsp || m_plc || m_io || m_heartbeat;

    if (not m_event_handler) {
        promise->set_value(false);
        return result;
    }

    m_event_handler->add_action([this, promise = std::move(promise), preserve_runtime_objects]() mutable {
        try {
            create_internal_runtime();
            auto runtime_registered = register_internal_events(*m_event_handler);
            if (not runtime_registered) {
                unregister_internal_runtime_events(*m_event_handler);
                if (preserve_runtime_objects) {
                    disconnect_internal_runtime_endpoints();
                } else {
                    cleanup_internal_runtime();
                }
                promise->set_value(false);
                m_cb_status.notify_one();
                return;
            }

            promise->set_value(true);
            m_cb_status.notify_one();
        } catch (...) {
            unregister_internal_runtime_events(*m_event_handler);
            if (preserve_runtime_objects) {
                disconnect_internal_runtime_endpoints();
            } else {
                cleanup_internal_runtime();
            }
            promise->set_exception(std::current_exception());
            m_cb_status.notify_one();
        }
    });

    return result;
}

void charge_bridge::create_internal_runtime() {
    if (m_config.can0.has_value()) {
        if (not m_can_0_client) {
            m_can_0_client = std::make_unique<can_bridge>(m_config.can0.value(), m_ready_notify);
        } else {
            m_can_0_client->connect_cb_endpoint(m_config.can0->cb_remote);
        }
    }
    if (m_config.serial1.has_value()) {
        if (not m_pty_1) {
            m_pty_1 = std::make_unique<serial_bridge>(m_config.serial1.value(), m_ready_notify);
        } else {
            m_pty_1->connect_cb_endpoint(m_config.serial1->cb_remote);
        }
    }
    if (m_config.serial2.has_value()) {
        if (not m_pty_2) {
            m_pty_2 = std::make_unique<serial_bridge>(m_config.serial2.value(), m_ready_notify);
        } else {
            m_pty_2->connect_cb_endpoint(m_config.serial2->cb_remote);
        }
    }
    if (m_config.serial3.has_value()) {
        if (not m_pty_3) {
            m_pty_3 = std::make_unique<serial_bridge>(m_config.serial3.value(), m_ready_notify);
        } else {
            m_pty_3->connect_cb_endpoint(m_config.serial3->cb_remote);
        }
    }
    if (m_config.plc.has_value()) {
        if (not m_plc) {
            m_plc = std::make_unique<plc_bridge>(m_config.plc.value(), m_ready_notify);
        } else {
            m_plc->connect_cb_endpoint(m_config.plc->cb_remote);
        }
    }
    if (m_config.bsp.has_value()) {
        if (not m_bsp) {
            m_bsp = std::make_unique<bsp_bridge>(m_config.bsp.value(), m_ready_notify);
        } else {
            m_bsp->connect_cb_endpoint(m_config.bsp->cb_remote);
        }
    }
    if (m_config.io.has_value()) {
        if (not m_io) {
            m_io = std::make_unique<io_bridge>(m_config.io.value(), m_ready_notify);
        } else {
            m_io->connect_cb_endpoint(m_config.io->cb_remote);
        }
    }
    if (m_config.heartbeat.has_value()) {
        auto heartbeat_cb = [this](bool connected) {
            {
                auto handle = m_cb_status.handle();
                handle->is_connected = connected;
            }
            if (m_plc) {
                m_plc->set_cb_connection_status(connected);
            }
            if (m_io) {
                m_io->set_cb_connection_status(connected);
            }
            if (m_can_0_client) {
                m_can_0_client->set_cb_connection_status(connected);
            }
            m_cb_status.notify_one();
        };

        if (not m_heartbeat) {
            m_heartbeat = std::make_unique<heartbeat_service>(m_config.heartbeat.value(), heartbeat_cb, m_ready_notify);
        } else {
            m_heartbeat->connect_cb_endpoint(m_config.heartbeat->cb_remote);
        }
    }
}

void charge_bridge::cleanup_internal_runtime() {
    disconnect_internal_runtime_endpoints();
    m_can_0_client.reset();
    m_pty_1.reset();
    m_pty_2.reset();
    m_pty_3.reset();
    m_bsp.reset();
    m_plc.reset();
    m_io.reset();
    m_heartbeat.reset();
}

void charge_bridge::disconnect_internal_runtime_endpoints() {
    if (m_can_0_client) {
        m_can_0_client->disconnect_cb_endpoint();
    }
    if (m_pty_1) {
        m_pty_1->disconnect_cb_endpoint();
    }
    if (m_pty_2) {
        m_pty_2->disconnect_cb_endpoint();
    }
    if (m_pty_3) {
        m_pty_3->disconnect_cb_endpoint();
    }
    if (m_bsp) {
        m_bsp->disconnect_cb_endpoint();
    }
    if (m_plc) {
        m_plc->disconnect_cb_endpoint();
    }
    if (m_io) {
        m_io->disconnect_cb_endpoint();
    }
    if (m_heartbeat) {
        m_heartbeat->disconnect_cb_endpoint();
    }
}

bool charge_bridge::unregister_internal_runtime_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
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
    if (m_io) {
        result = handler.unregister_event_handler(m_io.get()) && result;
    }
    return result;
}

std::future<bool> charge_bridge::stop_internal_runtime() {
    auto promise = std::make_shared<std::promise<bool>>();
    auto result = promise->get_future();

    if (not m_event_handler) {
        cleanup_internal_runtime();
        promise->set_value(true);
        return result;
    }
    m_event_handler->add_action([this, promise = std::move(promise)]() mutable {
        try {
            unregister_internal_runtime_events(*m_event_handler);
            disconnect_internal_runtime_endpoints();
            promise->set_value(true);
        } catch (...) {
            disconnect_internal_runtime_endpoints();
            promise->set_exception(std::current_exception());
        }
        m_cb_status.notify_one();
    });

    return result;
}

charge_bridge::~charge_bridge() {
    m_cb_status.notify_one();
    if (m_manager.joinable()) {
        m_manager.join();
    }
}

void charge_bridge::manage(everest::lib::io::event::fd_event_handler& handler, std::atomic_bool const& run,
                           bool force_update) {
    if (m_manager.joinable()) {
        std::cerr << "WARN: charge_bridge::manage called while manager thread is already running" << std::endl;
        return;
    }

    using namespace std::chrono_literals;
    m_event_handler = &handler;
    m_force_firmware_update = force_update;

    m_event_handler->add_action([this]() {
        if (m_config.telemetry.has_value()) {
            m_mqtt = std::make_unique<everest::lib::io::mqtt::mqtt_client>(mqtt_reconnect_timeout_ms);
            m_mqtt->connect(m_config.telemetry->mqtt_bind, m_config.telemetry->mqtt_remote,
                            m_config.telemetry->mqtt_port, m_config.telemetry->mqtt_ping_interval_ms);
        }
        // Drive the 1s status/telemetry tick regardless of telemetry config or broker reachability,
        // so the status report and the terminal UI's live readouts refresh even with telemetry
        // disabled or the MQTT broker down. (timer_fd is periodic, so a single arm keeps it firing.)
        m_1s_tick.set_timeout(std::chrono::seconds(1));
        // Register the manage events (readiness notifier + tick) regardless of telemetry, so the
        // status report is produced even when telemetry/MQTT is disabled. The MQTT fd itself is only
        // registered when the telemetry client exists (see register_manage_events).
        register_manage_events(*m_event_handler);
    });

    if (is_mdns_endpoint()) {
        set_discovery_pending(true);
    }

    using clock = std::chrono::steady_clock;
    auto action = [this](auto& status_handle, charge_bridge_status& current_status, int& error_count,
                         std::optional<clock::time_point>& next_connect_retry_time, std::future<bool>& startup_runtime,
                         bool& startup_runtime_in_progress,
                         std::optional<clock::time_point>& discovery_attempt_deadline,
                         std::optional<clock::time_point>& discovery_retry_time, std::future<bool>& stop_runtime,
                         bool& runtime_stop_in_progress) {
        auto now = clock::now();
        if (runtime_stop_in_progress) {
            if (stop_runtime.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }
            try {
                stop_runtime.get();
            } catch (...) {
            }
            runtime_stop_in_progress = false;
            m_internal_runtime_started = false;
            m_was_connected = false;
            error_count = 0;

            if (is_mdns_endpoint()) {
                set_discovery_pending(current_status, true);
                m_discovery_active = false;
                next_connect_retry_time.reset();
                discovery_attempt_deadline.reset();
                discovery_retry_time.reset();
                return;
            }
        }
        if (next_connect_retry_time.has_value() && now < next_connect_retry_time.value()) {
            return;
        } else if (next_connect_retry_time.has_value()) {
            next_connect_retry_time.reset();
        }
        if (current_status.discovery_pending) {
            if (m_discovery_active) {
                if (discovery_attempt_deadline.has_value() && now >= discovery_attempt_deadline.value()) {
                    stop_discovery();
                    m_discovery_active = false;
                    discovery_attempt_deadline.reset();
                    discovery_retry_time = now + discovery_retry_delay;
                }
                return;
            }

            if (discovery_retry_time.has_value() && now < discovery_retry_time.value()) {
                return;
            }

            auto discovery_interfaces = select_discovery_interfaces();
            if (discovery_interfaces.empty()) {
                discovery_retry_time = now + discovery_retry_delay;
                return;
            }

            start_discovery_attempt(discovery_interfaces);
            m_discovery_active = true;
            discovery_attempt_deadline = now + discovery_attempt_timeout;
            discovery_retry_time.reset();
            return;
        }

        if (m_discovery_active) {
            m_discovery_active = false;
            discovery_attempt_deadline.reset();
            discovery_retry_time.reset();
        }

        if (m_was_connected and not current_status.is_connected) {
            stop_runtime = stop_internal_runtime();
            runtime_stop_in_progress = true;
        }
        if (not m_was_connected) {
            if (startup_runtime_in_progress) {
                if (startup_runtime.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    bool runtime_started = false;
                    try {
                        runtime_started = startup_runtime.get();
                    } catch (...) {
                        runtime_started = false;
                    }
                    startup_runtime_in_progress = false;
                    if (runtime_started) {
                        m_internal_runtime_started = true;
                        m_was_connected = true;
                        error_count = 0;
                    }
                }
                return;
            }

            // update_firmware() blocks (connection probe, and potentially a multi-minute firmware
            // upload) and does not touch the guarded status. Release the monitor lock across it so
            // get_status() on the shared event-loop thread — and therefore every other bridge
            // instance — is not stalled for the duration. current_status stays valid; only the lock
            // is dropped and re-acquired.
            status_handle.unlock();
            bool firmware_ok = false;
            try {
                firmware_ok = update_firmware(m_force_firmware_update);
            } catch (...) {
                status_handle.lock();
                throw;
            }
            status_handle.lock();
            if (firmware_ok) {
                if (not m_internal_runtime_started) {
                    startup_runtime = start_internal_runtime();
                    startup_runtime_in_progress = true;
                } else {
                    m_event_handler->add_action([this]() { register_internal_events(*m_event_handler); });
                    m_was_connected = true;
                    error_count = 0;
                }
            } else if (is_mdns_endpoint() && not m_internal_runtime_started) {
                set_discovery_pending(current_status, true);
                next_connect_retry_time = clock::now() + manager_base_cycle;
            }
        }
    };
    m_manager = std::thread([&run, action, this]() {
        using clock = std::chrono::steady_clock;
        std::future<bool> startup_runtime;
        bool startup_runtime_in_progress = false;
        std::future<bool> stop_runtime;
        bool runtime_stop_in_progress = false;
        std::optional<clock::time_point> discovery_attempt_deadline;
        std::optional<clock::time_point> discovery_retry_time;
        std::optional<clock::time_point> next_connect_retry_time;

        auto is_startup_runtime_ready = [&startup_runtime, &startup_runtime_in_progress]() {
            return startup_runtime_in_progress &&
                   startup_runtime.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        };
        auto is_stop_runtime_ready = [&stop_runtime, &runtime_stop_in_progress]() {
            return runtime_stop_in_progress &&
                   stop_runtime.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        };

        auto handle = m_cb_status.handle();
        bool last_is_connected = handle->is_connected;
        bool last_discovery_pending = handle->discovery_pending;
        int error_count = 0;
        auto compute_wait_timeout = [&](std::chrono::milliseconds wait_timeout) {
            if (handle->discovery_pending && is_mdns_endpoint()) {
                if (next_connect_retry_time.has_value()) {
                    auto now = clock::now();
                    auto retry_remaining =
                        std::chrono::duration_cast<std::chrono::milliseconds>(next_connect_retry_time.value() - now);
                    if (retry_remaining < wait_timeout) {
                        return retry_remaining;
                    }
                }
                if (m_discovery_active && discovery_attempt_deadline.has_value()) {
                    auto now = clock::now();
                    auto attempt_remaining =
                        std::chrono::duration_cast<std::chrono::milliseconds>(discovery_attempt_deadline.value() - now);
                    if (attempt_remaining < wait_timeout) {
                        return attempt_remaining;
                    }
                } else if (discovery_retry_time.has_value()) {
                    auto now = clock::now();
                    auto retry_remaining =
                        std::chrono::duration_cast<std::chrono::milliseconds>(discovery_retry_time.value() - now);
                    if (retry_remaining < wait_timeout) {
                        return retry_remaining;
                    }
                }
            }
            return wait_timeout;
        };

        auto condition = [&] {
            if (handle->is_connected not_eq last_is_connected) {
                return true;
            }
            if (handle->discovery_pending not_eq last_discovery_pending) {
                return true;
            }
            if (is_startup_runtime_ready()) {
                return true;
            }
            if (is_stop_runtime_ready()) {
                return true;
            }
            if (handle->discovery_pending && m_discovery_active && discovery_attempt_deadline.has_value()) {
                if (clock::now() >= discovery_attempt_deadline.value()) {
                    return true;
                }
            }
            if (handle->discovery_pending && (not m_discovery_active) && discovery_retry_time.has_value()) {
                if (clock::now() >= discovery_retry_time.value()) {
                    return true;
                }
            }
            if (handle->discovery_pending && next_connect_retry_time.has_value()) {
                if (clock::now() >= next_connect_retry_time.value()) {
                    return true;
                }
            }
            if (not run.load()) {
                return true;
            }
            return false;
        };
        while (run.load()) {
            action(handle, *handle, error_count, next_connect_retry_time, startup_runtime, startup_runtime_in_progress,
                   discovery_attempt_deadline, discovery_retry_time, stop_runtime, runtime_stop_in_progress);
            if (handle->discovery_pending && is_mdns_endpoint()) {
                auto wait_timeout =
                    compute_wait_timeout(std::chrono::duration_cast<std::chrono::milliseconds>(manager_base_cycle));
                if (wait_timeout < 0ms) {
                    wait_timeout = 0ms;
                }
                handle.wait_for(condition, wait_timeout);
                last_is_connected = handle->is_connected;
                last_discovery_pending = handle->discovery_pending;
                continue;
            }

            auto wait_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(manager_base_cycle);
            handle.wait_for(condition, wait_timeout);
            last_is_connected = handle->is_connected;
            last_discovery_pending = handle->discovery_pending;
        }
    });
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

utilities::chargebridge_status charge_bridge::get_status() {
    utilities::chargebridge_status status;

    status.cb_name = m_config.cb_name;
    {
        auto handle = m_cb_status.handle();
        status.connected = handle->is_connected;
        if (m_endpoint_intent.value != endpoint_intent::fixed_ip) {
            status.discovered = not handle->discovery_pending;
        }
    }

    // Read-only network identity: the IP we talk to (configured or discovered) and the mDNS
    // hostname/service/TXT records when discovered.
    if (not m_config.cb_remote.empty() || m_discovery_info.has_value()) {
        utilities::chargebridge_network_info net;
        net.ip = m_config.cb_remote;
        if (m_discovery_info.has_value()) {
            net.mdns_hostname = m_discovery_info->hostname;
            net.mdns_service = m_discovery_info->service_instance;
            for (auto const& [key, value] : m_discovery_info->txt) {
                net.mdns_txt.emplace_back(key, value);
            }
        }
        status.network = std::move(net);
    }

    if (m_can_0_client) {
        auto available = m_can_0_client->available();
        status.can0.emplace(available);
    }
    if (m_pty_1) {
        auto available = m_pty_1->available();
        status.serial1.emplace(available);
    }
    if (m_pty_2) {
        auto available = m_pty_2->available();
        status.serial2.emplace(available);
    }
    if (m_pty_3) {
        auto available = m_pty_3->available();
        status.serial3.emplace(available);
    }
    if (m_bsp) {
        auto available = m_bsp->available();
        status.bsp.emplace(available);
        status.cp_state = m_bsp->cp_state();
    }
    if (m_plc) {
        auto available = m_plc->available();
        status.plc.emplace(available);
    }
    if (m_heartbeat) {
        auto available = m_heartbeat->available();
        status.heartbeat.emplace(available);
        status.mcu_resets.emplace(m_heartbeat->mcu_reset_count());
        status.telemetry = m_heartbeat->latest_telemetry();
    }
    if (m_io) {
        auto available = m_io->available();
        status.io.emplace(available);
        if (auto io = m_io->latest_io()) {
            status.gpio = std::move(io->gpio);
            status.adc = std::move(io->adc);
            status.io_telemetry = std::move(io->telemetry);
        }
    }

    return status;
}

void charge_bridge::handle_ready() {
    auto status = get_status();
    publish_status(status);
    if (m_status_sink) {
        m_status_sink(status);
    }
}

void charge_bridge::handle_tick() {
    auto status = get_status();
    publish_status(status);
    // The tick sink is wired up only for the interactive terminal UI, so its live readouts and
    // telemetry charts refresh every tick. In log mode it stays unset to keep the log output unchanged.
    if (m_tick_sink) {
        m_tick_sink(status);
    }
}

void charge_bridge::publish_status(utilities::chargebridge_status const& status) {
    if (not m_config.telemetry.has_value()) {
        return;
    }

    bool result = true;
    auto publish = [this](std::string_view component, std::string_view item, bool status) {
        std::stringstream topic;
        topic << m_config.telemetry->telemetry_topic << "/" << m_config.cb_name << "/" << component << "/" << item;
        std::string_view payload = status ? "true" : "false";
        m_mqtt->publish(topic.str(), payload);
    };

    publish("chargebridge", "connected", status.connected);
    if (status.discovered.has_value()) {
        auto discovered = status.discovered.value();
        publish("chargebridge", "discovered", discovered);
        result = result && discovered;
    }

    auto publish_status = [publish](std::string_view component, bool status) { publish(component, "status", status); };

    if (status.can0.has_value()) {
        auto available = status.can0.value();
        result = result && available;
        publish_status("can_0", available);
    }
    if (status.serial1.has_value()) {
        auto available = status.serial1.value();
        result = result && available;
        publish_status("serial_1", available);
    }
    if (status.serial2.has_value()) {
        auto available = status.serial2.value();
        result = result && available;
        publish_status("serial_2", available);
    }
    if (status.serial3.has_value()) {
        auto available = status.serial3.value();
        result = result && available;
        publish_status("serial_3", available);
    }
    if (status.bsp.has_value()) {
        auto available = status.bsp.value();
        result = result && available;
        publish_status("bsp", available);
    }
    if (status.plc.has_value()) {
        auto available = status.plc.value();
        result = result && available;
        publish_status("plc", available);
    }
    if (status.heartbeat.has_value()) {
        auto available = status.heartbeat.value();
        result = result && available;
        publish_status("heartbeat", available);
    }
    if (status.io.has_value()) {
        auto available = status.io.value();
        result = result && available;
        publish_status("io", available);
    }
    publish_status("chargebridge", result);
}

bool charge_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = register_internal_events(handler) && result;
    result = register_manage_events(handler) && result;

    return result;
}
bool charge_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = unregister_internal_events(handler) && result;
    result = unregister_manage_events(handler) && result;

    return result;
}
bool charge_bridge::register_manage_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result =
        handler.register_event_handler(&m_1s_tick, everest::lib::util::bind_obj(&charge_bridge::handle_tick, this)) &&
        result;

    // The readiness notifier drives the status report (status UI line + the telemetry status publish),
    // so it must be registered regardless of telemetry. publish_status() itself no-ops when telemetry
    // is disabled, and the MQTT fd below is only registered when the client exists.
    result = handler.register_event_handler(&m_ready_notify,
                                            everest::lib::util::bind_obj(&charge_bridge::handle_ready, this)) &&
             result;

    if (m_mqtt) {
        result = handler.register_event_handler(m_mqtt.get()) && result;
    }

    return result;
}
bool charge_bridge::unregister_manage_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.unregister_event_handler(&m_1s_tick) && result;
    result = handler.unregister_event_handler(&m_ready_notify) && result;
    if (m_mqtt) {
        result = handler.unregister_event_handler(m_mqtt.get()) && result;
    }

    return result;
}

bool charge_bridge::register_internal_events(everest::lib::io::event::fd_event_handler& handler) {
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
    if (m_io) {
        result = handler.register_event_handler(m_io.get()) && result;
    }

    if (m_discovery) {
        result = handler.register_event_handler(m_discovery.get()) && result;
    }

    return result;
}
bool charge_bridge::unregister_internal_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;

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
    if (m_io) {
        result = handler.unregister_event_handler(m_io.get()) && result;
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
    if (c.io) {
        std::cout << " * io:        " << c.cb_remote << ":" << c.cb_port;
        std::cout << " MQTT " << c.io->mqtt_remote << ":" << c.io->mqtt_port;
        if (not c.io->mqtt_bind.empty()) {
            std::cout << " on " << c.io->mqtt_bind;
        }
        std::cout << " send interval " << c.io->interval_s << "s" << std::endl;
    }

    std::cout << "\n" << std::endl;
}

} // namespace charge_bridge
