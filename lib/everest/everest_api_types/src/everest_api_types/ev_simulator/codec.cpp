// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "ev_simulator/codec.hpp"
#include "ev_simulator/API.hpp"
#include "ev_simulator/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include <string>
#include <utility>
#include <variant>

namespace everest::lib::API::V1_0::types::ev_simulator {

std::optional<ChargingCurve> ChargingCurve::make(std::vector<CurvePoint> points, bool loop) {
    if (points.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (points[i].t_offset_ms <= points[i - 1].t_offset_ms) {
            return std::nullopt;
        }
    }
    ChargingCurve curve;
    curve.points = std::move(points);
    curve.loop = loop;
    return curve;
}

ChargeMode mode_of(const SessionConfigParams& p) {
    struct Visitor {
        ChargeMode operator()(const AcIecSessionParams&) const {
            return ChargeMode::AcIec;
        }
        ChargeMode operator()(const AcIso2SessionParams&) const {
            return ChargeMode::AcIso2;
        }
        ChargeMode operator()(const AcIsoD20SessionParams&) const {
            return ChargeMode::AcIsoD20;
        }
        ChargeMode operator()(const DcIso2SessionParams&) const {
            return ChargeMode::DcIso2;
        }
        ChargeMode operator()(const DcIsoD20SessionParams&) const {
            return ChargeMode::DcIsoD20;
        }
    };
    return std::visit(Visitor{}, p);
}

std::string serialize(FsmState val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ChargeMode val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(PaymentOption val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(FaultType val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ScenarioName val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(IsoSessionEventKind val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(CommandAckStatus val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(BspEventKind val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(SlacStateKind val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(BptParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(CurvePoint const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ChargingCurve const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(SessionConfigParams const& val) noexcept {
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

std::string serialize(ScenarioTimingOverrides const& val) noexcept {
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

std::ostream& operator<<(std::ostream& os, BspEventKind const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SlacStateKind const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, BptParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CurvePoint const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ChargingCurve const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SessionConfigParams const& val) {
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

std::ostream& operator<<(std::ostream& os, ScenarioTimingOverrides const& val) {
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

template <> BspEventKind deserialize(std::string const& val) {
    return json::parse(val);
}

template <> SlacStateKind deserialize(std::string const& val) {
    return json::parse(val);
}

template <> BptParams deserialize(std::string const& val) {
    return json::parse(val);
}

template <> CurvePoint deserialize(std::string const& val) {
    return json::parse(val);
}

template <> ChargingCurve deserialize(std::string const& val) {
    return json::parse(val);
}

template <> SessionConfigParams deserialize(std::string const& val) {
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

template <> ScenarioTimingOverrides deserialize(std::string const& val) {
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
