// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "session_storage/wrapper.hpp"
#include "auth/wrapper.hpp"
#include "evse_manager/wrapper.hpp"
#include "powermeter/wrapper.hpp"
#include "session_cost/wrapper.hpp"
#include "session_storage/API.hpp"
#include <vector>

namespace everest::lib::API::V1_0::types {

namespace {
using namespace session_storage;
using namespace auth;
using namespace evse_manager;
using namespace powermeter;
using namespace session_cost;

template <class SrcT, class ConvT>
auto srcToTarOpt(std::optional<SrcT> const& src, ConvT const& converter)
    -> std::optional<decltype(converter(src.value()))> {
    if (src) {
        return std::make_optional(converter(src.value()));
    }
    return std::nullopt;
}

template <class SrcT, class ConvT> auto srcToTarVec(std::vector<SrcT> const& src, ConvT const& converter) {
    using TarT = decltype(converter(src[0]));
    std::vector<TarT> result;
    for (SrcT const& elem : src) {
        result.push_back(converter(elem));
    }
    return result;
}

template <class SrcT>
auto optToInternal(std::optional<SrcT> const& src) -> std::optional<decltype(to_internal_api(src.value()))> {
    return srcToTarOpt(src, [](SrcT const& val) { return to_internal_api(val); });
}

template <class SrcT>
auto optToExternal(std::optional<SrcT> const& src) -> std::optional<decltype(to_external_api(src.value()))> {
    return srcToTarOpt(src, [](SrcT const& val) { return to_external_api(val); });
}

template <class SrcT> auto vecToExternal(std::vector<SrcT> const& src) {
    return srcToTarVec(src, [](SrcT const& val) { return to_external_api(val); });
}

template <class SrcT> auto vecToInternal(std::vector<SrcT> const& src) {
    return srcToTarVec(src, [](SrcT const& val) { return to_internal_api(val); });
}

} // namespace

namespace session_storage {

SessionState_Internal to_internal_api(SessionState_External const& val) {
    using SrcT = SessionState_External;
    using TarT = SessionState_Internal;
    switch (val) {
    case SrcT::Open:
        return TarT::Open;
    case SrcT::Finished:
        return TarT::Finished;
    case SrcT::Stale:
        return TarT::Stale;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::session_storage::SessionState_External");
}

SessionState_External to_external_api(SessionState_Internal const& val) {
    using SrcT = SessionState_Internal;
    using TarT = SessionState_External;
    switch (val) {
    case SrcT::Open:
        return TarT::Open;
    case SrcT::Finished:
        return TarT::Finished;
    case SrcT::Stale:
        return TarT::Stale;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::session_storage::SessionState_Internal");
}

Transaction_Internal to_internal_api(Transaction_External const& val) {
    Transaction_Internal result;
    result.timestamp_start = val.timestamp_start;
    result.energy_Wh_import_start = val.energy_Wh_import_start;
    result.timestamp_stop = val.timestamp_stop;
    result.energy_Wh_import_stop = val.energy_Wh_import_stop;
    result.id_token_hash = val.id_token_hash;
    result.id_token_type = optToInternal(val.id_token_type);
    result.authorization_type = optToInternal(val.authorization_type);
    result.stop_reason = optToInternal(val.stop_reason);
    result.signed_meter_value_start = optToInternal(val.signed_meter_value_start);
    result.signed_meter_value_stop = optToInternal(val.signed_meter_value_stop);
    return result;
}

Transaction_External to_external_api(Transaction_Internal const& val) {
    Transaction_External result;
    result.timestamp_start = val.timestamp_start;
    result.energy_Wh_import_start = val.energy_Wh_import_start;
    result.timestamp_stop = val.timestamp_stop;
    result.energy_Wh_import_stop = val.energy_Wh_import_stop;
    result.id_token_hash = val.id_token_hash;
    result.id_token_type = optToExternal(val.id_token_type);
    result.authorization_type = optToExternal(val.authorization_type);
    result.stop_reason = optToExternal(val.stop_reason);
    result.signed_meter_value_start = optToExternal(val.signed_meter_value_start);
    result.signed_meter_value_stop = optToExternal(val.signed_meter_value_stop);
    return result;
}

Session_Internal to_internal_api(Session_External const& val) {
    Session_Internal result;
    result.session_id = val.session_id;
    result.evse_id = val.evse_id;
    result.evse_id_string = val.evse_id_string;
    result.connector_id = val.connector_id;
    result.state = to_internal_api(val.state);
    result.timestamp_start = val.timestamp_start;
    result.start_reason = evse_manager::to_internal_api(val.start_reason);
    result.timestamp_stop = val.timestamp_stop;
    result.transaction = optToInternal(val.transaction);
    result.ocpp_transaction_id = val.ocpp_transaction_id;
    result.ocpp_transaction_timestamp_start = val.ocpp_transaction_timestamp_start;
    result.ocpp_transaction_timestamp_stop = val.ocpp_transaction_timestamp_stop;
    result.cost = optToInternal(val.cost);
    return result;
}

Session_External to_external_api(Session_Internal const& val) {
    Session_External result;
    result.session_id = val.session_id;
    result.evse_id = val.evse_id;
    result.evse_id_string = val.evse_id_string;
    result.connector_id = val.connector_id;
    result.state = to_external_api(val.state);
    result.timestamp_start = val.timestamp_start;
    result.start_reason = evse_manager::to_external_api(val.start_reason);
    result.timestamp_stop = val.timestamp_stop;
    result.transaction = optToExternal(val.transaction);
    result.ocpp_transaction_id = val.ocpp_transaction_id;
    result.ocpp_transaction_timestamp_start = val.ocpp_transaction_timestamp_start;
    result.ocpp_transaction_timestamp_stop = val.ocpp_transaction_timestamp_stop;
    result.cost = optToExternal(val.cost);
    return result;
}

SessionFilter_Internal to_internal_api(SessionFilter_External const& val) {
    SessionFilter_Internal result;
    result.state = optToInternal(val.state);
    result.evse_id = val.evse_id;
    result.started_after = val.started_after;
    return result;
}

SessionFilter_External to_external_api(SessionFilter_Internal const& val) {
    SessionFilter_External result;
    result.state = optToExternal(val.state);
    result.evse_id = val.evse_id;
    result.started_after = val.started_after;
    return result;
}

GetSessionsRequest_Internal to_internal_api(GetSessionsRequest_External const& val) {
    GetSessionsRequest_Internal result;
    result.limit = val.limit;
    result.continuation_token = val.continuation_token;
    result.filter = optToInternal(val.filter);
    return result;
}

GetSessionsRequest_External to_external_api(GetSessionsRequest_Internal const& val) {
    GetSessionsRequest_External result;
    result.limit = val.limit;
    result.continuation_token = val.continuation_token;
    result.filter = optToExternal(val.filter);
    return result;
}

SessionList_Internal to_internal_api(SessionList_External const& val) {
    SessionList_Internal result;
    result.sessions = vecToInternal(val.sessions);
    result.continuation_token = val.continuation_token;
    return result;
}

SessionList_External to_external_api(SessionList_Internal const& val) {
    SessionList_External result;
    result.sessions = vecToExternal(val.sessions);
    result.continuation_token = val.continuation_token;
    return result;
}

SessionIdentifier_Internal to_internal_api(SessionIdentifier_External const& val) {
    SessionIdentifier_Internal result;
    result.session_id = val.session_id;
    result.ocpp_transaction_id = val.ocpp_transaction_id;
    return result;
}

SessionIdentifier_External to_external_api(SessionIdentifier_Internal const& val) {
    SessionIdentifier_External result;
    result.session_id = val.session_id;
    result.ocpp_transaction_id = val.ocpp_transaction_id;
    return result;
}

ClearSessionsResult_Internal to_internal_api(ClearSessionsResult_External const& val) {
    ClearSessionsResult_Internal result;
    result.cleared = val.cleared;
    return result;
}

ClearSessionsResult_External to_external_api(ClearSessionsResult_Internal const& val) {
    ClearSessionsResult_External result;
    result.cleared = val.cleared;
    return result;
}

} // namespace session_storage
} // namespace everest::lib::API::V1_0::types
