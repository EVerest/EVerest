// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Stopping.hpp"

#include "../FsmContext.hpp"

#include <chrono>

namespace module {

namespace api = API_types::ev_controller;

void Stopping::enter() {
    ctx.iso_end_session();
    ctx.allow_power_on(false);
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.arm_timer(std::chrono::seconds(10));
    ctx.publish_e2m_state(api::FsmState::Stopping);
}

StateBase::Result Stopping::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::IsoV2GFinished:
        return finish_stopping(ctx);
    case EK::StateDeadline:
        // Forced timeout: the session never reported finished, so stop it
        // again before ending the stop as if it had.
        ctx.iso_stop_charging();
        return finish_stopping(ctx);
    case EK::BspEvent:
        return handle_pilot_event(ev);
    case EK::InternalFault: {
        const auto& p = std::get<InternalFault>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Stopping);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::BcbToggle:
    case EK::SetSoc:
        return reject(ev, "session stopping");
    case EK::Enable:
    case EK::BspMeasurement:
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

} // namespace module
