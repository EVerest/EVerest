// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "ev_simulator/json_codec.hpp"
#include "ev_simulator/API.hpp"
#include "ev_simulator/codec.hpp"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::ev_simulator {

void to_json(json& j, FsmState const& k) noexcept {
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
    case FsmState::Faulted:
        j = "Faulted";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::ev_simulator::FsmState";
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
    if (s == "Faulted") {
        k = FsmState::Faulted;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_simulator::FsmState");
}

void to_json(json& j, ChargeMode const& k) noexcept {
    switch (k) {
    case ChargeMode::AcIec:
        j = "AcIec";
        return;
    case ChargeMode::AcIso2:
        j = "AcIso2";
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
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::ev_simulator::ChargeMode";
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
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_simulator::ChargeMode");
}

void to_json(json& j, PaymentOption const& k) noexcept {
    switch (k) {
    case PaymentOption::ExternalPayment:
        j = "ExternalPayment";
        return;
    case PaymentOption::Contract:
        j = "Contract";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::ev_simulator::PaymentOption";
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
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_simulator::PaymentOption");
}

void to_json(json& j, FaultType const& k) noexcept {
    switch (k) {
    case FaultType::DiodeFail:
        j = "DiodeFail";
        return;
    case FaultType::RcdError:
        j = "RcdError";
        return;
    case FaultType::CpErrorE:
        j = "CpErrorE";
        return;
    case FaultType::SlacTimeout:
        j = "SlacTimeout";
        return;
    case FaultType::V2GTimeout:
        j = "V2GTimeout";
        return;
    case FaultType::Internal:
        j = "Internal";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::ev_simulator::FaultType";
}

void from_json(json const& j, FaultType& k) {
    std::string s = j;
    if (s == "DiodeFail") {
        k = FaultType::DiodeFail;
        return;
    }
    if (s == "RcdError") {
        k = FaultType::RcdError;
        return;
    }
    if (s == "CpErrorE") {
        k = FaultType::CpErrorE;
        return;
    }
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

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_simulator::FaultType");
}

void to_json(json& j, ScenarioName const& k) noexcept {
    switch (k) {
    case ScenarioName::AcIecBasic:
        j = "AcIecBasic";
        return;
    case ScenarioName::AcIecPauseResume:
        j = "AcIecPauseResume";
        return;
    case ScenarioName::AcIsoBasic:
        j = "AcIsoBasic";
        return;
    case ScenarioName::AcIsoD20Basic:
        j = "AcIsoD20Basic";
        return;
    case ScenarioName::DcIsoBasic:
        j = "DcIsoBasic";
        return;
    case ScenarioName::DcIsoD20Basic:
        j = "DcIsoD20Basic";
        return;
    case ScenarioName::DcIsoPauseResume:
        j = "DcIsoPauseResume";
        return;
    case ScenarioName::DcIsoBpt:
        j = "DcIsoBpt";
        return;
    case ScenarioName::DcIsoMcs:
        j = "DcIsoMcs";
        return;
    case ScenarioName::DiodeFailSmoke:
        j = "DiodeFailSmoke";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::ev_simulator::ScenarioName";
}

void from_json(json const& j, ScenarioName& k) {
    std::string s = j;
    if (s == "AcIecBasic") {
        k = ScenarioName::AcIecBasic;
        return;
    }
    if (s == "AcIecPauseResume") {
        k = ScenarioName::AcIecPauseResume;
        return;
    }
    if (s == "AcIsoBasic") {
        k = ScenarioName::AcIsoBasic;
        return;
    }
    if (s == "AcIsoD20Basic") {
        k = ScenarioName::AcIsoD20Basic;
        return;
    }
    if (s == "DcIsoBasic") {
        k = ScenarioName::DcIsoBasic;
        return;
    }
    if (s == "DcIsoD20Basic") {
        k = ScenarioName::DcIsoD20Basic;
        return;
    }
    if (s == "DcIsoPauseResume") {
        k = ScenarioName::DcIsoPauseResume;
        return;
    }
    if (s == "DcIsoBpt") {
        k = ScenarioName::DcIsoBpt;
        return;
    }
    if (s == "DcIsoMcs") {
        k = ScenarioName::DcIsoMcs;
        return;
    }
    if (s == "DiodeFailSmoke") {
        k = ScenarioName::DiodeFailSmoke;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_simulator::ScenarioName");
}

void to_json(json& j, IsoSessionEventKind const& k) noexcept {
    switch (k) {
    case IsoSessionEventKind::V2GStarted:
        j = "V2GStarted";
        return;
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
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::ev_simulator::IsoSessionEventKind";
}

void from_json(json const& j, IsoSessionEventKind& k) {
    std::string s = j;
    if (s == "V2GStarted") {
        k = IsoSessionEventKind::V2GStarted;
        return;
    }
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
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_simulator::IsoSessionEventKind");
}

void to_json(json& j, CommandAckStatus const& k) noexcept {
    switch (k) {
    case CommandAckStatus::Accepted:
        j = "Accepted";
        return;
    case CommandAckStatus::Rejected:
        j = "Rejected";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::ev_simulator::CommandAckStatus";
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
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_simulator::CommandAckStatus");
}

void to_json(json& j, StartSessionParams const& k) noexcept {
    j = json{
        {"mode", k.mode},
    };
    if (k.payment) {
        j["payment"] = k.payment.value();
    }
    if (k.departure_time_s) {
        j["departure_time_s"] = k.departure_time_s.value();
    }
    if (k.e_amount_wh) {
        j["e_amount_wh"] = k.e_amount_wh.value();
    }
    if (k.charging_current_a) {
        j["charging_current_a"] = k.charging_current_a.value();
    }
    if (k.three_phases) {
        j["three_phases"] = k.three_phases.value();
    }
}

void from_json(json const& j, StartSessionParams& k) {
    k.mode = j.at("mode");

    if (j.contains("payment")) {
        k.payment.emplace(j.at("payment"));
    }
    if (j.contains("departure_time_s")) {
        k.departure_time_s.emplace(j.at("departure_time_s"));
    }
    if (j.contains("e_amount_wh")) {
        k.e_amount_wh.emplace(j.at("e_amount_wh"));
    }
    if (j.contains("charging_current_a")) {
        k.charging_current_a.emplace(j.at("charging_current_a"));
    }
    if (j.contains("three_phases")) {
        k.three_phases.emplace(j.at("three_phases"));
    }
}

void to_json(json& j, SetChargingCurrentParams const& k) noexcept {
    j = json{
        {"current_a", k.current_a},
        {"three_phases", k.three_phases},
    };
}

void from_json(json const& j, SetChargingCurrentParams& k) {
    k.current_a = j.at("current_a");
    k.three_phases = j.at("three_phases");
}

void to_json(json& j, SetSocParams const& k) noexcept {
    j = json{
        {"soc_pct", k.soc_pct},
    };
}

void from_json(json const& j, SetSocParams& k) {
    k.soc_pct = j.at("soc_pct");
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

void to_json(json& j, InjectFaultParams const& k) noexcept {
    j = json{
        {"type", k.type},
    };
    if (k.rcd_mA) {
        j["rcd_mA"] = k.rcd_mA.value();
    }
}

void from_json(json const& j, InjectFaultParams& k) {
    k.type = j.at("type");

    if (j.contains("rcd_mA")) {
        k.rcd_mA.emplace(j.at("rcd_mA"));
    }
}

void to_json(json& j, RunScenarioParams const& k) noexcept {
    j = json{
        {"name", k.name},
    };
}

void from_json(json const& j, RunScenarioParams& k) {
    k.name = j.at("name");
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
    k.event = j.at("event");
}

void to_json(json& j, SlacState const& k) noexcept {
    j = json{
        {"state", k.state},
    };
}

void from_json(json const& j, SlacState& k) {
    k.state = j.at("state");
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

} // namespace everest::lib::API::V1_0::types::ev_simulator
