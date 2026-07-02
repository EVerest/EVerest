// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ChargingPwmPaused.hpp"

#include "../FsmContext.hpp"
#include "Charging.hpp"
#include "Unplugged.hpp"

namespace module {

namespace api = API_types::ev_simulator;

void ChargingPwmPaused::enter() {
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.allow_power_on(false);
    ctx.publish_e2m_state(api::FsmState::ChargingPwmPaused);
    // No state_timer armed; SocIntegrator skipped (state != Charging).
}

StateBase::Result ChargingPwmPaused::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::BspMeasurement: {
        const auto& m = std::get<BspMeasurementPayload>(ev.payload);
        ctx.vars.pwm_duty_cycle = m.cp_pwm_duty_cycle;
        if (m.cp_pwm_duty_cycle > 7 && m.cp_pwm_duty_cycle < 97) {
            return {false, std::make_unique<Charging>(ctx)};
        }
        return {false, nullptr};
    }
    case EK::Unplug:
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::BspEvent:
        return handle_disconnect(ev);
    case EK::StopSession:
        // AC IEC: direct unplug-equivalent.
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::PauseSession:
        ctx.publish_e2m_command_ack("pause_session", "AC IEC: ISO verbs not applicable");
        return {false, nullptr};
    case EK::ResumeSession:
        ctx.publish_e2m_command_ack("resume_session", "AC IEC: ISO verbs not applicable");
        return {false, nullptr};
    case EK::IsoStopFromCharger:
        ctx.publish_e2m_command_ack("iso_stop_from_charger", "AC IEC: ISO verbs not applicable");
        return {false, nullptr};
    case EK::IsoPauseFromCharger:
        ctx.publish_e2m_command_ack("iso_pause_from_charger", "AC IEC: ISO verbs not applicable");
        return {false, nullptr};
    case EK::IsoPowerReady:
        ctx.publish_e2m_command_ack("iso_power_ready", "AC IEC: ISO verbs not applicable");
        return {false, nullptr};
    case EK::BcbToggle:
        ctx.publish_e2m_command_ack("bcb_toggle", "AC IEC: ISO verbs not applicable");
        return {false, nullptr};
    case EK::SetChargingCurrent:
        ctx.publish_e2m_command_ack("set_charging_current", "AC IEC: ISO verbs not applicable");
        return {false, nullptr};
    case EK::InjectFault: {
        auto p = std::get<api::InjectFaultParams>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::ChargingPwmPaused);
    case EK::ClearFault:
    case EK::SetSoc:
    case EK::RunScenario:
    case EK::Plug:
    case EK::Enable:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::DcEvsePresentCurrent:
    case EK::DcEvsePresentVoltage:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
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
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
