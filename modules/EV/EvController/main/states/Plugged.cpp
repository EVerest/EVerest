// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Plugged.hpp"

#include "../FsmContext.hpp"
#include "Charging.hpp"
#include "SlacMatching.hpp"

#include <everest/logging.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <variant>

namespace module {

namespace api = API_types::ev_controller;

void Plugged::enter() {
    ++ctx.vars.plug_generation;
    // Self-advance: a plug consumes the latched session config or default on the next on_wake flush. Synthetic-event
    // idiom mirrors SlacMatching::enter; keeps Plugged::enter void and the resolve logic in feed where transitions are
    // returned. Queued before the CP change so the session begins ahead of anything a peer sends in reply to it.
    if (!ctx.vars.hold_session) {
        ctx.enqueue(Event{BeginSessionEvt{ctx.vars.plug_generation}});
    }
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.allow_power_on(false);
    // A fresh plug has no prior V2G session. Reset the resume-gate bookkeeping
    // so a session that ended without an IsoV2GFinished (e.g. a hard
    // teardown) cannot leave iso_session_active stale-true and defer a future
    // resume forever.
    ctx.vars.iso_session_active = false;
    // finish_stopping runs once the session has ended, or Stopping gave up on it.
    if (m_restart == ChargerRestart::Settle) {
        ctx.arm_timer(kChargerWakeSettle);
    }
    ctx.kvs_save();
    ctx.publish_e2m_state(api::FsmState::Plugged);
}

// An AcIec session configured during the stopped one starts as on a plug.
bool Plugged::restarts_iso() const {
    return m_restart != ChargerRestart::None && ctx.vars.charge_mode() != api::ChargeMode::AcIec;
}

StateBase::Result Plugged::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::BeginSession: {
        // Internal, so nothing to reject: one queued by an earlier plug, or
        // reaching an EV held with no session, begins nothing.
        if (ctx.vars.hold_session ||
            std::get<BeginSessionEvt>(ev.payload).plug_generation != ctx.vars.plug_generation) {
            return {true, nullptr};
        }
        const api::SessionConfigParams sp = ctx.configured_session.value_or(
            ctx.default_session.value_or(api::SessionConfigParams{api::AcIecSessionParams{
                static_cast<float>(ctx.cfg.max_current_a), ctx.cfg.three_phases, std::nullopt}}));
        const api::ChargeMode mode = api::mode_of(sp);

        // Pull the fields each alternative carries into a flat local view.
        // Fields absent from an alternative stay nullopt; the variant makes
        // illegal combinations (mcs on AcIec, bpt on a non-D20 mode)
        // unrepresentable, so the old runtime rejections are gone.
        std::optional<api::PaymentOption> payment;
        std::optional<int32_t> departure_time_s;
        std::optional<int32_t> e_amount_wh;
        std::optional<float> charging_current_a;
        std::optional<bool> three_phases;
        std::optional<api::BptParams> bpt;
        bool mcs_enabled = false;
        std::optional<api::ChargingCurve> curve;
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, api::AcIecSessionParams>) {
                    charging_current_a = v.charging_current_a;
                    three_phases = v.three_phases;
                    curve = v.curve;
                } else if constexpr (std::is_same_v<T, api::AcIso2SessionParams> ||
                                     std::is_same_v<T, api::AcIsoD20DerIecSessionParams> ||
                                     std::is_same_v<T, api::AcIsoD20DerSaeSessionParams>) {
                    payment = v.payment;
                    departure_time_s = v.departure_time_s;
                    e_amount_wh = v.e_amount_wh;
                    charging_current_a = v.charging_current_a;
                    three_phases = v.three_phases;
                    curve = v.curve;
                } else if constexpr (std::is_same_v<T, api::AcIsoD20SessionParams>) {
                    payment = v.payment;
                    departure_time_s = v.departure_time_s;
                    e_amount_wh = v.e_amount_wh;
                    charging_current_a = v.charging_current_a;
                    three_phases = v.three_phases;
                    bpt = v.bpt;
                    curve = v.curve;
                } else if constexpr (std::is_same_v<T, api::DcIso2SessionParams>) {
                    payment = v.payment;
                    departure_time_s = v.departure_time_s;
                    e_amount_wh = v.e_amount_wh;
                    curve = v.curve;
                } else if constexpr (std::is_same_v<T, api::DcIsoD20SessionParams>) {
                    payment = v.payment;
                    departure_time_s = v.departure_time_s;
                    e_amount_wh = v.e_amount_wh;
                    bpt = v.bpt;
                    mcs_enabled = v.mcs_enabled;
                    curve = v.curve;
                }
            },
            sp);

        // Falling back on cfg defaults when the user omits a field is
        // intentional UX for AC scenarios; but log the substitution so the
        // omission is visible rather than silent.
        float current_a;
        if (charging_current_a) {
            current_a = *charging_current_a;
        } else {
            current_a = static_cast<float>(ctx.cfg.max_current_a);
            EVLOG_info << "EvController: session config omitted charging_current_a; using cfg.max_current_a="
                       << current_a;
        }
        bool three_ph;
        if (three_phases) {
            three_ph = *three_phases;
        } else {
            three_ph = ctx.cfg.three_phases;
            EVLOG_info << "EvController: session config omitted three_phases; using cfg.three_phases=" << three_ph;
        }
        if (is_ac_mode(mode)) {
            // Establish the EV's desired current from the session config and
            // apply it clamped against any EVSE AC limit already received, so a
            // ceiling communicated before Charging is entered constrains the
            // params applied here without discarding the desired.
            ctx.set_desired_ac_params(current_a, three_ph);
        }
        if (departure_time_s) {
            ctx.vars.departure_time_s = *departure_time_s;
        }
        if (e_amount_wh) {
            ctx.vars.e_amount_wh = *e_amount_wh;
        }
        // Commit the whole session in one assignment. The pending_curve is
        // carried so Charging::enter can append it to the curve
        // player: appending here would let an offset-0 step land in the
        // runtime queue while the FSM is still in SlacMatching /
        // V2GNegotiating / BcbToggling, all of which reject
        // SetChargingCurrent. Curve well-formedness is already enforced at
        // session validation time.
        Session sess;
        sess.mode = mode;
        sess.payment = payment;
        sess.bpt = bpt;
        sess.mcs_enabled = mcs_enabled;
        if (curve.has_value()) {
            sess.pending_curve = std::move(curve);
        }
        ctx.vars.session = std::move(sess);
        return {false, nullptr};
    }
    case EK::StateDeadline:
        if (m_restart == ChargerRestart::Settle) {
            m_restart = ChargerRestart::AwaitPwm;
            if (restarts_iso() && pwm_running(ctx.vars.pwm_duty_cycle)) {
                return {false, std::make_unique<SlacMatching>(ctx)};
            }
            return {false, nullptr};
        }
        if (!m_iso_pwm_waiting) {
            return {true, nullptr};
        }
        m_iso_pwm_waiting = false;
        return transition_to_fault(ctx, {api::FaultType::Internal, "no 5 % PWM"});
    case EK::BspMeasurement: {
        const auto& m = std::get<BspMeasurementPayload>(ev.payload);
        ctx.vars.pwm_duty_cycle = m.cp_pwm_duty_cycle;
        const bool running = pwm_running(m.cp_pwm_duty_cycle);
        const bool five_percent = running && m.cp_pwm_duty_cycle < 7;
        const bool nominal = pwm_nominal(m.cp_pwm_duty_cycle);
        // After a charger stop, any running PWM past the settle restarts, in
        // every ISO mode and with no gap rule.
        if (restarts_iso()) {
            if (m_restart == ChargerRestart::AwaitPwm && running) {
                return {false, std::make_unique<SlacMatching>(ctx)};
            }
            return {false, nullptr};
        }
        const auto mode = ctx.vars.charge_mode();
        // AC ISO matches on pilot B with or without PWM, as EvManager's iso_wait_slac_matched
        // (car_simulation.cpp:116-120, 444-462), so an X1 start reaches SLAC. DC needs 5 %.
        const bool iso_start = mode && is_iso_mode(*mode) && (is_ac_mode(*mode) || five_percent);
        if (iso_start) {
            return {false, std::make_unique<SlacMatching>(ctx)};
        }
        if (mode && is_iso_mode(*mode) && nominal && !m_iso_pwm_waiting) {
            m_iso_pwm_waiting = true;
            ctx.arm_timer(ctx.cfg.iso_pwm_wait_s > 0 ? std::chrono::seconds(ctx.cfg.iso_pwm_wait_s)
                                                     : std::chrono::milliseconds(1));
        }
        if (mode == api::ChargeMode::AcIec && nominal) {
            return {false, std::make_unique<Charging>(ctx)};
        }
        return {false, nullptr};
    }
    case EK::BspEvent:
        return handle_pilot_event(ev);
    case EK::InternalFault: {
        const auto& p = std::get<InternalFault>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Plugged);
    case EK::StopSession:
        // A restart waiting for the charger's PWM has a session to stop.
        if (m_restart != ChargerRestart::None) {
            return stop_session(ctx);
        }
        return reject(ev, "no session active");
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::BcbToggle:
    case EK::SetSoc:
        return reject(ev, "no session active");
    case EK::Enable:
    case EK::IsoDerNegotiated:
    case EK::IsoDerControl:
    case EK::IsoWeldingDetectionFailed:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::DcEvsePresentCurrent:
    case EK::DcEvsePresentVoltage:
    case EK::IsoPowerDeliveryStopped:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoStopFromCharger:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
    // ConfigureSession is intercepted pre-FSM (loop thread); RaiseError /
    // ClearError likewise. Listed only to keep the switch exhaustive
    // (-Werror=switch).
    case EK::ConfigureSession:
    case EK::SetPresentValues:
    case EK::RaiseError:
    case EK::ClearError:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
