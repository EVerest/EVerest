// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Paused.hpp"

#include "../FsmContext.hpp"
#include "BcbToggling.hpp"
#include "Stopping.hpp"
#include "Unplugged.hpp"

#include <chrono>

namespace module {

namespace api = API_types::ev_simulator;

void Paused::enter() {
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.allow_power_on(false);
    ctx.iso_pause_charging();
    ctx.arm_timer(std::chrono::hours(1));
    ctx.publish_e2m_state(api::FsmState::Paused);
}

StateBase::Result Paused::feed(EventType ev) {
    using EK = EventKind;
    switch (ev.kind) {
    case EK::ResumeSession:
        ctx.vars.bcb_remaining = 6; // 3 round-trips × 2 edges
        return {false, std::make_unique<BcbToggling>(ctx)};
    case EK::Unplug:
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::StateDeadline:
        return {false, std::make_unique<Stopping>(ctx)};
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
        return handle_query_state(ctx, api::FsmState::Paused);
    case EK::StartSession:
        ctx.publish_e2m_command_ack("start_session", "session paused");
        return {false, nullptr};
    case EK::StopSession:
        ctx.publish_e2m_command_ack("stop_session", "session paused");
        return {false, nullptr};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "session paused");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "session paused");
        return {false, nullptr};
    case EK::ClearFault:
        ctx.publish_e2m_command_ack("clear_fault", "session paused");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "session paused");
        return {false, nullptr};
    case EK::SetSoc:
        ctx.publish_e2m_command_ack("set_soc", "session paused");
        return {false, nullptr};
    case EK::RunScenario:
        ctx.publish_e2m_command_ack("run_scenario", "session paused");
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
