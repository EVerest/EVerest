// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Charging.hpp"

#include "../FsmContext.hpp"
#include "ChargingPwmPaused.hpp"
#include "Paused.hpp"
#include "Stopping.hpp"
#include "Unplugged.hpp"

namespace module {

namespace api = API_types::ev_simulator;

void Charging::enter() {
    ctx.set_cp(::types::ev_board_support::EvCpState::C);
    ctx.allow_power_on(true);
    ctx.arm_tick(ctx.cfg.tick_interval_ms);
    ctx.publish_e2m_state(api::FsmState::Charging);

    // Re-apply the AC current clamped against any EVSE limit received during
    // Plugged so the first applied current already respects that ceiling.
    const auto mode = ctx.vars.charge_mode();
    if (mode == api::ChargeMode::AcIec || mode == api::ChargeMode::AcIso2 || mode == api::ChargeMode::AcIsoD20) {
        ctx.bsp_apply_ac_params_clamped(ctx.vars.charging_current_a, ctx.vars.three_phases);
    }

    if (ctx.vars.session && ctx.vars.session->pending_curve.has_value()) {
        const auto& curve = *ctx.vars.session->pending_curve;
        std::vector<ScenarioStep> curve_steps;
        curve_steps.reserve(curve.points.size());
        for (const auto& cp : curve.points) {
            api::SetChargingCurrentParams sc;
            sc.current_a = cp.current_a;
            sc.three_phases = cp.three_phases;
            sc.ramp_ms = cp.ramp_ms;
            curve_steps.push_back({std::chrono::milliseconds(cp.t_offset_ms), Event{std::move(sc)}});
        }
        const std::size_t loop_begin = ctx.scenario.step_count();
        const std::size_t loop_end = loop_begin + curve_steps.size();
        const bool should_loop = curve.loop;
        ctx.scenario.append_steps(std::move(curve_steps), ctx);
        if (should_loop) {
            ctx.scenario.mark_loop(loop_begin, loop_end);
        }
        ctx.vars.session->pending_curve.reset();
    }
}

void Charging::on_leave() {
    ctx.disarm_tick();
    ctx.vars.active_ramp.reset();
}

StateBase::Result Charging::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::StopSession:
        if (ctx.vars.charge_mode() == api::ChargeMode::AcIec) {
            return {false, std::make_unique<Unplugged>(ctx)};
        }
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::PauseSession:
        return {false, std::make_unique<Paused>(ctx)};
    case EK::SetChargingCurrent: {
        auto p = std::get<api::SetChargingCurrentParams>(ev.payload);
        if (ctx.vars.charge_mode() == api::ChargeMode::AcIec || ctx.vars.charge_mode() == api::ChargeMode::AcIso2) {
            // Store the RAW request as the EV's desired current. Clamping
            // against the most recent EVSE limit happens at apply time
            // (set_desired_ac_params / the ramp step), so a transient ceiling
            // never overwrites the desired and the applied current can recover
            // toward it once the ceiling is lifted.
            if (p.ramp_ms.has_value() && *p.ramp_ms > 0) {
                const auto now = std::chrono::steady_clock::now();
                ActiveRamp ramp;
                ramp.start_a = ctx.vars.charging_current_a;
                ramp.target_a = p.current_a;
                ramp.three_phases = p.three_phases;
                ramp.start_at = now;
                ramp.end_at = now + std::chrono::milliseconds{*p.ramp_ms};
                ctx.vars.active_ramp = ramp;
            } else {
                ctx.vars.active_ramp.reset();
                ctx.set_desired_ac_params(p.current_a, p.three_phases);
            }
        } else {
            ctx.publish_e2m_command_ack("set_charging_current", "set_charging_current not supported in DC/ISO-20 mode");
        }
        return {false, nullptr};
    }
    case EK::IsoStopFromCharger:
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::IsoPauseFromCharger:
        return {false, std::make_unique<Paused>(ctx)};
    case EK::BspMeasurement: {
        const auto& m = std::get<BspMeasurementPayload>(ev.payload);
        ctx.vars.pwm_duty_cycle = m.cp_pwm_duty_cycle;
        if (ctx.vars.charge_mode() == api::ChargeMode::AcIec &&
            (m.cp_pwm_duty_cycle <= 7 || m.cp_pwm_duty_cycle >= 97)) {
            return {false, std::make_unique<ChargingPwmPaused>(ctx)};
        }
        return {false, nullptr};
    }
    case EK::Unplug:
        if (ctx.vars.charge_mode() == api::ChargeMode::AcIec) {
            return {false, std::make_unique<Unplugged>(ctx)};
        }
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::BspEvent:
        return handle_disconnect(ev);
    case EK::InjectFault: {
        auto p = std::get<api::InjectFaultParams>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Charging);
    case EK::ResumeSession:
    case EK::ClearFault:
    case EK::BcbToggle:
    case EK::SetSoc:
    case EK::RunScenario:
    case EK::Plug:
    case EK::Enable:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::IsoAcMaxCurrent: {
        const auto& p = std::get<IsoAcMaxCurrentEvt>(ev.payload);
        ctx.note_evse_ac_max_current(p.max_current_a);
        ctx.vars.active_ramp.reset();
        ctx.bsp_apply_ac_params_clamped(ctx.vars.charging_current_a, ctx.vars.three_phases);
        return {false, nullptr};
    }
    case EK::IsoAcTargetPower: {
        const auto& p = std::get<IsoAcTargetPowerEvt>(ev.payload);
        ctx.note_evse_ac_target_power(p.target_power);
        ctx.vars.active_ramp.reset();
        ctx.bsp_apply_ac_params_clamped(ctx.vars.charging_current_a, ctx.vars.three_phases);
        return {false, nullptr};
    }
    case EK::IsoPowerReady:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::StateDeadline:
    // DC live present-current / present-voltage are var-only passthrough:
    // EvSimRuntime::apply_passthrough_vars latches them into vars before the
    // FSM feed, so the state itself ignores them (no ev.payload access).
    case EK::DcEvsePresentCurrent:
    case EK::DcEvsePresentVoltage:
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
