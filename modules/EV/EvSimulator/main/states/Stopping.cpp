// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Stopping.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"

#include <chrono>

namespace module {

namespace api = API_types::ev_simulator;

void Stopping::enter() {
    ctx.iso_stop_charging();
    ctx.allow_power_on(false);
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.arm_timer(std::chrono::seconds(10));
    ctx.publish_e2m_state(api::FsmState::Stopping);
}

StateBase::Result Stopping::feed(EventType ev) {
    using EK = EventKind;
    switch (ev.kind) {
    case EK::IsoV2GFinished:
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::StateDeadline:
        // Forced timeout - fall through to Unplugged.
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
        return handle_query_state(ctx, api::FsmState::Stopping);
    case EK::StartSession:
        ctx.publish_e2m_command_ack("start_session", "session stopping");
        return {false, nullptr};
    case EK::StopSession:
        ctx.publish_e2m_command_ack("stop_session", "session stopping");
        return {false, nullptr};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "session stopping");
        return {false, nullptr};
    case EK::ResumeSession:
        ctx.publish_e2m_command_ack("resume_session", "session stopping");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "session stopping");
        return {false, nullptr};
    case EK::ClearFault:
        ctx.publish_e2m_command_ack("clear_fault", "session stopping");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "session stopping");
        return {false, nullptr};
    case EK::SetSoc:
        ctx.publish_e2m_command_ack("set_soc", "session stopping");
        return {false, nullptr};
    case EK::RunScenario:
        ctx.publish_e2m_command_ack("run_scenario", "session stopping");
        return {false, nullptr};
    case EK::Unplug:
    case EK::Plug:
    case EK::Enable:
    case EK::BspMeasurement:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoStopFromCharger:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
