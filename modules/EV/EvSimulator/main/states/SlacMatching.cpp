// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SlacMatching.hpp"

#include "../FsmContext.hpp"
#include "Faulted.hpp"
#include "Unplugged.hpp"
#include "V2GNegotiating.hpp"

#include <chrono>

namespace module {

namespace api = API_types::ev_simulator;

void SlacMatching::enter() {
    ctx.slac_trigger_matching();
    ctx.vars.slac_state = "MATCHING";
    ctx.arm_timer(std::chrono::seconds(30));
    ctx.publish_e2m_state(api::FsmState::SlacMatching);
}

StateBase::Result SlacMatching::feed(EventType ev) {
    using EK = EventKind;
    switch (ev.kind) {
    case EK::SlacState: {
        const auto& p = std::get<SlacStatePayload>(ev.payload);
        if (p.state == "MATCHED") {
            return {false, std::make_unique<V2GNegotiating>(ctx)};
        }
        return {true, nullptr};
    }
    case EK::StateDeadline:
        ctx.vars.last_fault =
            api::FaultReport{api::FaultType::SlacTimeout, std::string{"SLAC match deadline exceeded"}, std::nullopt};
        return {false, std::make_unique<Faulted>(ctx)};
    case EK::Unplug:
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::BspEvent: {
        const auto& p = std::get<BspEventPayload>(ev.payload);
        if (::types::board_support_common::event_to_string(p.bsp_event.event) == "Disconnected") {
            return {false, std::make_unique<Unplugged>(ctx)};
        }
        return {true, nullptr};
    }
    case EK::InjectFault: {
        auto p = std::get<api::InjectFaultParams>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::SlacMatching);
    case EK::StartSession:
        ctx.publish_e2m_command_ack("start_session", "slac matching in progress");
        return {false, nullptr};
    case EK::StopSession:
        ctx.publish_e2m_command_ack("stop_session", "slac matching in progress");
        return {false, nullptr};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "slac matching in progress");
        return {false, nullptr};
    case EK::ResumeSession:
        ctx.publish_e2m_command_ack("resume_session", "slac matching in progress");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "slac matching in progress");
        return {false, nullptr};
    case EK::ClearFault:
        ctx.publish_e2m_command_ack("clear_fault", "slac matching in progress");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "slac matching in progress");
        return {false, nullptr};
    case EK::SetSoc:
        ctx.publish_e2m_command_ack("set_soc", "slac matching in progress");
        return {false, nullptr};
    case EK::RunScenario:
        ctx.publish_e2m_command_ack("run_scenario", "slac matching in progress");
        return {false, nullptr};
    case EK::Plug:
    case EK::Enable:
    case EK::BspMeasurement:
    case EK::EvInfo:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoStopFromCharger:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
