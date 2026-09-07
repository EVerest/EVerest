// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "session_storage/json_codec.hpp"
#include "session_storage/API.hpp"
#include "session_storage/codec.hpp"

#include "auth/json_codec.hpp"
#include "evse_manager/json_codec.hpp"
#include "powermeter/json_codec.hpp"
#include "session_cost/json_codec.hpp"

#include "nlohmann/json.hpp"

namespace everest::lib::API::V1_0::types::session_storage {

void to_json(json& j, SessionState const& k) noexcept {
    switch (k) {
    case SessionState::Open:
        j = "Open";
        return;
    case SessionState::Finished:
        j = "Finished";
        return;
    case SessionState::Stale:
        j = "Stale";
        return;
    }

    j = "INVALID_VALUE__everest::lib::API::V1_0::types::session_storage::SessionState";
}

void from_json(const json& j, SessionState& k) {
    std::string s = j;
    if (s == "Open") {
        k = SessionState::Open;
        return;
    }
    if (s == "Finished") {
        k = SessionState::Finished;
        return;
    }
    if (s == "Stale") {
        k = SessionState::Stale;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::session_storage::SessionState");
}

void to_json(json& j, Transaction const& k) noexcept {
    j = json{
        {"timestamp_start", k.timestamp_start},
        {"energy_Wh_import_start", k.energy_Wh_import_start},
    };
    if (k.timestamp_stop) {
        j["timestamp_stop"] = k.timestamp_stop.value();
    }
    if (k.energy_Wh_import_stop) {
        j["energy_Wh_import_stop"] = k.energy_Wh_import_stop.value();
    }
    if (k.id_token_hash) {
        j["id_token_hash"] = k.id_token_hash.value();
    }
    if (k.id_token_type) {
        j["id_token_type"] = k.id_token_type.value();
    }
    if (k.authorization_type) {
        j["authorization_type"] = k.authorization_type.value();
    }
    if (k.stop_reason) {
        j["stop_reason"] = k.stop_reason.value();
    }
    if (k.signed_meter_value_start) {
        j["signed_meter_value_start"] = k.signed_meter_value_start.value();
    }
    if (k.signed_meter_value_stop) {
        j["signed_meter_value_stop"] = k.signed_meter_value_stop.value();
    }
}

void from_json(const json& j, Transaction& k) {
    k.timestamp_start = j.at("timestamp_start");
    k.energy_Wh_import_start = j.at("energy_Wh_import_start");

    if (j.contains("timestamp_stop")) {
        k.timestamp_stop.emplace(j.at("timestamp_stop"));
    }
    if (j.contains("energy_Wh_import_stop")) {
        k.energy_Wh_import_stop.emplace(j.at("energy_Wh_import_stop"));
    }
    if (j.contains("id_token_hash")) {
        k.id_token_hash.emplace(j.at("id_token_hash"));
    }
    if (j.contains("id_token_type")) {
        k.id_token_type.emplace(j.at("id_token_type"));
    }
    if (j.contains("authorization_type")) {
        k.authorization_type.emplace(j.at("authorization_type"));
    }
    if (j.contains("stop_reason")) {
        k.stop_reason.emplace(j.at("stop_reason"));
    }
    if (j.contains("signed_meter_value_start")) {
        k.signed_meter_value_start.emplace(j.at("signed_meter_value_start"));
    }
    if (j.contains("signed_meter_value_stop")) {
        k.signed_meter_value_stop.emplace(j.at("signed_meter_value_stop"));
    }
}

void to_json(json& j, Session const& k) noexcept {
    j = json{
        {"session_id", k.session_id},     {"evse_id", k.evse_id}, {"evse_id_string", k.evse_id_string},
        {"connector_id", k.connector_id}, {"state", k.state},     {"timestamp_start", k.timestamp_start},
        {"start_reason", k.start_reason},
    };
    if (k.timestamp_stop) {
        j["timestamp_stop"] = k.timestamp_stop.value();
    }
    if (k.transaction) {
        j["transaction"] = k.transaction.value();
    }
    if (k.ocpp_transaction_id) {
        j["ocpp_transaction_id"] = k.ocpp_transaction_id.value();
    }
    if (k.ocpp_transaction_timestamp_start) {
        j["ocpp_transaction_timestamp_start"] = k.ocpp_transaction_timestamp_start.value();
    }
    if (k.ocpp_transaction_timestamp_stop) {
        j["ocpp_transaction_timestamp_stop"] = k.ocpp_transaction_timestamp_stop.value();
    }
    if (k.cost) {
        j["cost"] = k.cost.value();
    }
}

void from_json(const json& j, Session& k) {
    k.session_id = j.at("session_id");
    k.evse_id = j.at("evse_id");
    k.evse_id_string = j.at("evse_id_string");
    k.connector_id = j.at("connector_id");
    k.state = j.at("state");
    k.timestamp_start = j.at("timestamp_start");
    k.start_reason = j.at("start_reason");

    if (j.contains("timestamp_stop")) {
        k.timestamp_stop.emplace(j.at("timestamp_stop"));
    }
    if (j.contains("transaction")) {
        k.transaction.emplace(j.at("transaction"));
    }
    if (j.contains("ocpp_transaction_id")) {
        k.ocpp_transaction_id.emplace(j.at("ocpp_transaction_id"));
    }
    if (j.contains("ocpp_transaction_timestamp_start")) {
        k.ocpp_transaction_timestamp_start.emplace(j.at("ocpp_transaction_timestamp_start"));
    }
    if (j.contains("ocpp_transaction_timestamp_stop")) {
        k.ocpp_transaction_timestamp_stop.emplace(j.at("ocpp_transaction_timestamp_stop"));
    }
    if (j.contains("cost")) {
        k.cost.emplace(j.at("cost"));
    }
}

void to_json(json& j, SessionFilter const& k) noexcept {
    j = json({});
    if (k.state) {
        j["state"] = k.state.value();
    }
    if (k.evse_id) {
        j["evse_id"] = k.evse_id.value();
    }
    if (k.started_after) {
        j["started_after"] = k.started_after.value();
    }
}

void from_json(const json& j, SessionFilter& k) {
    if (j.contains("state")) {
        k.state.emplace(j.at("state"));
    }
    if (j.contains("evse_id")) {
        k.evse_id.emplace(j.at("evse_id"));
    }
    if (j.contains("started_after")) {
        k.started_after.emplace(j.at("started_after"));
    }
}

void to_json(json& j, GetSessionsRequest const& k) noexcept {
    j = json({});
    if (k.limit) {
        j["limit"] = k.limit.value();
    }
    if (k.continuation_token) {
        j["continuation_token"] = k.continuation_token.value();
    }
    if (k.filter) {
        j["filter"] = k.filter.value();
    }
}

void from_json(const json& j, GetSessionsRequest& k) {
    if (j.contains("limit")) {
        k.limit.emplace(j.at("limit"));
    }
    if (j.contains("continuation_token")) {
        k.continuation_token.emplace(j.at("continuation_token"));
    }
    if (j.contains("filter")) {
        k.filter.emplace(j.at("filter"));
    }
}

void to_json(json& j, SessionList const& k) noexcept {
    j["sessions"] = json::array();
    for (auto const& val : k.sessions) {
        j["sessions"].push_back(val);
    }
    if (k.continuation_token) {
        j["continuation_token"] = k.continuation_token.value();
    }
}

void from_json(const json& j, SessionList& k) {
    k.sessions.clear();
    for (auto const& val : j.at("sessions")) {
        k.sessions.push_back(val);
    }
    if (j.contains("continuation_token")) {
        k.continuation_token.emplace(j.at("continuation_token"));
    }
}

void to_json(json& j, SessionIdentifier const& k) noexcept {
    j = json({});
    if (k.session_id) {
        j["session_id"] = k.session_id.value();
    }
    if (k.ocpp_transaction_id) {
        j["ocpp_transaction_id"] = k.ocpp_transaction_id.value();
    }
}

void from_json(const json& j, SessionIdentifier& k) {
    if (j.contains("session_id")) {
        k.session_id.emplace(j.at("session_id"));
    }
    if (j.contains("ocpp_transaction_id")) {
        k.ocpp_transaction_id.emplace(j.at("ocpp_transaction_id"));
    }
}

void to_json(json& j, ClearSessionsResult const& k) noexcept {
    j = json{
        {"cleared", k.cleared},
    };
}

void from_json(const json& j, ClearSessionsResult& k) {
    k.cleared = j.at("cleared");
}

} // namespace everest::lib::API::V1_0::types::session_storage
