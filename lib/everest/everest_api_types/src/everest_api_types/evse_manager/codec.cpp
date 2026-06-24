// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "evse_manager/codec.hpp"
#include "evse_manager/API.hpp"
#include "evse_manager/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::evse_manager {

std::string serialize(StopTransactionReason val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(StopTransactionRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(StartSessionReason val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SessionEventEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SessionEvent const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Limits const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EVInfo const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(CarManufacturer val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SessionStarted const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SessionFinished const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(TransactionStarted const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(TransactionFinished const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ChargingStateChangedEvent const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(AuthorizationEvent const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorSeverity val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorOrigin const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Error const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConnectorTypeEnum const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Connector const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Evse const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EnableSourceEnum const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EnableStateEnum const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EnableDisableSource const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EnableDisableRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(AuthorizeResponseArgs const& val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(PlugAndChargeConfiguration const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EvseStateEnum const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SessionInfo const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(PauseChargingEVSEReasonEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ChargingPausedEVSEReasons const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(HlcSessionFailedReasonEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(HlcSessionFailedEvent const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, StopTransactionReason const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, StopTransactionRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, StartSessionReason const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionEventEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionEvent const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Limits const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EVInfo const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CarManufacturer const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionStarted const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionFinished const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, TransactionStarted const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, TransactionFinished const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ChargingStateChangedEvent const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, AuthorizationEvent const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ErrorSeverity const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ErrorState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ErrorOrigin const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Error const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConnectorTypeEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Connector const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Evse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EnableSourceEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EnableStateEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EnableDisableSource const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EnableDisableRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, AuthorizeResponseArgs const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, PlugAndChargeConfiguration const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EvseStateEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionInfo const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, PauseChargingEVSEReasonEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ChargingPausedEVSEReasons const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, HlcSessionFailedReasonEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, HlcSessionFailedEvent const& val) {
    os << serialize(val);
    return os;
}

template <> StopTransactionReason deserialize(std::string_view val) {
    return utilities::parse_json<StopTransactionReason>(val);
}

template <> StopTransactionRequest deserialize(std::string_view val) {
    return utilities::parse_json<StopTransactionRequest>(val);
}

template <> StartSessionReason deserialize(std::string_view val) {
    return utilities::parse_json<StartSessionReason>(val);
}

template <> SessionEventEnum deserialize(std::string_view val) {
    return utilities::parse_json<SessionEventEnum>(val);
}

template <> SessionEvent deserialize(std::string_view val) {
    return utilities::parse_json<SessionEvent>(val);
}

template <> Limits deserialize(std::string_view val) {
    return utilities::parse_json<Limits>(val);
}

template <> EVInfo deserialize(std::string_view val) {
    return utilities::parse_json<EVInfo>(val);
}

template <> CarManufacturer deserialize(std::string_view val) {
    return utilities::parse_json<CarManufacturer>(val);
}

template <> SessionStarted deserialize(std::string_view val) {
    return utilities::parse_json<SessionStarted>(val);
}

template <> SessionFinished deserialize(std::string_view val) {
    return utilities::parse_json<SessionFinished>(val);
}

template <> TransactionStarted deserialize(std::string_view val) {
    return utilities::parse_json<TransactionStarted>(val);
}

template <> TransactionFinished deserialize(std::string_view val) {
    return utilities::parse_json<TransactionFinished>(val);
}

template <> ChargingStateChangedEvent deserialize(std::string_view val) {
    return utilities::parse_json<ChargingStateChangedEvent>(val);
}

template <> AuthorizationEvent deserialize(std::string_view val) {
    return utilities::parse_json<AuthorizationEvent>(val);
}

template <> ErrorSeverity deserialize(std::string_view val) {
    return utilities::parse_json<ErrorSeverity>(val);
}

template <> ErrorState deserialize(std::string_view val) {
    return utilities::parse_json<ErrorState>(val);
}

template <> ErrorOrigin deserialize(std::string_view val) {
    return utilities::parse_json<ErrorOrigin>(val);
}

template <> Error deserialize(std::string_view val) {
    return utilities::parse_json<Error>(val);
}

template <> ConnectorTypeEnum deserialize(std::string_view val) {
    return utilities::parse_json<ConnectorTypeEnum>(val);
}

template <> Connector deserialize(std::string_view val) {
    return utilities::parse_json<Connector>(val);
}

template <> Evse deserialize(std::string_view val) {
    return utilities::parse_json<Evse>(val);
}

template <> EnableSourceEnum deserialize(std::string_view val) {
    return utilities::parse_json<EnableSourceEnum>(val);
}

template <> EnableStateEnum deserialize(std::string_view val) {
    return utilities::parse_json<EnableStateEnum>(val);
}

template <> EnableDisableSource deserialize(std::string_view val) {
    return utilities::parse_json<EnableDisableSource>(val);
}

template <> EnableDisableRequest deserialize(std::string_view val) {
    return utilities::parse_json<EnableDisableRequest>(val);
}

template <> AuthorizeResponseArgs deserialize(std::string_view val) {
    return utilities::parse_json<AuthorizeResponseArgs>(val);
}

template <> PlugAndChargeConfiguration deserialize(std::string_view val) {
    return utilities::parse_json<PlugAndChargeConfiguration>(val);
}

template <> EvseStateEnum deserialize(std::string_view val) {
    return utilities::parse_json<EvseStateEnum>(val);
}

template <> SessionInfo deserialize(std::string_view val) {
    return utilities::parse_json<SessionInfo>(val);
}

template <> PauseChargingEVSEReasonEnum deserialize(std::string_view val) {
    return utilities::parse_json<PauseChargingEVSEReasonEnum>(val);
}

template <> ChargingPausedEVSEReasons deserialize(std::string_view val) {
    return utilities::parse_json<ChargingPausedEVSEReasons>(val);
}

template <> HlcSessionFailedReasonEnum deserialize(std::string_view val) {
    return utilities::parse_json<HlcSessionFailedReasonEnum>(val);
}

template <> HlcSessionFailedEvent deserialize(std::string_view val) {
    return utilities::parse_json<HlcSessionFailedEvent>(val);
}

} // namespace everest::lib::API::V1_0::types::evse_manager
