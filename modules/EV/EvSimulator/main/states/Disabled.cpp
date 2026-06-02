// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Disabled.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"

namespace module {

void Disabled::enter() {
    ctx.publish_e2m_state(API_types::ev_simulator::FsmState::Disabled);
}

StateBase::Result Disabled::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::Enable:
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::QueryState:
        return handle_query_state(ctx, API_types::ev_simulator::FsmState::Disabled);
    case EK::Disable:
        return {true, nullptr};
    case EK::InjectFault:
        ctx.publish_e2m_command_ack("inject_fault", "InjectFault requires session");
        return {false, nullptr};
    case EK::StopSession:
    case EK::SetSoc:
    case EK::SetChargingCurrent:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::BcbToggle:
    case EK::RunScenario:
    case EK::ClearFault:
    case EK::Plug:
    case EK::Unplug:
        return reject(ev, "module disabled");
    case EK::BspEvent:
    case EK::BspMeasurement:
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
    // RaiseError / ClearError are intercepted on the loop thread before the
    // FSM feed, so they never reach a state; listed only to keep the switch
    // exhaustive (-Werror=switch).
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
