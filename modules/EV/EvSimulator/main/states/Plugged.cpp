// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Plugged.hpp"

#include "../FsmContext.hpp"
#include "Charging.hpp"
#include "SlacMatching.hpp"
#include "Unplugged.hpp"

#include <everest/logging.hpp>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <variant>

namespace module {

namespace api = API_types::ev_simulator;

void Plugged::enter() {
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.allow_power_on(false);
    ctx.mark_plugged_in(true);
    // A fresh plug has no prior V2G session. Reset the resume-gate bookkeeping
    // so a session that ended without an IsoV2GFinished (e.g. a hard
    // teardown) cannot leave iso_session_active stale-true and defer a future
    // resume forever.
    ctx.vars.iso_session_active = false;
    ctx.vars.resume_pending = false;
    ctx.vars.bcb_pending = 0;
    ctx.kvs_save();
    ctx.publish_e2m_state(api::FsmState::Plugged);
    // Self-advance: a plug consumes the latched session config (or an AcIec
    // default) on the next on_wake flush. Synthetic-event idiom mirrors
    // SlacMatching::enter — keeps Plugged::enter void and the resolve logic
    // in feed where transitions are returned.
    ctx.enqueue(Event{BeginSessionEvt{}});
}

StateBase::Result Plugged::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::BeginSession: {
        // Source the spec from the latched configure_session, or synthesize a
        // bare AcIec session from cfg defaults when nothing was configured.
        // The latched spec was already validated (curve + ISO/SLAC peer) at
        // configure time, and a synthesized default has no curve, so no
        // validation is repeated here.
        const api::SessionConfigParams sp =
            ctx.configured_session.has_value()
                ? *ctx.configured_session
                : api::SessionConfigParams{api::AcIecSessionParams{static_cast<float>(ctx.cfg.max_current_a),
                                                                   ctx.cfg.three_phases, std::nullopt}};
        const api::ChargeMode mode = api::mode_of(sp);

        // Pull the fields each alternative carries into a flat local view.
        // Fields absent from an alternative stay nullopt — the variant makes
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
                } else if constexpr (std::is_same_v<T, api::AcIso2SessionParams>) {
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
        // intentional UX for AC scenarios — but log the substitution so the
        // omission is visible rather than silent.
        float current_a;
        if (charging_current_a) {
            current_a = *charging_current_a;
        } else {
            current_a = static_cast<float>(ctx.cfg.max_current_a);
            EVLOG_info << "EvSimulator: session config omitted charging_current_a; using cfg.max_current_a="
                       << current_a;
        }
        bool three_ph;
        if (three_phases) {
            three_ph = *three_phases;
        } else {
            three_ph = ctx.cfg.three_phases;
            EVLOG_info << "EvSimulator: session config omitted three_phases; using cfg.three_phases=" << three_ph;
        }
        if (mode == api::ChargeMode::AcIec || mode == api::ChargeMode::AcIso2 || mode == api::ChargeMode::AcIsoD20) {
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
        // carried so Charging::enter can splice it into the scenario
        // dispatcher: splicing here would let an offset-0 step land in the
        // runtime queue while the FSM is still in SlacMatching /
        // V2GNegotiating / BcbToggling, all of which reject
        // SetChargingCurrent. Curve well-formedness is already enforced at
        // configure_session time.
        Session sess;
        sess.mode = mode;
        sess.payment = payment;
        sess.bpt = bpt;
        sess.mcs_enabled = mcs_enabled;
        if (curve.has_value()) {
            sess.pending_curve = std::move(curve);
        }
        ctx.vars.session = std::move(sess);
        switch (mode) {
        case api::ChargeMode::AcIec:
            return {false, nullptr};
        case api::ChargeMode::AcIso2:
        case api::ChargeMode::AcIsoD20:
        case api::ChargeMode::DcIso2:
        case api::ChargeMode::DcIsoD20:
            return {false, std::make_unique<SlacMatching>(ctx)};
        }
        return {false, nullptr};
    }
    case EK::BspMeasurement: {
        const auto& m = std::get<BspMeasurementPayload>(ev.payload);
        ctx.vars.pwm_duty_cycle = m.cp_pwm_duty_cycle;
        if (ctx.vars.charge_mode() == api::ChargeMode::AcIec && m.cp_pwm_duty_cycle > 7 && m.cp_pwm_duty_cycle < 97) {
            return {false, std::make_unique<Charging>(ctx)};
        }
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
        return handle_query_state(ctx, api::FsmState::Plugged);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::ClearFault:
    case EK::BcbToggle:
    case EK::SetSoc:
    case EK::RunScenario:
        return reject(ev, "no session active");
    case EK::Plug:
    case EK::Enable:
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
    // ConfigureSession is intercepted pre-FSM (loop thread); RaiseError /
    // ClearError likewise. Listed only to keep the switch exhaustive
    // (-Werror=switch).
    case EK::ConfigureSession:
    case EK::RaiseError:
    case EK::ClearError:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
