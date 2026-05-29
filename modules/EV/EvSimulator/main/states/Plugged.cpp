// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Plugged.hpp"

#include "../FsmContext.hpp"
#include "Charging.hpp"
#include "SlacMatching.hpp"
#include "Unplugged.hpp"

namespace module {

namespace api = API_types::ev_simulator;

void Plugged::enter() {
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.allow_power_on(false);
    ctx.persisted.plugged_in = true;
    ctx.kvs_save();
    ctx.publish_e2m_state(api::FsmState::Plugged);
}

StateBase::Result Plugged::feed(EventType ev) {
    using EK = EventKind;
    switch (ev.kind) {
    case EK::StartSession: {
        auto p = std::get<api::StartSessionParams>(ev.payload);
        ctx.vars.charge_mode = p.mode;
        auto current_a = p.charging_current_a.value_or(static_cast<float>(ctx.cfg.max_current_a));
        auto three_ph = p.three_phases.value_or(ctx.cfg.three_phases);
        if (p.mode == api::ChargeMode::AcIsoD20 || p.mode == api::ChargeMode::DcIsoD20) {
            ctx.publish_e2m_command_ack("start_session", "iso15118-20 not supported in v1");
            ctx.vars.charge_mode.reset();
            return {false, nullptr};
        }
        if (p.mode == api::ChargeMode::AcIso2 || p.mode == api::ChargeMode::DcIso2) {
            if (!ctx.peer_actions.iso_start_charging || !ctx.peer_actions.slac_trigger_matching) {
                ctx.publish_e2m_command_ack("start_session", "no ev_slac peer");
                ctx.vars.charge_mode.reset();
                return {false, nullptr};
            }
        }
        if (p.mode == api::ChargeMode::AcIec || p.mode == api::ChargeMode::AcIso2) {
            ctx.bsp_apply_ac_params(current_a, three_ph);
        }
        if (p.departure_time_s) {
            ctx.vars.departure_time_s = *p.departure_time_s;
        }
        if (p.e_amount_wh) {
            ctx.vars.e_amount_wh = *p.e_amount_wh;
        }
        if (p.payment) {
            ctx.persisted.last_mode = p.mode;
        }
        switch (p.mode) {
        case api::ChargeMode::AcIec:
            return {false, nullptr};
        case api::ChargeMode::AcIso2:
        case api::ChargeMode::DcIso2:
            return {false, std::make_unique<SlacMatching>(ctx)};
        default:
            return {false, nullptr};
        }
    }
    case EK::BspMeasurement: {
        const auto& m = std::get<BspMeasurementPayload>(ev.payload);
        ctx.vars.pwm_duty_cycle = m.cp_pwm_duty_cycle;
        if (ctx.vars.charge_mode == api::ChargeMode::AcIec && m.cp_pwm_duty_cycle > 7 && m.cp_pwm_duty_cycle < 97) {
            return {false, std::make_unique<Charging>(ctx)};
        }
        return {false, nullptr};
    }
    case EK::Unplug:
        return {false, std::make_unique<Unplugged>(ctx)};
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
        return handle_query_state(ctx, api::FsmState::Plugged);
    case EK::StopSession:
        ctx.publish_e2m_command_ack("stop_session", "no session active");
        return {false, nullptr};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "no session active");
        return {false, nullptr};
    case EK::ResumeSession:
        ctx.publish_e2m_command_ack("resume_session", "no session active");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "no session active");
        return {false, nullptr};
    case EK::ClearFault:
        ctx.publish_e2m_command_ack("clear_fault", "no session active");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "no session active");
        return {false, nullptr};
    case EK::SetSoc:
        ctx.publish_e2m_command_ack("set_soc", "no session active");
        return {false, nullptr};
    case EK::RunScenario:
        ctx.publish_e2m_command_ack("run_scenario", "no session active");
        return {false, nullptr};
    case EK::Plug:
    case EK::Enable:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoStopFromCharger:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
    case EK::StateDeadline:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
