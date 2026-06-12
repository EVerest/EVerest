// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest_api_types/telemetry/json_codec.hpp>
#include <framework/everest.hpp>
#include <mutex>

namespace module {

// Owns the V2G telemetry status structs and publishes them through the
// Everest TelemetryProvider channel. One instance per EvseV2G module.
// Each struct is published as its own block under category "V2G".
class V2gTelemetryPublisher {
public:
    V2gTelemetryPublisher(Everest::TelemetryProvider& telemetry, bool enabled) :
        m_telemetry(telemetry), m_enabled(enabled) {
    }

    template <typename F> void update_transport(F&& update_fn) {
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        update_fn(m_transport);
        publish_block_no_lock("transport", m_transport);
    }

    template <typename F> void update_ev_electrical(F&& update_fn) {
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        update_fn(m_ev_electrical);
        publish_block_no_lock("ev_electrical", m_ev_electrical);
    }

    template <typename F> void update_payment_service(F&& update_fn) {
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        update_fn(m_payment_service);
        publish_block_no_lock("payment_service", m_payment_service);
    }

    template <typename F> void update_charger_status(F&& update_fn) {
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        update_fn(m_charger_status);
        publish_block_no_lock("charger_status", m_charger_status);
    }

    // Reset session-scoped telemetry values but keep persistent transport settings.
    void reset_session_state() {
        std::lock_guard<std::mutex> lock(m_publish_mutex);

        const auto transport_persistent = m_transport;
        m_transport = everest::lib::API::V1_0::types::telemetry::V2gTransport{};
        m_transport.udp_server_status = transport_persistent.udp_server_status;
        m_transport.tcp_listener_status = transport_persistent.tcp_listener_status;
        m_transport.tcp_server_status = transport_persistent.tcp_server_status;
        m_transport.tcp_connection_established = transport_persistent.tcp_connection_established;
        m_transport.tcp_discovery_enable = transport_persistent.tcp_discovery_enable;
        m_transport.tcp_security_enable = transport_persistent.tcp_security_enable;
        m_transport.tcp_security_required = transport_persistent.tcp_security_required;
        m_transport.tcp_port_number = transport_persistent.tcp_port_number;

        m_ev_electrical = everest::lib::API::V1_0::types::telemetry::V2gEvElectrical{};
        m_payment_service = everest::lib::API::V1_0::types::telemetry::V2gPaymentService{};
        m_charger_status = everest::lib::API::V1_0::types::telemetry::V2gChargerStatus{};

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

    Everest::TelemetryProvider& m_telemetry;
    bool m_enabled;
    std::mutex m_publish_mutex;

    everest::lib::API::V1_0::types::telemetry::V2gTransport m_transport{};
    everest::lib::API::V1_0::types::telemetry::V2gEvElectrical m_ev_electrical{};
    everest::lib::API::V1_0::types::telemetry::V2gPaymentService m_payment_service{};
    everest::lib::API::V1_0::types::telemetry::V2gChargerStatus m_charger_status{};
};

} // namespace module
