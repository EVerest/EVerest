// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Stopping.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"

#include <chrono>

namespace module {

namespace api = API_types::ev_simulator;

void Stopping::enter() {
    ctx.iso_stop_charging();
    ctx.allow_power_on(false);
    ctx.set_cp(::types::ev_board_support::EvCpState::B);
    ctx.arm_timer(std::chrono::seconds(10));
    ctx.publish_e2m_state(api::FsmState::Stopping);
}

StateBase::Result Stopping::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::IsoV2GFinished:
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::StateDeadline:
        // Forced timeout - transition to Unplugged.
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
        return handle_query_state(ctx, api::FsmState::Stopping);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::ClearFault:
    case EK::BcbToggle:
    case EK::SetSoc:
    case EK::RunScenario:
        return reject(ev, "session stopping");
    case EK::Unplug:
    case EK::Plug:
    case EK::Enable:
    case EK::BspMeasurement:
    case EK::EvInfo:
    case EK::SlacState:
    case EK::DcEvsePresentCurrent:
    case EK::DcEvsePresentVoltage:
    case EK::IsoPowerReady:
    case EK::IsoAcMaxCurrent:
    case EK::IsoAcTargetPower:
    case EK::IsoStopFromCharger:
    case EK::IsoDcPowerOn:
    case EK::IsoPauseFromCharger:
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
