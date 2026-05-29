// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "BcbToggling.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"
#include "V2GNegotiating.hpp"

#include <chrono>

namespace module {

namespace api = API_types::ev_simulator;

void BcbToggling::enter() {
    // bcb_remaining is pre-set by Paused::feed(ResumeSession) (= 6 = 3 round-trips × 2 edges);
    // default to 6 here for robustness if entered via a path that didn't seed it.
    if (ctx.vars.bcb_remaining == 0) {
        ctx.vars.bcb_remaining = 6;
    }
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.arm_timer(std::chrono::milliseconds(250));
    ctx.publish_e2m_state(api::FsmState::BcbToggling);
}

StateBase::Result BcbToggling::feed(EventType ev) {
    using EK = EventKind;
    using CpS = ::types::ev_board_support::EvCpState;
    switch (ev.kind) {
    case EK::StateDeadline: {
        // Parity: even → set_cp(C); odd → set_cp(B). Then decrement; when 0 → V2GNegotiating.
        if (ctx.vars.bcb_remaining % 2 == 0) {
            ctx.set_cp(CpS::C);
        } else {
            ctx.set_cp(CpS::B);
        }
        --ctx.vars.bcb_remaining;
        if (ctx.vars.bcb_remaining == 0) {
            return {false, std::make_unique<V2GNegotiating>(ctx)};
        }
        ctx.arm_timer(std::chrono::milliseconds(250));
        return {false, nullptr};
    }
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
        return handle_query_state(ctx, api::FsmState::BcbToggling);
    case EK::StartSession:
        ctx.publish_e2m_command_ack("start_session", "BCB toggling in progress");
        return {false, nullptr};
    case EK::StopSession:
        ctx.publish_e2m_command_ack("stop_session", "BCB toggling in progress");
        return {false, nullptr};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "BCB toggling in progress");
        return {false, nullptr};
    case EK::ResumeSession:
        ctx.publish_e2m_command_ack("resume_session", "BCB toggling in progress");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "BCB toggling in progress");
        return {false, nullptr};
    case EK::ClearFault:
        ctx.publish_e2m_command_ack("clear_fault", "BCB toggling in progress");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "BCB toggling in progress");
        return {false, nullptr};
    case EK::SetSoc:
        ctx.publish_e2m_command_ack("set_soc", "BCB toggling in progress");
        return {false, nullptr};
    case EK::RunScenario:
        ctx.publish_e2m_command_ack("run_scenario", "BCB toggling in progress");
        return {false, nullptr};
    case EK::Plug:
    case EK::Enable:
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
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
