// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Events.hpp"

#include <variant>

namespace module {

namespace {

namespace api = API_types::ev_simulator;

// Explicit overload set: one operator() per variant alternative, no generic
// `auto` fallback. std::visit requires the visitor to handle every
// alternative, so an alternative added to EventPayload without a matching
// overload here is a compile error.
struct KindVisitor {
    EventKind operator()(const EnableCmd&) const {
        return EventKind::Enable;
    }
    EventKind operator()(const DisableCmd&) const {
        return EventKind::Disable;
    }
    EventKind operator()(const PlugCmd&) const {
        return EventKind::Plug;
    }
    EventKind operator()(const UnplugCmd&) const {
        return EventKind::Unplug;
    }
    EventKind operator()(const api::SetSocParams&) const {
        return EventKind::SetSoc;
    }
    EventKind operator()(const api::SessionConfigParams&) const {
        return EventKind::ConfigureSession;
    }
    EventKind operator()(const StopSessionCmd&) const {
        return EventKind::StopSession;
    }
    EventKind operator()(const PauseSessionCmd&) const {
        return EventKind::PauseSession;
    }
    EventKind operator()(const ResumeSessionCmd&) const {
        return EventKind::ResumeSession;
    }
    EventKind operator()(const api::SetChargingCurrentParams&) const {
        return EventKind::SetChargingCurrent;
    }
    EventKind operator()(const api::InjectFaultParams&) const {
        return EventKind::InjectFault;
    }
    EventKind operator()(const ClearFaultCmd&) const {
        return EventKind::ClearFault;
    }
    EventKind operator()(const api::BcbToggleParams&) const {
        return EventKind::BcbToggle;
    }
    EventKind operator()(const api::RunScenarioParams&) const {
        return EventKind::RunScenario;
    }
    EventKind operator()(const RaiseErrorCmd&) const {
        return EventKind::RaiseError;
    }
    EventKind operator()(const ClearErrorCmd&) const {
        return EventKind::ClearError;
    }
    EventKind operator()(const QueryStateCmd&) const {
        return EventKind::QueryState;
    }
    EventKind operator()(const BspEventPayload&) const {
        return EventKind::BspEvent;
    }
    EventKind operator()(const BspMeasurementPayload&) const {
        return EventKind::BspMeasurement;
    }
    EventKind operator()(const EvInfoPayload&) const {
        return EventKind::EvInfo;
    }
    EventKind operator()(const SlacStatePayload&) const {
        return EventKind::SlacState;
    }
    EventKind operator()(const IsoPowerReadyEvt&) const {
        return EventKind::IsoPowerReady;
    }
    EventKind operator()(const IsoAcMaxCurrentEvt&) const {
        return EventKind::IsoAcMaxCurrent;
    }
    EventKind operator()(const IsoAcTargetPowerEvt&) const {
        return EventKind::IsoAcTargetPower;
    }
    EventKind operator()(const IsoStopFromChargerEvt&) const {
        return EventKind::IsoStopFromCharger;
    }
    EventKind operator()(const IsoV2GFinishedEvt&) const {
        return EventKind::IsoV2GFinished;
    }
    EventKind operator()(const IsoDcPowerOnEvt&) const {
        return EventKind::IsoDcPowerOn;
    }
    EventKind operator()(const IsoPauseFromChargerEvt&) const {
        return EventKind::IsoPauseFromCharger;
    }
    EventKind operator()(const DcEvsePresentCurrentPayload&) const {
        return EventKind::DcEvsePresentCurrent;
    }
    EventKind operator()(const DcEvsePresentVoltagePayload&) const {
        return EventKind::DcEvsePresentVoltage;
    }
    EventKind operator()(const BeginSessionEvt&) const {
        return EventKind::BeginSession;
    }
    EventKind operator()(const StateDeadlineEvt&) const {
        return EventKind::StateDeadline;
    }
    EventKind operator()(const ShutdownEvt&) const {
        return EventKind::Shutdown;
    }
};

} // namespace

namespace detail {

EventPayload default_payload_for(EventKind kind) {
    switch (kind) {
    case EventKind::Enable:
        return EnableCmd{};
    case EventKind::Disable:
        return DisableCmd{};
    case EventKind::Plug:
        return PlugCmd{};
    case EventKind::Unplug:
        return UnplugCmd{};
    case EventKind::SetSoc:
        return api::SetSocParams{};
    case EventKind::ConfigureSession:
        return api::SessionConfigParams{};
    case EventKind::StopSession:
        return StopSessionCmd{};
    case EventKind::PauseSession:
        return PauseSessionCmd{};
    case EventKind::ResumeSession:
        return ResumeSessionCmd{};
    case EventKind::SetChargingCurrent:
        return api::SetChargingCurrentParams{};
    case EventKind::InjectFault:
        return api::InjectFaultParams{};
    case EventKind::ClearFault:
        return ClearFaultCmd{};
    case EventKind::BcbToggle:
        return api::BcbToggleParams{};
    case EventKind::RunScenario:
        return api::RunScenarioParams{};
    case EventKind::RaiseError:
        return RaiseErrorCmd{};
    case EventKind::ClearError:
        return ClearErrorCmd{};
    case EventKind::QueryState:
        return QueryStateCmd{};
    case EventKind::BspEvent:
        return BspEventPayload{};
    case EventKind::BspMeasurement:
        return BspMeasurementPayload{};
    case EventKind::EvInfo:
        return EvInfoPayload{};
    case EventKind::SlacState:
        return SlacStatePayload{};
    case EventKind::IsoPowerReady:
        return IsoPowerReadyEvt{};
    case EventKind::IsoAcMaxCurrent:
        return IsoAcMaxCurrentEvt{};
    case EventKind::IsoAcTargetPower:
        return IsoAcTargetPowerEvt{};
    case EventKind::IsoStopFromCharger:
        return IsoStopFromChargerEvt{};
    case EventKind::IsoV2GFinished:
        return IsoV2GFinishedEvt{};
    case EventKind::IsoDcPowerOn:
        return IsoDcPowerOnEvt{};
    case EventKind::IsoPauseFromCharger:
        return IsoPauseFromChargerEvt{};
    case EventKind::DcEvsePresentCurrent:
        return DcEvsePresentCurrentPayload{};
    case EventKind::DcEvsePresentVoltage:
        return DcEvsePresentVoltagePayload{};
    case EventKind::BeginSession:
        return BeginSessionEvt{};
    case EventKind::StateDeadline:
        return StateDeadlineEvt{};
    case EventKind::Shutdown:
        return ShutdownEvt{};
    }
    // -Werror=switch makes a missing case a compile error; this satisfies
    // control-flow analysis for an out-of-range cast value.
    return EnableCmd{};
}

} // namespace detail

EventKind kind_of(const Event& ev) {
    return std::visit(KindVisitor{}, ev.payload);
}

std::string_view command_verb(EventKind kind) {
    switch (kind) {
    case EventKind::Plug:
        return "plug";
    case EventKind::Unplug:
        return "unplug";
    case EventKind::SetSoc:
        return "set_soc";
    case EventKind::ConfigureSession:
        return "configure_session";
    case EventKind::StopSession:
        return "stop_session";
    case EventKind::PauseSession:
        return "pause_session";
    case EventKind::ResumeSession:
        return "resume_session";
    case EventKind::SetChargingCurrent:
        return "set_charging_current";
    case EventKind::InjectFault:
        return "inject_fault";
    case EventKind::ClearFault:
        return "clear_fault";
    case EventKind::BcbToggle:
        return "bcb_toggle";
    case EventKind::RunScenario:
        return "run_scenario";
    // Non-command kinds never reach a blanket reject() arm; an empty verb
    // keeps command_verb total without inventing a literal.
    case EventKind::Enable:
    case EventKind::Disable:
    case EventKind::RaiseError:
    case EventKind::ClearError:
    case EventKind::QueryState:
    case EventKind::BspEvent:
    case EventKind::BspMeasurement:
    case EventKind::EvInfo:
    case EventKind::SlacState:
    case EventKind::IsoPowerReady:
    case EventKind::IsoAcMaxCurrent:
    case EventKind::IsoAcTargetPower:
    case EventKind::IsoStopFromCharger:
    case EventKind::IsoV2GFinished:
    case EventKind::IsoDcPowerOn:
    case EventKind::IsoPauseFromCharger:
    case EventKind::DcEvsePresentCurrent:
    case EventKind::DcEvsePresentVoltage:
    case EventKind::BeginSession:
    case EventKind::StateDeadline:
    case EventKind::Shutdown:
        return "";
    }
    // -Werror=switch makes a missing case a compile error; this satisfies
    // control-flow analysis for an out-of-range cast value.
    return "";
}

} // namespace module
