// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Disabled.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"

namespace module {

void Disabled::enter() {
    ctx.publish_e2m_state(API_types::ev_controller::FsmState::Disabled);
}

StateBase::Result Disabled::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::Enable:
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::QueryState:
        return handle_query_state(ctx, API_types::ev_controller::FsmState::Disabled);
    case EK::Disable:
        return {true, nullptr};
    case EK::StopSession:
    case EK::SetSoc:
    case EK::SetChargingCurrent:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::BcbToggle:
        return reject(ev, "module disabled");
    case EK::BspEvent:
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
    case EK::IsoV2GFinished:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
    case EK::StateDeadline:
    // RaiseError / ClearError are intercepted on the loop thread before the
    // FSM feed, so they never reach a state; listed only to keep the switch
    // exhaustive (-Werror=switch).
    // ConfigureSession is intercepted pre-FSM (loop thread); BeginSession is
    // internal to Plugged and Unplugged. Listed for switch exhaustiveness.
    case EK::ConfigureSession:
    case EK::BeginSession:
    case EK::SetPresentValues:
    case EK::RaiseError:
    case EK::ClearError:
    case EK::InternalFault:
    case EK::Shutdown:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
