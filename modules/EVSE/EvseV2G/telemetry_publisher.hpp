// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest_api_types/telemetry/v2g.hpp>
#include <framework/everest.hpp>
#include <mutex>
#include <nlohmann/json.hpp>

namespace module {

// Owns the V2G telemetry status structs and publishes them through the
// Everest TelemetryProvider channel. One instance per EvseV2G module.
// Each struct is published as its own block under category "V2G".
class V2gTelemetryPublisher {
public:
    V2gTelemetryPublisher(Everest::TelemetryProvider& telemetry, bool enabled) :
        m_telemetry(telemetry), m_enabled(enabled) {}

    everest::lib::API::V1_0::types::telemetry::V2gTransport      transport;
    everest::lib::API::V1_0::types::telemetry::V2gEvElectrical   ev_electrical;
    everest::lib::API::V1_0::types::telemetry::V2gPaymentService payment_service;
    everest::lib::API::V1_0::types::telemetry::V2gChargerStatus  charger_status;

    void publish_transport()       { publish_block("transport", transport); }
    void publish_ev_electrical()   { publish_block("ev_electrical", ev_electrical); }
    void publish_payment_service() { publish_block("payment_service", payment_service); }
    void publish_charger_status()  { publish_block("charger_status", charger_status); }

    void publish_all() {
        publish_transport();
        publish_ev_electrical();
        publish_payment_service();
        publish_charger_status();
    }

private:
    template <typename T>
    void publish_block(const std::string& block, T const& payload) {
        if (!m_enabled) {
            return;
        }
        nlohmann::json const j = payload;
        Everest::TelemetryMap tm;
        for (auto it = j.begin(); it != j.end(); ++it) {
            tm[it.key()] = it.value();
        }
        std::lock_guard<std::mutex> lock(m_publish_mutex);
        m_telemetry.publish("V2G", block, tm);
    }

    Everest::TelemetryProvider& m_telemetry;
    bool m_enabled;
    std::mutex m_publish_mutex;
};

} // namespace module
