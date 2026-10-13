// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "V2GNegotiating.hpp"

#include "../FsmContext.hpp"
#include "Charging.hpp"
#include "Faulted.hpp"
#include "Stopping.hpp"

#include <everest/logging.hpp>

#include <chrono>

namespace module {

namespace api = API_types::ev_controller;

namespace {

// Issue the ISO start_charging request with the (resumed) session parameters.
// Returns false when there is no charge mode or the wired peer rejects the
// start (or the peer is absent / AcIec), in which case the caller routes into
// Faulted. No deadline follows: the ISO peer times each step (SETUP_TIMEOUT,
// the per-message and ongoing timeouts of ISO 15118-2 and -20) and always
// ends with v2g_session_finished.
bool start_iso_charging(FsmContext& ctx) {
    const auto cm = ctx.vars.charge_mode();
    return cm && ctx.iso_start_charging(*cm, ctx.vars.payment(), ctx.vars.departure_time_s, ctx.vars.e_amount_wh);
}

} // namespace

void V2GNegotiating::enter() {
    // On an EV-initiated resume BcbToggling sets resume_awaiting_pwm: the SECC
    // has just been woken by the CP wake-up edges but has not yet re-applied
    // the charging PWM. Defer start_charging until a PWM-running BspMeasurement
    // arrives, mirroring EvManager's iso_wait_pwm_is_running step -- issuing it
    // before the SECC is ready races its handshake and leaves the resume
    // hanging. No ISO peer times this wait, so a 60 s deadline does: a SECC
    // that never re-applies PWM faults via StateDeadline -> V2GTimeout instead
    // of hanging. Only start_charging is deferred. The first-session path
    // (SlacMatching -> V2GNegotiating) leaves the flag false and starts now.
    if (ctx.vars.resume_awaiting_pwm) {
        ctx.arm_timer(std::chrono::seconds(60));
        // The EV must hold CP=B for the whole pause window. The SECC switches
        // its CP PWM off across the pause and runs a 6 s C1 timer ("EV did not
        // go back to state B after PWM was switched off"); if it sees the EV
        // still at CP=C it force-powers-off under load and never re-applies PWM,
        // hanging the resume. BcbToggling already leaves CP=B and SlacMatching
        // does not touch it, so re-assert B explicitly here for clarity/safety.
        // The DC resume's contactor close (CP=C) is what the SECC's resume
        // CableCheck needs, but it must wait until the SECC re-applies the
        // charging PWM: a PWM-running BspMeasurement drives CP=C in feed()
        // below, mirroring EvManager's iso_wait_pwm_is_running -> CP=C ordering.
        ctx.set_cp(::types::ev_board_support::EvCpState::B);
        ctx.publish_e2m_state(api::FsmState::V2GNegotiating);
        return;
    }
    // start_iso_charging returns false when there is no mode or the ISO peer is
    // not wired (or for AcIec, which has no ISO session); each indicates a
    // misconfiguration when we reach this state, and no session would ever end
    // the wait. Route into Faulted on the next on_wake flush via a synthetic
    // InternalFault, carrying the descriptive message on the payload so it cannot
    // go stale if an intervening transition is flushed first.
    if (!start_iso_charging(ctx)) {
        ctx.enqueue(Event{InternalFault{api::FaultType::Internal, std::string{"iso_start_charging rejected"}}});
    }
    ctx.publish_e2m_state(api::FsmState::V2GNegotiating);
}

StateBase::Result V2GNegotiating::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::BspMeasurement: {
        const auto& m = std::get<BspMeasurementPayload>(ev.payload);
        ctx.vars.pwm_duty_cycle = m.cp_pwm_duty_cycle;
        // Resume gate: once the SECC re-applies the charging PWM, cancel the
        // wait and issue the deferred start_charging. Outside the resume gate
        // a BspMeasurement is informational (no transition).
        if (ctx.vars.resume_awaiting_pwm && pwm_running(m.cp_pwm_duty_cycle)) {
            ctx.vars.resume_awaiting_pwm = false;
            ctx.cancel_timer();
            // The SECC has re-applied the charging PWM and is ready for the
            // resume CableCheck. Re-issue the deferred start_charging now, but
            // keep CP at B: the EV holds CP=B until ev_power_ready (handled in
            // feed(IsoPowerReady)), mirroring EvManager which holds CP=B through
            // the pause until pwr_ready and only then closes the contactor for
            // the resume CableCheck. Asserting CP=C here, while the SECC's
            // pause-window PWM-off C1 timer may still be running, risks a
            // force-power-off under load.
            if (!start_iso_charging(ctx)) {
                ctx.vars.last_fault = api::FaultReport{api::FaultType::Internal,
                                                       std::string{"iso_start_charging rejected"}, std::nullopt};
                return {false, std::make_unique<Faulted>(ctx)};
            }
        }
        return {false, nullptr};
    }
    case EK::IsoPowerReady: {
        // Clear the resume gate so an unconsumed flag can never leak into a
        // later V2GNegotiating.
        ctx.vars.resume_awaiting_pwm = false;
        // DC ISO splits the charge-loop entry into two milestones, mirroring
        // EvManager. The ISO15118_ev module publishes ev_power_ready back in
        // ChargeParameterDiscovery (before CableCheck) and dc_power_on later in
        // PreCharge. EvManager gates on these separately: ev_power_ready ->
        // ISO_POWER_READY asserts CP=C and HOLDS (it does NOT start drawing
        // power yet), and only dc_power_on -> ISO_CHARGING_REGULATED enters the
        // charge loop. So for DC, present CP=C here (mirroring Charging::enter,
        // which closes the SECC contactor for CableCheck/PreCharge) and STAY in
        // V2GNegotiating. Transitioning to Charging now is premature: it signals
        // "Charging" while the SECC is still entering CableCheck, and a pause at
        // that moment drops CP to B mid-CableCheck and kills the PSU.
        const auto cm = ctx.vars.charge_mode();
        if (cm && (*cm == api::ChargeMode::DcIso2 || *cm == api::ChargeMode::DcIsoD20)) {
            ctx.set_cp(::types::ev_board_support::EvCpState::C);
            ctx.allow_power_on(true);
            return {false, nullptr};
        }
        // AC ISO has no dc_power_on milestone, so ev_power_ready is the
        // charge-loop entry gate.
        return {false, std::make_unique<Charging>(ctx)};
    }
    case EK::IsoDcPowerOn:
        // dc_power_on (PreCharge complete) is the DC charging milestone.
        // Published for both DcIso2 (iso15118_2_states.py PreCharge) and
        // DcIsoD20 (iso15118_20_states.py DCPreCharge). Clear the resume gate
        // defensively. CP=C was already asserted at IsoPowerReady; Charging::
        // enter re-asserts it (harmless) and closes the contactor.
        ctx.vars.resume_awaiting_pwm = false;
        return {false, std::make_unique<Charging>(ctx)};
    case EK::IsoStopFromCharger:
        ctx.vars.charger_stopped = true;
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::IsoV2GFinished:
        // Before the resumed start is sent, a finish is the paused session's.
        if (ctx.vars.resume_awaiting_pwm) {
            return {true, nullptr};
        }
        // Plugged opens the contactor and returns to B ([V2G2-913]).
        EVLOG_warning << "EvController: V2G session ended during negotiation";
        // The V2G session failed before charging, which the removed 60 s
        // deadline reported as V2GTimeout. It enters no Faulted.
        ctx.publish_e2m_fault(api::FaultReport{api::FaultType::V2GTimeout,
                                               std::string{"V2G session ended during negotiation"}, std::nullopt});
        return finish_stopping(ctx);
    case EK::StateDeadline:
        if (!ctx.vars.resume_awaiting_pwm) {
            return {true, nullptr};
        }
        ctx.vars.last_fault =
            api::FaultReport{api::FaultType::V2GTimeout, std::string{"no PWM to resume the session"}, std::nullopt};
        return {false, std::make_unique<Faulted>(ctx)};
    case EK::BspEvent:
        return handle_pilot_event(ev);
    case EK::InternalFault: {
        const auto& p = std::get<InternalFault>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::V2GNegotiating);
    case EK::StopSession:
        // No PowerDelivery(Stop) answers a stop before the charge loop, so
        // power and CP drop at once, as EvManager's iso_stop_charging
        // (car_simulation.cpp:610-616).
        return stop_session(ctx);
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::BcbToggle:
    case EK::SetSoc:
        return reject(ev, "negotiation in progress");
    case EK::Enable:
    case EK::IsoDerNegotiated:
    case EK::IsoDerControl:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::DcEvsePresentCurrent:
    case EK::DcEvsePresentVoltage:
    case EK::IsoPowerDeliveryStopped:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoPauseFromCharger:
    // RaiseError / ClearError are intercepted on the loop thread before the
    // FSM feed; listed only to keep the switch exhaustive (-Werror=switch).
    // ConfigureSession is intercepted pre-FSM (loop thread); BeginSession is
    // internal to Plugged and Unplugged. Listed for switch exhaustiveness.
    case EK::ConfigureSession:
    case EK::BeginSession:
    case EK::SetPresentValues:
    case EK::RaiseError:
    case EK::ClearError:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
