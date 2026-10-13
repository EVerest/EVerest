// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Faulted.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"

#include <everest/logging.hpp>

namespace module {

namespace api = API_types::ev_controller;

void Faulted::enter() {
    ctx.vars.session.reset();
    ctx.vars.was_full = false;
    // Software faults must also remove power permission and stop the V2G session.
    ctx.allow_power_on(false);
    ctx.iso_stop_charging();
    if (!ctx.vars.last_fault) {
        EVLOG_error << "EvController: Faulted entered without last_fault set";
        ctx.publish_e2m_fault(
            api::FaultReport{api::FaultType::Internal, std::string{"last_fault unset"}, std::nullopt});
        ctx.publish_e2m_state(api::FsmState::Faulted);
        return;
    }
    const auto& f = *ctx.vars.last_fault;
    ctx.publish_e2m_fault(f);
    ctx.publish_e2m_state(api::FsmState::Faulted);
}

StateBase::Result Faulted::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::BspEvent:
        if (std::get<BspEventPayload>(ev.payload).bsp_event.event ==
            ::types::board_support_common::Event::Disconnected) {
            return {false, std::make_unique<Unplugged>(ctx)};
        }
        return {true, nullptr};
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Faulted);
    case EK::Enable:
    case EK::Disable:
    case EK::SetSoc:
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::InternalFault:
    case EK::BcbToggle:
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
