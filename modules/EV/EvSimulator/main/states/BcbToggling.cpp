// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "BcbToggling.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"
#include "V2GNegotiating.hpp"

#include <everest/logging.hpp>

#include <chrono>

namespace module {

namespace api = API_types::ev_simulator;

void BcbToggling::enter() {
    // bcb_remaining is pre-set by callers: Paused::feed(ResumeSession) seeds 6
    // (= 3 round-trips × 2 edges); Paused::feed(BcbToggle) seeds
    // params.count * 2 (defaulting to 6 when count is absent). Default to 6
    // here for robustness if entered via a path that did not seed it.
    if (ctx.vars.bcb_remaining == 0) {
        EVLOG_warning << "EvSimulator: BcbToggling::enter found bcb_remaining=0; reseeding to default 6 "
                         "(caller should have set this)";
        ctx.vars.bcb_remaining = 6;
    }
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.arm_timer(std::chrono::milliseconds(250));
    ctx.publish_e2m_state(api::FsmState::BcbToggling);
}

StateBase::Result BcbToggling::feed(EventType ev) {
    using EK = EventKind;
    using CpS = ::types::ev_board_support::EvCpState;
    switch (kind_of(ev)) {
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
    case EK::BspEvent:
        return handle_disconnect(ev);
    case EK::InjectFault: {
        auto p = std::get<api::InjectFaultParams>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::BcbToggling);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::ClearFault:
    case EK::BcbToggle:
    case EK::SetSoc:
    case EK::RunScenario:
        return reject(ev, "BCB toggling in progress");
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
