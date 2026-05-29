// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "V2GNegotiating.hpp"

#include "../FsmContext.hpp"
#include "Charging.hpp"
#include "Faulted.hpp"
#include "Stopping.hpp"
#include "Unplugged.hpp"

#include <chrono>

namespace module {

namespace api = API_types::ev_simulator;

void V2GNegotiating::enter() {
    if (ctx.vars.charge_mode) {
        ctx.iso_start_charging(*ctx.vars.charge_mode, /*payment*/ std::nullopt, ctx.vars.departure_time_s,
                               ctx.vars.e_amount_wh);
    }
    ctx.arm_timer(std::chrono::seconds(60));
    ctx.publish_e2m_state(api::FsmState::V2GNegotiating);
}

StateBase::Result V2GNegotiating::feed(EventType ev) {
    using EK = EventKind;
    switch (ev.kind) {
    case EK::IsoPowerReady:
        return {false, std::make_unique<Charging>(ctx)};
    case EK::IsoStopFromCharger:
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::Unplug:
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::StateDeadline:
        ctx.vars.last_fault = api::FaultReport{api::FaultType::V2GTimeout,
                                               std::string{"V2G negotiation deadline exceeded"}, std::nullopt};
        return {false, std::make_unique<Faulted>(ctx)};
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
        return handle_query_state(ctx, api::FsmState::V2GNegotiating);
    case EK::StartSession:
        ctx.publish_e2m_command_ack("start_session", "negotiation in progress");
        return {false, nullptr};
    case EK::StopSession:
        ctx.publish_e2m_command_ack("stop_session", "negotiation in progress");
        return {false, nullptr};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "negotiation in progress");
        return {false, nullptr};
    case EK::ResumeSession:
        ctx.publish_e2m_command_ack("resume_session", "negotiation in progress");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "negotiation in progress");
        return {false, nullptr};
    case EK::ClearFault:
        ctx.publish_e2m_command_ack("clear_fault", "negotiation in progress");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "negotiation in progress");
        return {false, nullptr};
    case EK::SetSoc:
        ctx.publish_e2m_command_ack("set_soc", "negotiation in progress");
        return {false, nullptr};
    case EK::RunScenario:
        ctx.publish_e2m_command_ack("run_scenario", "negotiation in progress");
        return {false, nullptr};
    case EK::Plug:
    case EK::Enable:
    case EK::BspMeasurement:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
