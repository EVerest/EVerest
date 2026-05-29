// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Unplugged.hpp"

#include "../FsmContext.hpp"
#include "../ScenarioDispatcher.hpp"
#include "Plugged.hpp"

namespace module {

namespace api = API_types::ev_simulator;

void Unplugged::enter() {
    ctx.set_cp(::types::ev_board_support::EvCpState::A);
    ctx.allow_power_on(false);
    ctx.iso_stop_charging();
    ctx.vars.charge_mode.reset();
    ctx.vars.last_fault.reset();
    ctx.persisted.plugged_in = false;
    ctx.kvs_save();
    ctx.publish_e2m_state(api::FsmState::Unplugged);
}

StateBase::Result Unplugged::feed(EventType ev) {
    using EK = EventKind;
    switch (ev.kind) {
    case EK::Plug:
        return {false, std::make_unique<Plugged>(ctx)};
    case EK::RunScenario: {
        auto p = std::get<api::RunScenarioParams>(ev.payload);
        start_scenario(p.name, ctx);
        return {false, nullptr};
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::SetSoc: {
        auto p = std::get<api::SetSocParams>(ev.payload);
        ctx.vars.battery_charge_wh = (p.soc_pct / 100.0f) * ctx.vars.battery_capacity_wh;
        ctx.vars.soc_pct = p.soc_pct;
        return {false, nullptr};
    }
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Unplugged);
    case EK::StartSession:
        ctx.publish_e2m_command_ack("start_session", "no session active");
        return {false, nullptr};
    case EK::StopSession:
        ctx.publish_e2m_command_ack("stop_session", "no session active");
        return {false, nullptr};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "no session active");
        return {false, nullptr};
    case EK::ResumeSession:
        ctx.publish_e2m_command_ack("resume_session", "no session active");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "no session active");
        return {false, nullptr};
    case EK::InjectFault:
        ctx.publish_e2m_command_ack("inject_fault", "no session active");
        return {false, nullptr};
    case EK::ClearFault:
        ctx.publish_e2m_command_ack("clear_fault", "no session active");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "no session active");
        return {false, nullptr};
    case EK::Unplug:
        return {true, nullptr};
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
    case EK::Enable:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
