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

namespace {

// CP PWM duty-cycle window the SECC drives once it is ready for HLC. Matches
// EvManager's iso_wait_pwm_is_running gate (> 4% .. < 97%); the lower bound is
// generous so the "PWM running" edge is observed as soon as the SECC re-applies
// any non-nominal duty cycle on resume.
constexpr float kPwmRunningLowPct = 4.0f;
constexpr float kPwmRunningHighPct = 97.0f;

// Issue the ISO start_charging request with the (resumed) session parameters
// and, on success, arm the 60 s negotiation deadline. Returns false when the
// wired peer rejects the start (or the peer is absent / AcIec), in which case
// the deadline is NOT armed and the caller routes into Faulted. The deadline is
// armed only after the request is sent.
bool start_iso_charging(FsmContext& ctx) {
    const auto cm = ctx.vars.charge_mode();
    if (cm && !ctx.iso_start_charging(*cm, ctx.vars.payment(), ctx.vars.departure_time_s, ctx.vars.e_amount_wh)) {
        return false;
    }
    ctx.arm_timer(std::chrono::seconds(60));
    return true;
}

} // namespace

void V2GNegotiating::enter() {
    // On an EV-initiated resume BcbToggling sets resume_awaiting_pwm: the SECC
    // has just been woken by the CP wake-up edges but has not yet re-applied
    // the charging PWM. Defer start_charging until a PWM-running BspMeasurement
    // arrives, mirroring EvManager's iso_wait_pwm_is_running step -- issuing it
    // before the SECC is ready races its handshake and leaves the resume
    // hanging. The deadline IS armed here, though: a SECC that never re-applies
    // PWM then faults via StateDeadline -> V2GTimeout instead of hanging
    // forever. Only start_charging is deferred. The first-session path
    // (SlacMatching -> V2GNegotiating) leaves the flag false and starts now.
    if (ctx.vars.resume_awaiting_pwm) {
        ctx.arm_timer(std::chrono::seconds(60));
        ctx.publish_e2m_state(api::FsmState::V2GNegotiating);
        return;
    }
    // start_iso_charging returns false when the ISO peer is not wired (or for
    // AcIec, which has no ISO session); both indicate a misconfiguration when
    // we reach this state. Without an early fault transition the state would
    // arm the 60 s deadline timer and only fault later with a misleading
    // V2GTimeout. Route into Faulted on the next on_wake flush via a synthetic
    // InjectFault, carrying the descriptive message on the payload so it cannot
    // go stale if an intervening transition is flushed first.
    if (!start_iso_charging(ctx)) {
        ctx.enqueue(Event{api::InjectFaultParams{api::FaultType::Internal, std::nullopt,
                                                 std::string{"iso_start_charging rejected"}}});
    }
    ctx.publish_e2m_state(api::FsmState::V2GNegotiating);
}

StateBase::Result V2GNegotiating::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::BspMeasurement: {
        const auto& m = std::get<BspMeasurementPayload>(ev.payload);
        ctx.vars.pwm_duty_cycle = m.cp_pwm_duty_cycle;
        // Resume gate: once the SECC re-applies the charging PWM, issue the
        // deferred start_charging and arm the deadline. Outside the resume gate
        // a BspMeasurement is informational (no transition).
        if (ctx.vars.resume_awaiting_pwm && m.cp_pwm_duty_cycle > kPwmRunningLowPct &&
            m.cp_pwm_duty_cycle < kPwmRunningHighPct) {
            ctx.vars.resume_awaiting_pwm = false;
            if (!start_iso_charging(ctx)) {
                ctx.vars.last_fault = api::FaultReport{api::FaultType::Internal,
                                                       std::string{"iso_start_charging rejected"}, std::nullopt};
                return {false, std::make_unique<Faulted>(ctx)};
            }
        }
        return {false, nullptr};
    }
    case EK::IsoPowerReady:
        // Clear the resume gate on the way into the charge loop so an
        // unconsumed flag can never leak into a later V2GNegotiating.
        ctx.vars.resume_awaiting_pwm = false;
        return {false, std::make_unique<Charging>(ctx)};
    case EK::IsoDcPowerOn:
        ctx.allow_power_on(true);
        return {false, nullptr};
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
    case EK::EvInfo:
    case EK::SlacState:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoV2GFinished:
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
