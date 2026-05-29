// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "ev_simulator/codec.hpp"
#include "ev_simulator/API.hpp"
#include "ev_simulator/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include <string>

namespace everest::lib::API::V1_0::types::ev_simulator {

std::string serialize(FsmState val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ChargeMode val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(PaymentOption val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(FaultType val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ScenarioName val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(IsoSessionEventKind val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(CommandAckStatus val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(StartSessionParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(SetChargingCurrentParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(SetSocParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(BcbToggleParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(InjectFaultParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(RunScenarioParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(EvInfo const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(IsoSessionEvent const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(BspEvent const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(SlacState const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(FaultReport const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(CommandAck const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::ostream& operator<<(std::ostream& os, FsmState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ChargeMode const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, PaymentOption const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FaultType const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ScenarioName const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, IsoSessionEventKind const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CommandAckStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, StartSessionParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SetChargingCurrentParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SetSocParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, BcbToggleParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, InjectFaultParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, RunScenarioParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EvInfo const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, IsoSessionEvent const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, BspEvent const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SlacState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FaultReport const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CommandAck const& val) {
    os << serialize(val);
    return os;
}

template <> FsmState deserialize(std::string const& val) {
    return json::parse(val);
}

template <> ChargeMode deserialize(std::string const& val) {
    return json::parse(val);
}

template <> PaymentOption deserialize(std::string const& val) {
    return json::parse(val);
}

template <> FaultType deserialize(std::string const& val) {
    return json::parse(val);
}

template <> ScenarioName deserialize(std::string const& val) {
    return json::parse(val);
}

template <> IsoSessionEventKind deserialize(std::string const& val) {
    return json::parse(val);
}

template <> CommandAckStatus deserialize(std::string const& val) {
    return json::parse(val);
}

template <> StartSessionParams deserialize(std::string const& val) {
    return json::parse(val);
}

template <> SetChargingCurrentParams deserialize(std::string const& val) {
    return json::parse(val);
}

template <> SetSocParams deserialize(std::string const& val) {
    return json::parse(val);
}

template <> BcbToggleParams deserialize(std::string const& val) {
    return json::parse(val);
}

template <> InjectFaultParams deserialize(std::string const& val) {
    return json::parse(val);
}

template <> RunScenarioParams deserialize(std::string const& val) {
    return json::parse(val);
}

template <> EvInfo deserialize(std::string const& val) {
    return json::parse(val);
}

template <> IsoSessionEvent deserialize(std::string const& val) {
    return json::parse(val);
}

template <> BspEvent deserialize(std::string const& val) {
    return json::parse(val);
}

template <> SlacState deserialize(std::string const& val) {
    return json::parse(val);
}

template <> FaultReport deserialize(std::string const& val) {
    return json::parse(val);
}

template <> CommandAck deserialize(std::string const& val) {
    return json::parse(val);
}

} // namespace everest::lib::API::V1_0::types::ev_simulator
