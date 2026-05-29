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
}

void Charging::leave() {
    ctx.disarm_tick();
    StateBase::leave();
}

StateBase::Result Charging::feed(EventType ev) {
    using EK = EventKind;
    switch (ev.kind) {
    case EK::StopSession:
        if (ctx.vars.charge_mode == api::ChargeMode::AcIec) {
            return {false, std::make_unique<Unplugged>(ctx)};
        }
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::PauseSession:
        return {false, std::make_unique<Paused>(ctx)};
    case EK::SetChargingCurrent: {
        auto p = std::get<api::SetChargingCurrentParams>(ev.payload);
        if (ctx.vars.charge_mode == api::ChargeMode::AcIec || ctx.vars.charge_mode == api::ChargeMode::AcIso2) {
            ctx.bsp_apply_ac_params(p.current_a, p.three_phases);
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
        if (ctx.vars.charge_mode == api::ChargeMode::AcIec && (m.cp_pwm_duty_cycle <= 7 || m.cp_pwm_duty_cycle >= 97)) {
            return {false, std::make_unique<ChargingPwmPaused>(ctx)};
        }
        return {false, nullptr};
    }
    case EK::Unplug:
        if (ctx.vars.charge_mode == api::ChargeMode::AcIec) {
            return {false, std::make_unique<Unplugged>(ctx)};
        }
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::BspEvent: {
        const auto& p = std::get<BspEventPayload>(ev.payload);
        if (::types::board_support_common::event_to_string(p.bsp_event.event) == "Disconnected") {
            return {false, std::make_unique<Unplugged>(ctx)};
        }
        return {true, nullptr};
    }
    case EK::InjectFault: {
        auto p = std::get<api::InjectFaultParams>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Charging);
    case EK::StartSession:
    case EK::ResumeSession:
    case EK::ClearFault:
    case EK::BcbToggle:
    case EK::SetSoc:
    case EK::RunScenario:
    case EK::Plug:
    case EK::Enable:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::StateDeadline:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
