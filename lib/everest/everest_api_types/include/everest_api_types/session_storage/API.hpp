// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <everest_api_types/auth/API.hpp>
#include <everest_api_types/evse_manager/API.hpp>
#include <everest_api_types/powermeter/API.hpp>
#include <everest_api_types/session_cost/API.hpp>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::session_storage {

enum class SessionState {
    Open,
    Finished,
    Stale,
};

struct Transaction {
    std::string timestamp_start;
    float energy_Wh_import_start;
    std::optional<std::string> timestamp_stop;
    std::optional<float> energy_Wh_import_stop;
    std::optional<std::string> id_token_hash;
    std::optional<auth::IdTokenType> id_token_type;
    std::optional<auth::AuthorizationType> authorization_type;
    std::optional<evse_manager::StopTransactionReason> stop_reason;
    std::optional<powermeter::SignedMeterValue> signed_meter_value_start;
    std::optional<powermeter::SignedMeterValue> signed_meter_value_stop;
};

struct Session {
    std::string session_id;
    int32_t evse_id;
    std::string evse_id_string;
    int32_t connector_id;
    SessionState state;
    std::string timestamp_start;
    evse_manager::StartSessionReason start_reason;
    std::optional<std::string> timestamp_stop;
    std::optional<Transaction> transaction;
    std::optional<std::string> ocpp_transaction_id;
    std::optional<std::string> ocpp_transaction_timestamp_start;
    std::optional<std::string> ocpp_transaction_timestamp_stop;
    std::optional<session_cost::SessionCost> cost;
};

struct SessionFilter {
    std::optional<SessionState> state;
    std::optional<int32_t> evse_id;
    std::optional<std::string> started_after;
};

struct GetSessionsRequest {
    std::optional<int32_t> limit;
    std::optional<std::string> continuation_token;
    std::optional<SessionFilter> filter;
};

struct SessionList {
    std::vector<Session> sessions;
    std::optional<std::string> continuation_token;
};

struct SessionIdentifier {
    std::optional<std::string> session_id;
    std::optional<std::string> ocpp_transaction_id;
};

struct ClearSessionsResult {
    int32_t cleared;
};

} // namespace everest::lib::API::V1_0::types::session_storage
