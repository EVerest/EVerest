// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "entrypoint/json_codec.hpp"
#include "entrypoint/API.hpp"
#include "entrypoint/codec.hpp"

#include "nlohmann/json.hpp"

#include <iostream>

namespace everest::lib::API::V1_0::types::entrypoint {

void to_json(json& j, ApiParameter const& k) noexcept {
    j = json{
        {"type", k.type},
        {"module_id", k.module_id},
        {"version", k.version},
    };
    if (k.communication_monitoring) {
        j["communication_monitoring"] = k.communication_monitoring.value();
    }
}

void from_json(const json& j, ApiParameter& k) {
    k.type = j.at("type");
    k.module_id = j.at("module_id");
    k.version = j.at("version");
    if (j.contains("communication_monitoring")) {
        k.communication_monitoring.emplace(j["communication_monitoring"].get<CommunicationParameters>());
    }
}

void to_json(json& j, ApiDiscoverResponse const& k) noexcept {
    j["apis"] = json::array();
    for (auto val : k.apis) {
        j["apis"].push_back(val);
    }
}

void from_json(const json& j, ApiDiscoverResponse& k) {
    k.apis.clear();
    for (auto val : j.at("apis")) {
        k.apis.push_back(val);
    }
}

void to_json(json& j, CommunicationParameters const& k) noexcept {
    if (k.heartbeat_period_ms) {
        j["heartbeat_period_ms"] = k.heartbeat_period_ms.value();
    }
    if (k.communication_check_period_s) {
        j["communication_check_period_s"] = k.communication_check_period_s.value();
    }
    if (k.request_reply_timeout_s) {
        j["request_reply_timeout_s"] = k.request_reply_timeout_s.value();
    }
}

void from_json(const json& j, CommunicationParameters& k) {
    if (j.contains("heartbeat_period_ms")) {
        k.heartbeat_period_ms.emplace(j["heartbeat_period_ms"].get<int32_t>());
    }
    if (j.contains("communication_check_period_s")) {
        k.communication_check_period_s.emplace(j["communication_check_period_s"].get<int32_t>());
    }
    if (j.contains("request_reply_timeout_s")) {
        k.request_reply_timeout_s.emplace(j["request_reply_timeout_s"].get<int32_t>());
    }
}

} // namespace everest::lib::API::V1_0::types::entrypoint
