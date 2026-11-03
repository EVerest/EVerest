// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Paused.hpp"

#include "../FsmContext.hpp"
#include "BcbToggling.hpp"
#include "Charging.hpp"
#include "Stopping.hpp"
#include "V2GNegotiating.hpp"

#include <everest/logging.hpp>

#include <chrono>
#include <string_view>

namespace module {

namespace api = API_types::ev_controller;

namespace {
constexpr auto kPauseWatchdog = std::chrono::hours{1};
constexpr std::string_view kResumePending{"resume already pending"};
} // namespace

void Paused::enter() {
    ctx.allow_power_on(false);
    // A charger pause is already under way in the peer; EvManager only powers
    // off (car_simulation.cpp:694-696).
    if (not m_charger_pause) {
        ctx.iso_pause_charging();
    }
    if (not m_charger_pause and ctx.vars.iso_session_active) {
        m_retain = RetainPwm::SessionLive;
    }
    // An accepted PowerDelivery(Stop) precedes WeldingDetectionReq, for which
    // ISO 15118-2 [V2G2-920]..[V2G2-922] require CP B. Transport teardown is too late.
    // With no live session there is no response to wait for, and ISO 15118-20 AC reports
    // no accepted PowerDelivery(Stop), so B goes at once as EvManager's pause does.
    const auto mode = ctx.vars.charge_mode();
    const bool d20_ac = mode == api::ChargeMode::AcIsoD20 or mode == api::ChargeMode::AcIsoD20DerIec or
                        mode == api::ChargeMode::AcIsoD20DerSae;
    if (not ctx.vars.iso_session_active or d20_ac) {
        ctx.set_cp(::types::ev_board_support::EvCpState::B);
    }
    // EvManager's iso_wait_v2g_session_stopped returns at once with no session.
    if (m_charger_pause && not ctx.vars.iso_session_active) {
        m_charger_resume = ChargerResume::Settle;
        ctx.arm_timer(kChargerWakeSettle);
    } else {
        ctx.arm_timer(kPauseWatchdog);
    }
    ctx.publish_e2m_state(api::FsmState::Paused);
}

StateBase::Result Paused::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::ResumeSession:
        // AcIec resume is a direct CP B->C transition: there is no HLC to
        // re-establish, so BCB toggling does not apply. ISO modes (and the
        // no-session test path) keep the BcbToggling round-trip that
        // signals EVCC's intent to resume HLC to the SECC.
        if (ctx.vars.charge_mode() == api::ChargeMode::AcIec) {
            return {false, std::make_unique<Charging>(ctx)};
        }
        if (m_ev_resume != EvResume::None) {
            return reject(ev, kResumePending);
        }
        // The edge count is consumed by BcbToggling; see BcbToggling::enter
        // for how the default 6 maps to round-trips.
        return request_resume(6);
    case EK::BcbToggle: {
        if (m_ev_resume != EvResume::None) {
            return reject(ev, kResumePending);
        }
        // Explicit BcbToggle accepts the user-provided round-trip count via
        // BcbToggleParams::count. count <= 0 is rejected; absent count falls
        // back to the default of 3 round-trips (= 6 edges) so the behavior
        // matches the implicit ResumeSession path. An upper bound guards
        // against signed-overflow UB on `round_trips * 2` and against a
        // pathologically long toggle loop (cap is paranoia-only; well
        // beyond any realistic test usage at the 250 ms-per-edge timer).
        constexpr int32_t kMaxBcbRoundTrips = 1000;
        auto p = std::get<api::BcbToggleParams>(ev.payload);
        int32_t round_trips = p.count.value_or(3);
        if (round_trips <= 0) {
            ctx.publish_e2m_command_ack("bcb_toggle", "count must be > 0");
            return {false, nullptr};
        }
        if (round_trips > kMaxBcbRoundTrips) {
            ctx.publish_e2m_command_ack("bcb_toggle", "count exceeds 1000");
            return {false, nullptr};
        }
        // An explicit BcbToggle wakes the SECC the same way an implicit resume
        // does, so it must honor the same gate: starting the BCB edges while the
        // paused V2G session is still tearing down lets the previous session's
        // lagging SessionStop clobber the freshly re-established link. Defer
        // until IsoV2GFinished (or the bounded fallback), carrying the requested
        // edge count.
        return request_resume(round_trips * 2);
    }
    case EK::IsoPowerDeliveryStopped:
        ctx.set_cp(::types::ev_board_support::EvCpState::B);
        return {false, nullptr};
    case EK::IsoV2GFinished:
        // The paused V2G session has finished tearing down (iso_session_active
        // was cleared pre-feed). The bound on the wait for X1 runs from here.
        if (m_retain == RetainPwm::SessionLive) {
            start_x1_bound();
        }
        // A resume requested while the session was live is released now.
        if (m_ev_resume == EvResume::AwaitSessionEnd) {
            return release_resume();
        }
        // No resume pending. CP is already B if PowerDelivery(Stop) was accepted;
        // re-assert it for a session that ended without one, so CP is never left at C.
        ctx.set_cp(::types::ev_board_support::EvCpState::B);
        if (m_charger_resume == ChargerResume::AwaitSessionEnd && m_charger_pause) {
            m_charger_resume = ChargerResume::Settle;
            ctx.arm_timer(kChargerWakeSettle);
        }
        return {false, nullptr};
    case EK::StateDeadline:
        // A deferred resume armed a short fallback timer. If it fires while the
        // resume is still pending, IsoV2GFinished was never observed (abnormal
        // V2G teardown): wake the charger best-effort rather than hanging to
        // the test timeout. The fallback already outlasts the retain time
        // tenfold, so no wait for X1 follows it.
        if (m_ev_resume == EvResume::AwaitSessionEnd) {
            EVLOG_warning << "EvController: resume fallback fired (IsoV2GFinished "
                             "not observed); resuming best-effort";
            return wake_charger();
        }
        if (m_retain == RetainPwm::Bound) {
            m_retain = RetainPwm::None;
            if (m_ev_resume == EvResume::AwaitX1) {
                // Past its retain time the charger is outside [V2G-DC-968], but its
                // oscillator is live and EvseManager takes a restart during it.
                EVLOG_warning << "EvController: charger kept its PWM after the pause; waking it best-effort";
                return wake_charger();
            }
            ctx.arm_timer(kPauseWatchdog);
            return {false, nullptr};
        }
        if (m_charger_pause && m_charger_resume == ChargerResume::Settle) {
            m_charger_resume = ChargerResume::AwaitPwm;
            if (pwm_running(ctx.vars.pwm_duty_cycle)) {
                return {false, std::make_unique<V2GNegotiating>(ctx)};
            }
            ctx.arm_timer(kPauseWatchdog);
            return {false, nullptr};
        }
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::BspEvent:
        return handle_pilot_event(ev);
    case EK::InternalFault: {
        const auto& p = std::get<InternalFault>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Paused);
    case EK::StopSession:
        return stop_session(ctx);
    case EK::PauseSession:
    case EK::SetChargingCurrent:
    case EK::SetSoc:
        return reject(ev, "session paused");
    case EK::BspMeasurement:
        if (m_retain != RetainPwm::None &&
            not pwm_running(std::get<BspMeasurementPayload>(ev.payload).cp_pwm_duty_cycle)) {
            // The charger is in X1: the retain PWM is over.
            const bool bound_armed = m_retain == RetainPwm::Bound;
            m_retain = RetainPwm::None;
            if (m_ev_resume == EvResume::AwaitX1) {
                return wake_charger();
            }
            if (bound_armed) {
                ctx.arm_timer(kPauseWatchdog);
            }
        }
        // After a charger pause, running PWM starts a new session on the matched
        // link, with no BCB toggle and no SLAC, as EvManager's
        // arm_resume_after_charger_pause (car_simulation.cpp:694-708). There is
        // no gap rule: the charger may resume before its PWM ever drops.
        if (m_charger_resume == ChargerResume::AwaitPwm &&
            pwm_running(std::get<BspMeasurementPayload>(ev.payload).cp_pwm_duty_cycle)) {
            return {false, std::make_unique<V2GNegotiating>(ctx)};
        }
        return {true, nullptr};
    case EK::Enable:
    case EK::IsoDerNegotiated:
    case EK::IsoDerControl:
    case EK::IsoWeldingDetectionFailed:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::DcEvsePresentCurrent:
    case EK::DcEvsePresentVoltage:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoStopFromCharger:
    case EK::IsoDcPowerOn:
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

StateBase::Result Paused::request_resume(int32_t edges) {
    m_edges = edges;
    // The ISO 15118 session runs once per start_charging. If the
    // paused session has not finished tearing down yet, defer the resume: starting
    // the BCB wake-up + re-SLAC now would let the previous session's lagging
    // SessionStop clobber the freshly re-established link (the SECC then drops to
    // D-LINK_PAUSE + PWM off and never re-applies the charging PWM, so the resume
    // hangs). IsoV2GFinished releases it once the session is gone.
    if (ctx.vars.iso_session_active) {
        m_ev_resume = EvResume::AwaitSessionEnd;
        // If the ISO 15118 session never ends (abnormal teardown),
        // IsoV2GFinished never arrives and the deferred
        // resume would otherwise hang on the 1 h pause timer.
        ctx.arm_timer(kDeferredResumeFallback);
        return {false, nullptr};
    }
    return release_resume();
}

StateBase::Result Paused::release_resume() {
    // After SessionStop(Pause) the charger holds its 5 % oscillator for the
    // retain time ([V2G-DC-968]), then goes to X1. A wake-up inside that window
    // starts the new session on the retain PWM just as the charger drops it, so
    // wait for X1: the PWM V2GNegotiating then sees is the charger's re-apply.
    // The bound armed at the session end keeps running; nothing re-arms it.
    if (m_retain == RetainPwm::Bound && pwm_running(ctx.vars.pwm_duty_cycle)) {
        m_ev_resume = EvResume::AwaitX1;
        return {false, nullptr};
    }
    return wake_charger();
}

void Paused::start_x1_bound() {
    m_retain = RetainPwm::Bound;
    ctx.arm_timer(kChargerX1Wait);
}

StateBase::Result Paused::wake_charger() {
    m_ev_resume = EvResume::None;
    ctx.vars.bcb_remaining = m_edges;
    return {false, std::make_unique<BcbToggling>(ctx)};
}

} // namespace module
