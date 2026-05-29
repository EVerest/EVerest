// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Disabled.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"

namespace module {

void Disabled::enter() {
    ctx.publish_e2m_state(API_types::ev_simulator::FsmState::Disabled);
}

StateBase::Result Disabled::feed(EventType ev) {
    using EK = EventKind;
    switch (ev.kind) {
    case EK::Enable:
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::QueryState:
        return handle_query_state(ctx, API_types::ev_simulator::FsmState::Disabled);
    case EK::Disable:
        return {true, nullptr};
    case EK::InjectFault:
        ctx.publish_e2m_command_ack("inject_fault", "InjectFault requires session");
        return {false, nullptr};
    case EK::StartSession:
        ctx.publish_e2m_command_ack("start_session", "module disabled");
        return {false, nullptr};
    case EK::StopSession:
        ctx.publish_e2m_command_ack("stop_session", "module disabled");
        return {false, nullptr};
    case EK::SetSoc:
        ctx.publish_e2m_command_ack("set_soc", "module disabled");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "module disabled");
        return {false, nullptr};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "module disabled");
        return {false, nullptr};
    case EK::ResumeSession:
        ctx.publish_e2m_command_ack("resume_session", "module disabled");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "module disabled");
        return {false, nullptr};
    case EK::RunScenario:
        ctx.publish_e2m_command_ack("run_scenario", "module disabled");
        return {false, nullptr};
    case EK::ClearFault:
        ctx.publish_e2m_command_ack("clear_fault", "module disabled");
        return {false, nullptr};
    case EK::Plug:
        ctx.publish_e2m_command_ack("plug", "module disabled");
        return {false, nullptr};
    case EK::Unplug:
        ctx.publish_e2m_command_ack("unplug", "module disabled");
        return {false, nullptr};
    case EK::BspEvent:
    case EK::BspMeasurement:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoStopFromCharger:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
    case EK::StateDeadline:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
