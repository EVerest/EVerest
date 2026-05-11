// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/util/misc/change_tracker.hpp>
#include <everest_api_types/telemetry/json_codec.hpp>
#include <framework/everest.hpp>
#include <mutex>

namespace module {

namespace telemetry_types = everest::lib::API::V1_0::types::telemetry;
using V2gTransportTracker = everest::lib::util::change_tracker<telemetry_types::V2gTransport>;
using V2gEvElectricalTracker = everest::lib::util::change_tracker<telemetry_types::V2gEvElectrical>;
using V2gPaymentServiceTracker = everest::lib::util::change_tracker<telemetry_types::V2gPaymentService>;
using V2gChargerStatusTracker = everest::lib::util::change_tracker<telemetry_types::V2gChargerStatus>;

// Owns the V2G telemetry status structs and publishes them through the
// Everest TelemetryProvider channel. One instance per EvseV2G module.
// Each struct is published as its own block under category "V2G".
class V2gTelemetryPublisher {
public:
    V2gTelemetryPublisher(Everest::TelemetryProvider& telemetry, bool enabled, bool publish_only_on_change) :
        m_telemetry(telemetry), m_enabled(enabled), m_publish_only_on_change(publish_only_on_change) {
    }

    template <typename F> void update_transport(F&& update_fn) {
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        V2gTransportTracker tracker{m_transport};
        update_fn(tracker);
        maybe_publish_block_no_lock("transport", tracker);
    }

    template <typename F> void update_ev_electrical(F&& update_fn) {
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        V2gEvElectricalTracker tracker{m_ev_electrical};
        update_fn(tracker);
        maybe_publish_block_no_lock("ev_electrical", tracker);
    }

    template <typename F> void update_payment_service(F&& update_fn) {
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        V2gPaymentServiceTracker tracker{m_payment_service};
        update_fn(tracker);
        maybe_publish_block_no_lock("payment_service", tracker);
    }

    template <typename F> void update_charger_status(F&& update_fn) {
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        V2gChargerStatusTracker tracker{m_charger_status};
        update_fn(tracker);
        maybe_publish_block_no_lock("charger_status", tracker);
    }

    // Reset session-scoped telemetry values but keep persistent transport settings.
    void reset_session_state() {
        std::lock_guard<std::mutex> lock(m_publish_mutex);

        const auto transport_persistent = m_transport;
        m_transport = telemetry_types::V2gTransport{};
        m_transport.udp_server_status = transport_persistent.udp_server_status;
        m_transport.tcp_listener_status = transport_persistent.tcp_listener_status;
        m_transport.tcp_server_status = transport_persistent.tcp_server_status;
        m_transport.tcp_connection_established = transport_persistent.tcp_connection_established;
        m_transport.tcp_discovery_enable = transport_persistent.tcp_discovery_enable;
        m_transport.tcp_security_enable = transport_persistent.tcp_security_enable;
        m_transport.tcp_security_required = transport_persistent.tcp_security_required;
        m_transport.tcp_port_number = transport_persistent.tcp_port_number;

        m_ev_electrical = telemetry_types::V2gEvElectrical{};
        m_payment_service = telemetry_types::V2gPaymentService{};
        m_charger_status = telemetry_types::V2gChargerStatus{};

        publish_transport_no_lock();
        publish_ev_electrical_no_lock();
        publish_payment_service_no_lock();
        publish_charger_status_no_lock();
    }

    void publish_transport() {
        publish_block("transport", m_transport);
    }
    void publish_ev_electrical() {
        publish_block("ev_electrical", m_ev_electrical);
    }
    void publish_payment_service() {
        publish_block("payment_service", m_payment_service);
    }
    void publish_charger_status() {
        publish_block("charger_status", m_charger_status);
    }

    void publish_all() {
        publish_transport();
        publish_ev_electrical();
        publish_payment_service();
        publish_charger_status();
    }

private:
    template <typename T> void publish_block(const std::string& block, T const& payload) {
        if (!m_enabled) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        publish_block_no_lock(block, payload);
    }

    template <typename T> void publish_block_no_lock(const std::string& block, T const& payload) {
        if (!m_enabled) {
            return;
        }
        const nlohmann::json json_payload = payload;
        Everest::TelemetryMap telemetry;
        for (const auto& [key, value] : json_payload.items()) {
            telemetry.emplace(key, value);
        }
        m_telemetry.publish("V2G", block, telemetry);
    }

    void publish_transport_no_lock() {
        publish_block_no_lock("transport", m_transport);
    }
    void publish_ev_electrical_no_lock() {
        publish_block_no_lock("ev_electrical", m_ev_electrical);
    }
    void publish_payment_service_no_lock() {
        publish_block_no_lock("payment_service", m_payment_service);
    }
    void publish_charger_status_no_lock() {
        publish_block_no_lock("charger_status", m_charger_status);
    }

    template <typename T>
    void maybe_publish_block_no_lock(const std::string& block, const everest::lib::util::change_tracker<T>& tracker) {
        if (!m_publish_only_on_change || tracker.changed()) {
            publish_block_no_lock(block, tracker.value());
        }
    }

    Everest::TelemetryProvider& m_telemetry;
    bool m_enabled;
    bool m_publish_only_on_change;
    std::mutex m_publish_mutex;

    telemetry_types::V2gTransport m_transport{};
    telemetry_types::V2gEvElectrical m_ev_electrical{};
    telemetry_types::V2gPaymentService m_payment_service{};
    telemetry_types::V2gChargerStatus m_charger_status{};
};

} // namespace module
