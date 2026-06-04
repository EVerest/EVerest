// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "BcbToggling.hpp"

#include "../FsmContext.hpp"
#include "SlacMatching.hpp"
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
            // BcbToggling is only entered on an EV-initiated resume, so the
            // SECC has just been woken by the CP wake-up edges and has not yet
            // re-applied the charging PWM. Tell V2GNegotiating to wait for the
            // PWM-is-running BspMeasurement before re-issuing start_charging
            // (mirrors EvManager's iso_wait_pwm_is_running step). This flag is
            // set on both resume paths and carried through the SlacMatching
            // detour below, so the deferred-start contract holds whichever way
            // V2GNegotiating is finally entered.
            ctx.vars.resume_awaiting_pwm = true;
            // Re-establish SLAC only when the EV-side link was actually torn
            // down by the pause. A DC ISO pause is a clean D-LINK_PAUSE: the
            // data link survives and both sides stay SLAC MATCHED, so the EV
            // must NOT re-trigger matching -- the SECC SLAC is still MATCHED
            // (never MATCHING), so the SlacSimulator co-match, which fires only
            // with BOTH sides MATCHING, can never converge and the EV would
            // stall in MATCHING until SlacTimeout. The SECC resumes on the
            // surviving link (Car Paused -> PrepareCharging on the BCB wake-up),
            // so go straight to V2GNegotiating. An AC pause is a D-LINK_TERMINATE
            // that tears the EV link down and latches slac_unmatched; only then
            // is a fresh match needed. (AcIec has no SLAC and never latches
            // slac_unmatched, so it also takes the direct path.)
            if (ctx.vars.slac_unmatched) {
                return {false, std::make_unique<SlacMatching>(ctx)};
            }
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
    case EK::DcEvsePresentCurrent:
    case EK::DcEvsePresentVoltage:
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
