// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Unplugged.hpp"

#include "../FsmContext.hpp"
#include "../ScenarioDispatcher.hpp"
#include "Plugged.hpp"

#include <algorithm>

namespace module {

namespace api = API_types::ev_simulator;

void Unplugged::enter() {
    // The BSP peer's simulation loop is gated on its own enabled flag; arm
    // it on every Unplugged entry so the path from Disabled (where the BSP
    // is parked) back to Unplugged restarts the peer's CP/PWM propagation.
    // Idempotent on transitions between non-Disabled states.
    ctx.enable_bsp(true);
    ctx.set_cp(::types::ev_board_support::EvCpState::A);
    ctx.allow_power_on(false);
    ctx.iso_stop_charging();
    ctx.vars.session.reset();
    ctx.vars.last_fault.reset();
    ctx.vars.was_full = false;
    // Clear any pending resume PWM-wait so an aborted resume cannot leak into
    // the first V2GNegotiating of a fresh session (which must start immediately).
    ctx.vars.resume_awaiting_pwm = false;
    // Clear the deferred-resume edge count so an unplugged-mid-defer cannot carry
    // a stale BCB count into a fresh session's resume.
    ctx.vars.bcb_pending = 0;
    // Clear the SLAC torn-down latch so a teardown observed in a prior session
    // cannot force a redundant re-match on the first resume of a fresh one.
    ctx.vars.slac_unmatched = false;
    ctx.mark_plugged_in(false);
    ctx.scenario.reset();
    ctx.kvs_save();
    ctx.publish_e2m_state(api::FsmState::Unplugged);
}

StateBase::Result Unplugged::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::Plug:
        return {false, std::make_unique<Plugged>(ctx)};
    case EK::RunScenario: {
        auto p = std::get<api::RunScenarioParams>(ev.payload);
        ctx.scenario.start(p.name, p.timing, ctx);
        return {false, nullptr};
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::SetSoc: {
        auto p = std::get<api::SetSocParams>(ev.payload);
        // Clamp at the write site so a single out-of-range value (e.g. 1e30)
        // cannot publish on e2m/ev_info before the next-tick clamp catches it.
        const float clamped_soc = std::clamp(p.soc_pct, 0.0f, 100.0f);
        ctx.vars.soc_pct = clamped_soc;
        ctx.vars.battery_charge_wh = ctx.vars.battery_capacity_wh * (clamped_soc / 100.0f);
        return {false, nullptr};
    }
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Unplugged);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::InjectFault:
    case EK::ClearFault:
    case EK::BcbToggle:
        return reject(ev, "no session active");
    case EK::Unplug:
        return {true, nullptr};
    case EK::BspEvent:
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
    case EK::StateDeadline:
    // RaiseError / ClearError are intercepted on the loop thread before the
    // FSM feed; listed only to keep the switch exhaustive (-Werror=switch).
    // ConfigureSession is intercepted pre-FSM (loop thread); BeginSession is
    // an internal Plugged-only self-advance. Listed for switch exhaustiveness.
    case EK::ConfigureSession:
    case EK::BeginSession:
    case EK::RaiseError:
    case EK::ClearError:
    case EK::Shutdown:
    case EK::Enable:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
