// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ev_controller/codec.hpp"
#include "ev_controller/API.hpp"
#include "ev_controller/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace everest::lib::API::V1_0::types::ev_controller {

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
        ChargeMode operator()(const AcIsoD20DerIecSessionParams&) const {
            return ChargeMode::AcIsoD20DerIec;
        }
        ChargeMode operator()(const AcIsoD20DerSaeSessionParams&) const {
            return ChargeMode::AcIsoD20DerSae;
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

std::string serialize(DerFlavor val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(DerControlSource val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(FaultType val) {
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

std::string serialize(DerNegotiatedFunctions const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(DerControl const& val) noexcept {
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

std::string serialize(SetPresentValuesParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(StopSessionParams const& val) noexcept {
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

std::ostream& operator<<(std::ostream& os, DerFlavor const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DerControlSource const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FaultType const& val) {
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

std::ostream& operator<<(std::ostream& os, DerNegotiatedFunctions const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DerControl const& val) {
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

std::ostream& operator<<(std::ostream& os, SetPresentValuesParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, StopSessionParams const& val) {
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

template <> FsmState deserialize(std::string_view val) {
    return utilities::parse_json<FsmState>(val);
}

template <> ChargeMode deserialize(std::string_view val) {
    return utilities::parse_json<ChargeMode>(val);
}

template <> PaymentOption deserialize(std::string_view val) {
    return utilities::parse_json<PaymentOption>(val);
}

template <> DerFlavor deserialize(std::string_view val) {
    return utilities::parse_json<DerFlavor>(val);
}

template <> DerControlSource deserialize(std::string_view val) {
    return utilities::parse_json<DerControlSource>(val);
}

template <> FaultType deserialize(std::string_view val) {
    return utilities::parse_json<FaultType>(val);
}

template <> IsoSessionEventKind deserialize(std::string_view val) {
    return utilities::parse_json<IsoSessionEventKind>(val);
}

template <> CommandAckStatus deserialize(std::string_view val) {
    return utilities::parse_json<CommandAckStatus>(val);
}

template <> BspEventKind deserialize(std::string_view val) {
    return utilities::parse_json<BspEventKind>(val);
}

template <> SlacStateKind deserialize(std::string_view val) {
    return utilities::parse_json<SlacStateKind>(val);
}

template <> BptParams deserialize(std::string_view val) {
    return utilities::parse_json<BptParams>(val);
}

template <> DerNegotiatedFunctions deserialize(std::string_view val) {
    return utilities::parse_json<DerNegotiatedFunctions>(val);
}

template <> DerControl deserialize(std::string_view val) {
    return utilities::parse_json<DerControl>(val);
}

template <> CurvePoint deserialize(std::string_view val) {
    return utilities::parse_json<CurvePoint>(val);
}

template <> ChargingCurve deserialize(std::string_view val) {
    return utilities::parse_json<ChargingCurve>(val);
}

template <> SessionConfigParams deserialize(std::string_view val) {
    return utilities::parse_json<SessionConfigParams>(val);
}

template <> SetChargingCurrentParams deserialize(std::string_view val) {
    return utilities::parse_json<SetChargingCurrentParams>(val);
}

template <> SetSocParams deserialize(std::string_view val) {
    return utilities::parse_json<SetSocParams>(val);
}

template <> BcbToggleParams deserialize(std::string_view val) {
    return utilities::parse_json<BcbToggleParams>(val);
}

template <> SetPresentValuesParams deserialize(std::string_view val) {
    return utilities::parse_json<SetPresentValuesParams>(val);
}

template <> StopSessionParams deserialize(std::string_view val) {
    return utilities::parse_json<StopSessionParams>(val);
}

template <> EvInfo deserialize(std::string_view val) {
    return utilities::parse_json<EvInfo>(val);
}

template <> IsoSessionEvent deserialize(std::string_view val) {
    return utilities::parse_json<IsoSessionEvent>(val);
}

template <> BspEvent deserialize(std::string_view val) {
    return utilities::parse_json<BspEvent>(val);
}

template <> SlacState deserialize(std::string_view val) {
    return utilities::parse_json<SlacState>(val);
}

template <> FaultReport deserialize(std::string_view val) {
    return utilities::parse_json<FaultReport>(val);
}

template <> CommandAck deserialize(std::string_view val) {
    return utilities::parse_json<CommandAck>(val);
}

} // namespace everest::lib::API::V1_0::types::ev_controller
