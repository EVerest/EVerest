// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ChargerFault.hpp"

#include "../FsmContext.hpp"
#include "Plugged.hpp"
#include "Unplugged.hpp"

namespace module {

void ChargerFault::enter() {
    ctx.reset_session_vars();
    ctx.vars.hold_session = false;
    ctx.publish_e2m_state(API_types::ev_controller::FsmState::ChargerFault);
}

StateBase::Result ChargerFault::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::BspEvent:
        switch (std::get<BspEventPayload>(ev.payload).bsp_event.event) {
        case ::types::board_support_common::Event::Disconnected:
            return {false, std::make_unique<Unplugged>(ctx)};
        case ::types::board_support_common::Event::A:
            return {false, std::make_unique<Plugged>(ctx)};
        default:
            return {true, nullptr};
        }
    case EK::BspMeasurement: {
        const auto duty = std::get<BspMeasurementPayload>(ev.payload).cp_pwm_duty_cycle;
        if (duty > 4.0f && duty < 97.0f) {
            return {false, std::make_unique<Plugged>(ctx)};
        }
        return {true, nullptr};
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, API_types::ev_controller::FsmState::ChargerFault);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::SetSoc:
    case EK::BcbToggle:
        return reject(ev, "charger fault");
    case EK::Enable:
    case EK::IsoDerNegotiated:
    case EK::IsoDerControl:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::DcEvsePresentCurrent:
    case EK::DcEvsePresentVoltage:
    case EK::V2gMessage:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoStopFromCharger:
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
    case EK::ConfigureSession:
    case EK::BeginSession:
    case EK::SetPresentValues:
    case EK::RaiseError:
    case EK::ClearError:
    case EK::StateDeadline:
    case EK::InternalFault:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
