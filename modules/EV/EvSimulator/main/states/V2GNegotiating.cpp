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
    // iso_start_charging returns false when the ISO peer is not wired (or for
    // AcIec, which has no ISO session); both indicate a misconfiguration when
    // we reach this state. Without an early fault transition the state would
    // arm the 60s deadline timer and only fault later with a misleading
    // V2GTimeout. Route into Faulted on the next on_wake flush via a synthetic
    // InjectFault, carrying the descriptive message on the payload so it
    // cannot go stale if an intervening transition is flushed first.
    const auto cm = ctx.vars.charge_mode();
    if (cm && !ctx.iso_start_charging(*cm, ctx.vars.payment(), ctx.vars.departure_time_s, ctx.vars.e_amount_wh)) {
        ctx.enqueue(Event{api::InjectFaultParams{api::FaultType::Internal, std::nullopt,
                                                 std::string{"iso_start_charging rejected"}}});
        ctx.publish_e2m_state(api::FsmState::V2GNegotiating);
        return;
    }
    ctx.arm_timer(std::chrono::seconds(60));
    ctx.publish_e2m_state(api::FsmState::V2GNegotiating);
}

StateBase::Result V2GNegotiating::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
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
    case EK::BspEvent:
        return handle_disconnect(ev);
    case EK::InjectFault: {
        auto p = std::get<api::InjectFaultParams>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::V2GNegotiating);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::ClearFault:
    case EK::BcbToggle:
    case EK::SetSoc:
    case EK::RunScenario:
        return reject(ev, "negotiation in progress");
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
    // RaiseError / ClearError are intercepted on the loop thread before the
    // FSM feed; listed only to keep the switch exhaustive (-Werror=switch).
    // ConfigureSession is intercepted pre-FSM (loop thread); BeginSession is
    // an internal Plugged-only self-advance. Listed for switch exhaustiveness.
    case EK::ConfigureSession:
    case EK::BeginSession:
    case EK::RaiseError:
    case EK::ClearError:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
