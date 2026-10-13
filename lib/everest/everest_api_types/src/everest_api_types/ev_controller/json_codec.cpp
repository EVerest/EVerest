// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ev_controller/json_codec.hpp"
#include "ev_controller/API.hpp"
#include "ev_controller/codec.hpp"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace everest::lib::API::V1_0::types::ev_controller {

void to_json(json& j, FsmState const& k) {
    switch (k) {
    case FsmState::Disabled:
        j = "Disabled";
        return;
    case FsmState::Unplugged:
        j = "Unplugged";
        return;
    case FsmState::Plugged:
        j = "Plugged";
        return;
    case FsmState::SlacMatching:
        j = "SlacMatching";
        return;
    case FsmState::V2GNegotiating:
        j = "V2GNegotiating";
        return;
    case FsmState::BcbToggling:
        j = "BcbToggling";
        return;
    case FsmState::Charging:
        j = "Charging";
        return;
    case FsmState::ChargingPwmPaused:
        j = "ChargingPwmPaused";
        return;
    case FsmState::Paused:
        j = "Paused";
        return;
    case FsmState::Stopping:
        j = "Stopping";
        return;
    case FsmState::ChargerFault:
        j = "ChargerFault";
        return;
    case FsmState::Faulted:
        j = "Faulted";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::FsmState unknown enum value");
}

void from_json(json const& j, FsmState& k) {
    std::string s = j;
    if (s == "Disabled") {
        k = FsmState::Disabled;
        return;
    }
    if (s == "Unplugged") {
        k = FsmState::Unplugged;
        return;
    }
    if (s == "Plugged") {
        k = FsmState::Plugged;
        return;
    }
    if (s == "SlacMatching") {
        k = FsmState::SlacMatching;
        return;
    }
    if (s == "V2GNegotiating") {
        k = FsmState::V2GNegotiating;
        return;
    }
    if (s == "BcbToggling") {
        k = FsmState::BcbToggling;
        return;
    }
    if (s == "Charging") {
        k = FsmState::Charging;
        return;
    }
    if (s == "ChargingPwmPaused") {
        k = FsmState::ChargingPwmPaused;
        return;
    }
    if (s == "Paused") {
        k = FsmState::Paused;
        return;
    }
    if (s == "Stopping") {
        k = FsmState::Stopping;
        return;
    }
    if (s == "ChargerFault") {
        k = FsmState::ChargerFault;
        return;
    }
    if (s == "Faulted") {
        k = FsmState::Faulted;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::FsmState");
}

void to_json(json& j, ChargeMode const& k) {
    switch (k) {
    case ChargeMode::AcIec:
        j = "AcIec";
        return;
    case ChargeMode::AcIso2:
        j = "AcIso2";
        return;
    case ChargeMode::AcIsoD20DerIec:
        j = "AcIsoD20DerIec";
        return;
    case ChargeMode::AcIsoD20DerSae:
        j = "AcIsoD20DerSae";
        return;
    case ChargeMode::AcIsoD20:
        j = "AcIsoD20";
        return;
    case ChargeMode::DcIso2:
        j = "DcIso2";
        return;
    case ChargeMode::DcIsoD20:
        j = "DcIsoD20";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::ChargeMode unknown enum value");
}

void from_json(json const& j, ChargeMode& k) {
    std::string s = j;
    if (s == "AcIec") {
        k = ChargeMode::AcIec;
        return;
    }
    if (s == "AcIso2") {
        k = ChargeMode::AcIso2;
        return;
    }
    if (s == "AcIsoD20DerIec") {
        k = ChargeMode::AcIsoD20DerIec;
        return;
    }
    if (s == "AcIsoD20DerSae") {
        k = ChargeMode::AcIsoD20DerSae;
        return;
    }
    if (s == "AcIsoD20") {
        k = ChargeMode::AcIsoD20;
        return;
    }
    if (s == "DcIso2") {
        k = ChargeMode::DcIso2;
        return;
    }
    if (s == "DcIsoD20") {
        k = ChargeMode::DcIsoD20;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::ChargeMode");
}

void to_json(json& j, DerFlavor const& k) {
    switch (k) {
    case DerFlavor::IEC:
        j = "IEC";
        return;
    case DerFlavor::SAE:
        j = "SAE";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::DerFlavor unknown enum value");
}

void from_json(json const& j, DerFlavor& k) {
    std::string s = j;
    if (s == "IEC") {
        k = DerFlavor::IEC;
        return;
    }
    if (s == "SAE") {
        k = DerFlavor::SAE;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::DerFlavor");
}

void to_json(json& j, DerControlSource const& k) {
    switch (k) {
    case DerControlSource::ChargeParameterDiscovery:
        j = "ChargeParameterDiscovery";
        return;
    case DerControlSource::ChargeLoop:
        j = "ChargeLoop";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::DerControlSource unknown enum value");
}

void from_json(json const& j, DerControlSource& k) {
    std::string s = j;
    if (s == "ChargeParameterDiscovery") {
        k = DerControlSource::ChargeParameterDiscovery;
        return;
    }
    if (s == "ChargeLoop") {
        k = DerControlSource::ChargeLoop;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::DerControlSource");
}

void to_json(json& j, PaymentOption const& k) {
    switch (k) {
    case PaymentOption::ExternalPayment:
        j = "ExternalPayment";
        return;
    case PaymentOption::Contract:
        j = "Contract";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::PaymentOption unknown enum value");
}

void from_json(json const& j, PaymentOption& k) {
    std::string s = j;
    if (s == "ExternalPayment") {
        k = PaymentOption::ExternalPayment;
        return;
    }
    if (s == "Contract") {
        k = PaymentOption::Contract;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::PaymentOption");
}

void to_json(json& j, FaultType const& k) {
    switch (k) {
    case FaultType::SlacTimeout:
        j = "SlacTimeout";
        return;
    case FaultType::V2GTimeout:
        j = "V2GTimeout";
        return;
    case FaultType::Internal:
        j = "Internal";
        return;
    case FaultType::ChargerFault:
        j = "ChargerFault";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::FaultType unknown enum value");
}

void from_json(json const& j, FaultType& k) {
    std::string s = j;
    if (s == "SlacTimeout") {
        k = FaultType::SlacTimeout;
        return;
    }
    if (s == "V2GTimeout") {
        k = FaultType::V2GTimeout;
        return;
    }
    if (s == "Internal") {
        k = FaultType::Internal;
        return;
    }
    if (s == "ChargerFault") {
        k = FaultType::ChargerFault;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::FaultType");
}

void to_json(json& j, IsoSessionEventKind const& k) {
    switch (k) {
    case IsoSessionEventKind::PowerReady:
        j = "PowerReady";
        return;
    case IsoSessionEventKind::StopFromCharger:
        j = "StopFromCharger";
        return;
    case IsoSessionEventKind::PauseFromCharger:
        j = "PauseFromCharger";
        return;
    case IsoSessionEventKind::V2GFinished:
        j = "V2GFinished";
        return;
    case IsoSessionEventKind::DcPowerOn:
        j = "DcPowerOn";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::IsoSessionEventKind unknown enum value");
}

void from_json(json const& j, IsoSessionEventKind& k) {
    std::string s = j;
    if (s == "PowerReady") {
        k = IsoSessionEventKind::PowerReady;
        return;
    }
    if (s == "StopFromCharger") {
        k = IsoSessionEventKind::StopFromCharger;
        return;
    }
    if (s == "PauseFromCharger") {
        k = IsoSessionEventKind::PauseFromCharger;
        return;
    }
    if (s == "V2GFinished") {
        k = IsoSessionEventKind::V2GFinished;
        return;
    }
    if (s == "DcPowerOn") {
        k = IsoSessionEventKind::DcPowerOn;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::IsoSessionEventKind");
}

void to_json(json& j, CommandAckStatus const& k) {
    switch (k) {
    case CommandAckStatus::Accepted:
        j = "Accepted";
        return;
    case CommandAckStatus::Rejected:
        j = "Rejected";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::CommandAckStatus unknown enum value");
}

void from_json(json const& j, CommandAckStatus& k) {
    std::string s = j;
    if (s == "Accepted") {
        k = CommandAckStatus::Accepted;
        return;
    }
    if (s == "Rejected") {
        k = CommandAckStatus::Rejected;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::CommandAckStatus");
}

void to_json(json& j, BspEventKind const& k) {
    switch (k) {
    case BspEventKind::A:
        j = "A";
        return;
    case BspEventKind::B:
        j = "B";
        return;
    case BspEventKind::C:
        j = "C";
        return;
    case BspEventKind::D:
        j = "D";
        return;
    case BspEventKind::E:
        j = "E";
        return;
    case BspEventKind::F:
        j = "F";
        return;
    case BspEventKind::PowerOn:
        j = "PowerOn";
        return;
    case BspEventKind::PowerOff:
        j = "PowerOff";
        return;
    case BspEventKind::Disconnected:
        j = "Disconnected";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::BspEventKind unknown enum value");
}

void from_json(json const& j, BspEventKind& k) {
    std::string s = j;
    if (s == "A") {
        k = BspEventKind::A;
        return;
    }
    if (s == "B") {
        k = BspEventKind::B;
        return;
    }
    if (s == "C") {
        k = BspEventKind::C;
        return;
    }
    if (s == "D") {
        k = BspEventKind::D;
        return;
    }
    if (s == "E") {
        k = BspEventKind::E;
        return;
    }
    if (s == "F") {
        k = BspEventKind::F;
        return;
    }
    if (s == "PowerOn") {
        k = BspEventKind::PowerOn;
        return;
    }
    if (s == "PowerOff") {
        k = BspEventKind::PowerOff;
        return;
    }
    if (s == "Disconnected") {
        k = BspEventKind::Disconnected;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::BspEventKind");
}

void to_json(json& j, SlacStateKind const& k) {
    switch (k) {
    case SlacStateKind::Unmatched:
        j = "Unmatched";
        return;
    case SlacStateKind::Matching:
        j = "Matching";
        return;
    case SlacStateKind::Matched:
        j = "Matched";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_controller::SlacStateKind unknown enum value");
}

void from_json(json const& j, SlacStateKind& k) {
    std::string s = j;
    if (s == "Unmatched") {
        k = SlacStateKind::Unmatched;
        return;
    }
    if (s == "Matching") {
        k = SlacStateKind::Matching;
        return;
    }
    if (s == "Matched") {
        k = SlacStateKind::Matched;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_controller::SlacStateKind");
}

void to_json(json& j, BptParams const& k) noexcept {
    j = json{
        {"discharge_max_current_limit", k.discharge_max_current_limit},
        {"discharge_max_power_limit", k.discharge_max_power_limit},
        {"discharge_target_current", k.discharge_target_current},
        {"discharge_minimal_soc", k.discharge_minimal_soc},
    };
}

void from_json(json const& j, BptParams& k) {
    k.discharge_max_current_limit = j.at("discharge_max_current_limit");
    k.discharge_max_power_limit = j.at("discharge_max_power_limit");
    k.discharge_target_current = j.at("discharge_target_current");
    k.discharge_minimal_soc = j.at("discharge_minimal_soc");
}

void to_json(json& j, CurvePoint const& k) noexcept {
    j = json{
        {"t_offset_ms", k.t_offset_ms},
        {"current_a", k.current_a},
        {"three_phases", k.three_phases},
    };
    if (k.ramp_ms) {
        j["ramp_ms"] = k.ramp_ms.value();
    }
}

void from_json(json const& j, CurvePoint& k) {
    k.t_offset_ms = j.at("t_offset_ms");
    k.current_a = j.at("current_a");
    k.three_phases = j.at("three_phases");

    if (j.contains("ramp_ms")) {
        k.ramp_ms.emplace(j.at("ramp_ms"));
    }
}

void to_json(json& j, ChargingCurve const& k) noexcept {
    j = json{
        {"points", k.points},
        {"loop", k.loop},
    };
}

void from_json(json const& j, ChargingCurve& k) {
    auto points = j.at("points").get<std::vector<CurvePoint>>();
    auto loop = j.at("loop").get<bool>();
    auto curve = ChargingCurve::make(std::move(points), loop);
    if (!curve) {
        throw std::out_of_range("ChargingCurve points is empty or t_offset_ms not monotonic");
    }
    k = std::move(*curve);
}

namespace {

// Shared field serializers: each alternative emits only the keys it owns,
// so the wire "params" object can never carry a field that the C++ type
// cannot hold.
void put_payment(json& p, std::optional<PaymentOption> const& v) {
    if (v) {
        p["payment"] = v.value();
    }
}
void put_departure(json& p, std::optional<int32_t> const& v) {
    if (v) {
        p["departure_time_s"] = v.value();
    }
}
void put_e_amount(json& p, std::optional<int32_t> const& v) {
    if (v) {
        p["e_amount_wh"] = v.value();
    }
}
void put_current(json& p, std::optional<float> const& v) {
    if (v) {
        p["charging_current_a"] = v.value();
    }
}
void put_three_phases(json& p, std::optional<bool> const& v) {
    if (v) {
        p["three_phases"] = v.value();
    }
}
void put_bpt(json& p, std::optional<BptParams> const& v) {
    if (v) {
        p["bpt"] = v.value();
    }
}
void put_curve(json& p, std::optional<ChargingCurve> const& v) {
    if (v) {
        p["curve"] = v.value();
    }
}

template <class T> void get_opt(json const& j, char const* key, std::optional<T>& out) {
    if (j.contains(key)) {
        out.emplace(j.at(key).get<T>());
    }
}

} // namespace

void to_json(json& j, SessionConfigParams const& k) noexcept {
    json params = json::object();
    std::visit(
        [&](auto const& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, AcIecSessionParams>) {
                put_current(params, v.charging_current_a);
                put_three_phases(params, v.three_phases);
                put_curve(params, v.curve);
            } else if constexpr (std::is_same_v<T, AcIso2SessionParams>) {
                put_payment(params, v.payment);
                put_departure(params, v.departure_time_s);
                put_e_amount(params, v.e_amount_wh);
                put_current(params, v.charging_current_a);
                put_three_phases(params, v.three_phases);
                put_curve(params, v.curve);
            } else if constexpr (std::is_same_v<T, AcIsoD20DerIecSessionParams> ||
                                 std::is_same_v<T, AcIsoD20DerSaeSessionParams>) {
                put_payment(params, v.payment);
                put_departure(params, v.departure_time_s);
                put_e_amount(params, v.e_amount_wh);
                put_current(params, v.charging_current_a);
                put_three_phases(params, v.three_phases);
                put_curve(params, v.curve);
            } else if constexpr (std::is_same_v<T, AcIsoD20SessionParams>) {
                put_payment(params, v.payment);
                put_departure(params, v.departure_time_s);
                put_e_amount(params, v.e_amount_wh);
                put_current(params, v.charging_current_a);
                put_three_phases(params, v.three_phases);
                put_bpt(params, v.bpt);
                put_curve(params, v.curve);
            } else if constexpr (std::is_same_v<T, DcIso2SessionParams>) {
                put_payment(params, v.payment);
                put_departure(params, v.departure_time_s);
                put_e_amount(params, v.e_amount_wh);
                put_curve(params, v.curve);
            } else if constexpr (std::is_same_v<T, DcIsoD20SessionParams>) {
                put_payment(params, v.payment);
                put_departure(params, v.departure_time_s);
                put_e_amount(params, v.e_amount_wh);
                put_bpt(params, v.bpt);
                if (v.mcs_enabled) {
                    params["mcs_enabled"] = true;
                }
                put_curve(params, v.curve);
            }
        },
        k);
    j = json{
        {"mode", mode_of(k)},
        {"params", std::move(params)},
    };
}

void from_json(json const& j, SessionConfigParams& k) {
    auto mode = j.at("mode").get<ChargeMode>();
    // "params" is optional: a mode with all-defaulted fields may omit it.
    const json params = j.contains("params") ? j.at("params") : json::object();

    switch (mode) {
    case ChargeMode::AcIec: {
        AcIecSessionParams v;
        get_opt(params, "charging_current_a", v.charging_current_a);
        get_opt(params, "three_phases", v.three_phases);
        get_opt(params, "curve", v.curve);
        k = std::move(v);
        return;
    }
    case ChargeMode::AcIso2: {
        AcIso2SessionParams v;
        get_opt(params, "payment", v.payment);
        get_opt(params, "departure_time_s", v.departure_time_s);
        get_opt(params, "e_amount_wh", v.e_amount_wh);
        get_opt(params, "charging_current_a", v.charging_current_a);
        get_opt(params, "three_phases", v.three_phases);
        get_opt(params, "curve", v.curve);
        k = std::move(v);
        return;
    }
    case ChargeMode::AcIsoD20DerIec:
    case ChargeMode::AcIsoD20DerSae: {
        AcIsoD20DerParams v;
        get_opt(params, "payment", v.payment);
        get_opt(params, "departure_time_s", v.departure_time_s);
        get_opt(params, "e_amount_wh", v.e_amount_wh);
        get_opt(params, "charging_current_a", v.charging_current_a);
        get_opt(params, "three_phases", v.three_phases);
        get_opt(params, "curve", v.curve);
        if (mode == ChargeMode::AcIsoD20DerIec) {
            k = AcIsoD20DerIecSessionParams{std::move(v)};
        } else {
            k = AcIsoD20DerSaeSessionParams{std::move(v)};
        }
        return;
    }
    case ChargeMode::AcIsoD20: {
        AcIsoD20SessionParams v;
        get_opt(params, "payment", v.payment);
        get_opt(params, "departure_time_s", v.departure_time_s);
        get_opt(params, "e_amount_wh", v.e_amount_wh);
        get_opt(params, "charging_current_a", v.charging_current_a);
        get_opt(params, "three_phases", v.three_phases);
        get_opt(params, "bpt", v.bpt);
        get_opt(params, "curve", v.curve);
        k = std::move(v);
        return;
    }
    case ChargeMode::DcIso2: {
        DcIso2SessionParams v;
        get_opt(params, "payment", v.payment);
        get_opt(params, "departure_time_s", v.departure_time_s);
        get_opt(params, "e_amount_wh", v.e_amount_wh);
        get_opt(params, "curve", v.curve);
        k = std::move(v);
        return;
    }
    case ChargeMode::DcIsoD20: {
        DcIsoD20SessionParams v;
        get_opt(params, "payment", v.payment);
        get_opt(params, "departure_time_s", v.departure_time_s);
        get_opt(params, "e_amount_wh", v.e_amount_wh);
        get_opt(params, "bpt", v.bpt);
        if (params.contains("mcs_enabled")) {
            v.mcs_enabled = params.at("mcs_enabled").get<bool>();
        }
        get_opt(params, "curve", v.curve);
        k = std::move(v);
        return;
    }
    }
    throw std::out_of_range("SessionConfigParams: unknown mode");
}

void to_json(json& j, SetChargingCurrentParams const& k) noexcept {
    j = json{
        {"current_a", k.current_a},
        {"three_phases", k.three_phases},
    };
    if (k.ramp_ms) {
        j["ramp_ms"] = k.ramp_ms.value();
    }
}

void from_json(json const& j, SetChargingCurrentParams& k) {
    k.current_a = j.at("current_a");
    k.three_phases = j.at("three_phases");

    if (j.contains("ramp_ms")) {
        k.ramp_ms.emplace(j.at("ramp_ms"));
    }
}

void to_json(json& j, SetSocParams const& k) noexcept {
    j = json{
        {"soc_pct", k.soc_pct},
    };
}

void from_json(json const& j, SetSocParams& k) {
    k.soc_pct = j.at("soc_pct");
}

void to_json(json& j, SetPresentValuesParams const& k) noexcept {
    j = json::object();
    if (k.present_voltage) {
        j["present_voltage"] = k.present_voltage.value();
    }
    if (k.present_active_power) {
        j["present_active_power"] = k.present_active_power.value();
    }
}
void from_json(json const& j, SetPresentValuesParams& k) {
    if (j.contains("present_voltage")) {
        k.present_voltage.emplace(j.at("present_voltage"));
    }
    if (j.contains("present_active_power")) {
        k.present_active_power.emplace(j.at("present_active_power"));
    }
}

void to_json(json& j, StopSessionParams const& k) noexcept {
    j = json::object();
    j["abort"] = k.abort;
}
void from_json(json const& j, StopSessionParams& k) {
    if (j.contains("abort")) {
        k.abort = j.at("abort");
    }
}

void to_json(json& j, BcbToggleParams const& k) noexcept {
    j = json::object();
    if (k.count) {
        j["count"] = k.count.value();
    }
}

void from_json(json const& j, BcbToggleParams& k) {
    if (j.contains("count")) {
        k.count.emplace(j.at("count"));
    }
}

void to_json(json& j, DerNegotiatedFunctions const& k) noexcept {
    j = json{{"flavor", k.flavor}, {"functions", k.functions}};
}

void from_json(json const& j, DerNegotiatedFunctions& k) {
    k.flavor = j.at("flavor").get<DerFlavor>();
    k.functions = j.at("functions").get<std::vector<std::string>>();
}

void to_json(json& j, DerControl const& k) noexcept {
    j = json{{"flavor", k.flavor}, {"source", k.source}};
    if (k.permit_service) {
        j["permit_service"] = *k.permit_service;
    }
    if (k.enabled_functions) {
        j["enabled_functions"] = *k.enabled_functions;
    }
    if (k.problems) {
        j["problems"] = *k.problems;
    }
    if (k.dso_q_setpoint) {
        j["dso_q_setpoint"] = *k.dso_q_setpoint;
    }
    if (k.dso_cos_phi_setpoint) {
        j["dso_cos_phi_setpoint"] = *k.dso_cos_phi_setpoint;
    }
}

void from_json(json const& j, DerControl& k) {
    k.flavor = j.at("flavor").get<DerFlavor>();
    k.source = j.at("source").get<DerControlSource>();
    get_opt(j, "permit_service", k.permit_service);
    get_opt(j, "enabled_functions", k.enabled_functions);
    get_opt(j, "problems", k.problems);
    get_opt(j, "dso_q_setpoint", k.dso_q_setpoint);
    get_opt(j, "dso_cos_phi_setpoint", k.dso_cos_phi_setpoint);
}

void to_json(json& j, EvInfo const& k) noexcept {
    j = json{
        {"soc_pct", k.soc_pct},
        {"battery_capacity_wh", k.battery_capacity_wh},
        {"battery_charge_wh", k.battery_charge_wh},
        {"target_current_a", k.target_current_a},
        {"target_voltage_v", k.target_voltage_v},
    };
}

void from_json(json const& j, EvInfo& k) {
    k.soc_pct = j.at("soc_pct");
    k.battery_capacity_wh = j.at("battery_capacity_wh");
    k.battery_charge_wh = j.at("battery_charge_wh");
    k.target_current_a = j.at("target_current_a");
    k.target_voltage_v = j.at("target_voltage_v");
}

void to_json(json& j, IsoSessionEvent const& k) noexcept {
    j = json{
        {"kind", k.kind},
    };
    if (k.dc_voltage_v) {
        j["dc_voltage_v"] = k.dc_voltage_v.value();
    }
    if (k.dc_current_a) {
        j["dc_current_a"] = k.dc_current_a.value();
    }
}

void from_json(json const& j, IsoSessionEvent& k) {
    k.kind = j.at("kind");

    if (j.contains("dc_voltage_v")) {
        k.dc_voltage_v.emplace(j.at("dc_voltage_v"));
    }
    if (j.contains("dc_current_a")) {
        k.dc_current_a.emplace(j.at("dc_current_a"));
    }
}

void to_json(json& j, BspEvent const& k) noexcept {
    j = json{
        {"event", k.event},
    };
}

void from_json(json const& j, BspEvent& k) {
    k.event = j.at("event").get<BspEventKind>();
}

void to_json(json& j, SlacState const& k) noexcept {
    j = json{
        {"state", k.state},
    };
}

void from_json(json const& j, SlacState& k) {
    k.state = j.at("state").get<SlacStateKind>();
}

void to_json(json& j, FaultReport const& k) noexcept {
    j = json{
        {"type", k.type},
    };
    if (k.message) {
        j["message"] = k.message.value();
    }
    if (k.rcd_mA) {
        j["rcd_mA"] = k.rcd_mA.value();
    }
}

void from_json(json const& j, FaultReport& k) {
    k.type = j.at("type");

    if (j.contains("message")) {
        k.message.emplace(j.at("message"));
    }
    if (j.contains("rcd_mA")) {
        k.rcd_mA.emplace(j.at("rcd_mA"));
    }
}

void to_json(json& j, CommandAck const& k) noexcept {
    j = json{
        {"command", k.command},
        {"status", k.status},
    };
    if (k.reason) {
        j["reason"] = k.reason.value();
    }
}

void from_json(json const& j, CommandAck& k) {
    k.command = j.at("command");
    k.status = j.at("status");

    if (j.contains("reason")) {
        k.reason.emplace(j.at("reason"));
    }
}

} // namespace everest::lib::API::V1_0::types::ev_controller
