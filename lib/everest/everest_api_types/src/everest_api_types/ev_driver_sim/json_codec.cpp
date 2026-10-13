// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ev_driver_sim/json_codec.hpp"
#include "ev_driver_sim/API.hpp"
#include "ev_driver_sim/codec.hpp"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::ev_driver_sim {

void to_json(json& j, WorldFault const& k) {
    switch (k) {
    case WorldFault::DiodeFail:
        j = "DiodeFail";
        return;
    case WorldFault::RcdError:
        j = "RcdError";
        return;
    case WorldFault::CpErrorE:
        j = "CpErrorE";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_driver_sim::WorldFault unknown enum value");
}

void from_json(json const& j, WorldFault& k) {
    std::string s = j;
    if (s == "DiodeFail") {
        k = WorldFault::DiodeFail;
        return;
    }
    if (s == "RcdError") {
        k = WorldFault::RcdError;
        return;
    }
    if (s == "CpErrorE") {
        k = WorldFault::CpErrorE;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_driver_sim::WorldFault");
}

void to_json(json& j, PilotState const& k) {
    switch (k) {
    case PilotState::Withheld:
        j = "Withheld";
        return;
    case PilotState::Passed:
        j = "Passed";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_driver_sim::PilotState unknown enum value");
}

void from_json(json const& j, PilotState& k) {
    std::string s = j;
    if (s == "Withheld") {
        k = PilotState::Withheld;
        return;
    }
    if (s == "Passed") {
        k = PilotState::Passed;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_driver_sim::PilotState");
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
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_driver_sim::CommandAckStatus unknown enum value");
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
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_driver_sim::CommandAckStatus");
}

void to_json(json& j, InjectFaultParams const& k) noexcept {
    j = json{{"type", k.type}};
    if (k.rcd_mA) {
        j["rcd_mA"] = k.rcd_mA.value();
    }
    if (k.message) {
        j["message"] = k.message.value();
    }
}

void from_json(json const& j, InjectFaultParams& k) {
    k.type = j.at("type");
    if (j.contains("rcd_mA")) {
        k.rcd_mA.emplace(j.at("rcd_mA").get<float>());
    }
    if (j.contains("message")) {
        k.message.emplace(j.at("message").get<std::string>());
    }
}

void to_json(json& j, WorldState const& k) noexcept {
    j = json{{"pilot", k.pilot}};
    if (k.fault) {
        j["fault"] = k.fault.value();
    }
}

void from_json(json const& j, WorldState& k) {
    k.pilot = j.at("pilot");
    if (j.contains("fault")) {
        k.fault.emplace(j.at("fault").get<WorldFault>());
    }
}

void to_json(json& j, CommandAck const& k) noexcept {
    j = json{{"command", k.command}, {"status", k.status}};
    if (k.message) {
        j["reason"] = k.message.value();
    }
}

void from_json(json const& j, CommandAck& k) {
    k.command = j.at("command");
    k.status = j.at("status");
    if (j.contains("reason")) {
        k.message.emplace(j.at("reason").get<std::string>());
    }
}

void to_json(json& j, ScenarioName const& k) {
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
    case ScenarioName::AcIecRampUp:
        j = "AcIecRampUp";
        return;
    case ScenarioName::DcIsoTaper:
        j = "DcIsoTaper";
        return;
    }
    throw std::out_of_range("everest::lib::API::V1_0::types::ev_driver_sim::ScenarioName unknown enum value");
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
    if (s == "AcIecRampUp") {
        k = ScenarioName::AcIecRampUp;
        return;
    }
    if (s == "DcIsoTaper") {
        k = ScenarioName::DcIsoTaper;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::ev_driver_sim::ScenarioName");
}

void to_json(json& j, ScenarioTimingOverrides const& k) noexcept {
    j = json::object();
    if (k.pause_at_ms) {
        j["pause_at_ms"] = k.pause_at_ms.value();
    }
    if (k.resume_at_ms) {
        j["resume_at_ms"] = k.resume_at_ms.value();
    }
    if (k.stop_after_ms) {
        j["stop_after_ms"] = k.stop_after_ms.value();
    }
    if (k.unplug_after_ms) {
        j["unplug_after_ms"] = k.unplug_after_ms.value();
    }
    if (k.fault_at_ms) {
        j["fault_at_ms"] = k.fault_at_ms.value();
    }
    if (k.clear_fault_at_ms) {
        j["clear_fault_at_ms"] = k.clear_fault_at_ms.value();
    }
}

void from_json(json const& j, ScenarioTimingOverrides& k) {
    if (j.contains("pause_at_ms")) {
        k.pause_at_ms.emplace(j.at("pause_at_ms"));
    }
    if (j.contains("resume_at_ms")) {
        k.resume_at_ms.emplace(j.at("resume_at_ms"));
    }
    if (j.contains("stop_after_ms")) {
        k.stop_after_ms.emplace(j.at("stop_after_ms"));
    }
    if (j.contains("unplug_after_ms")) {
        k.unplug_after_ms.emplace(j.at("unplug_after_ms"));
    }
    if (j.contains("fault_at_ms")) {
        k.fault_at_ms.emplace(j.at("fault_at_ms"));
    }
    if (j.contains("clear_fault_at_ms")) {
        k.clear_fault_at_ms.emplace(j.at("clear_fault_at_ms"));
    }
}

void to_json(json& j, RunScenarioParams const& k) noexcept {
    j = json{{"name", k.name}};
    if (k.timing) {
        j["timing"] = k.timing.value();
    }
}

void from_json(json const& j, RunScenarioParams& k) {
    k.name = j.at("name");
    if (j.contains("timing")) {
        k.timing.emplace(j.at("timing"));
    }
}

void to_json(json& j, ScenarioEventKind const& k) {
    switch (k) {
    case ScenarioEventKind::Started:
        j = "Started";
        return;
    case ScenarioEventKind::StepFired:
        j = "StepFired";
        return;
    case ScenarioEventKind::Finished:
        j = "Finished";
        return;
    case ScenarioEventKind::Rejected:
        j = "Rejected";
        return;
    }
    throw std::out_of_range("unknown ScenarioEventKind");
}
void from_json(json const& j, ScenarioEventKind& k) {
    const std::string s = j;
    if (s == "Started") {
        k = ScenarioEventKind::Started;
        return;
    }
    if (s == "StepFired") {
        k = ScenarioEventKind::StepFired;
        return;
    }
    if (s == "Finished") {
        k = ScenarioEventKind::Finished;
        return;
    }
    if (s == "Rejected") {
        k = ScenarioEventKind::Rejected;
        return;
    }
    throw std::out_of_range("unknown ScenarioEventKind: " + s);
}
void to_json(json& j, ScenarioEvent const& k) noexcept {
    j = json{{"name", k.name}, {"kind", k.kind}};
    if (k.message) {
        j["message"] = *k.message;
    }
}
void from_json(json const& j, ScenarioEvent& k) {
    k.name = j.at("name");
    k.kind = j.at("kind");
    if (j.contains("message")) {
        k.message.emplace(j.at("message").get<std::string>());
    }
}

} // namespace everest::lib::API::V1_0::types::ev_driver_sim
