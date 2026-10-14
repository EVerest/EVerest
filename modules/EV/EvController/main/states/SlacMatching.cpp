// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SlacMatching.hpp"

#include "../FsmContext.hpp"
#include "Faulted.hpp"
#include "V2GNegotiating.hpp"

#include <chrono>

namespace module {

namespace api = API_types::ev_controller;
namespace {
constexpr auto kMcsWakePulse = std::chrono::seconds{4};
} // namespace

void SlacMatching::enter() {
    m_wake_pulse = ctx.vars.mcs_enabled();
    if (m_wake_pulse) {
        // IEC 61851-23-3 CC.5.2.4: wake the MCS EVSE before matching.
        ctx.set_cp(::types::ev_board_support::EvCpState::C);
        ctx.arm_timer(kMcsWakePulse);
    } else {
        start_matching();
    }
    ctx.publish_e2m_state(api::FsmState::SlacMatching);
}

void SlacMatching::start_matching() {
    // slac_trigger_matching returns false in two distinct cases: the ev_slac
    // peer is not wired at all, or a wired peer's trigger_matching() call
    // returned false. Without an early fault transition the state would arm
    // the 30s deadline timer and only fault later with a misleading
    // SlacTimeout. Route into Faulted on the next on_wake flush via a
    // synthetic InternalFault, carrying the descriptive message on the payload
    // so it cannot go stale if an intervening transition is flushed first.
    if (!ctx.slac_trigger_matching()) {
        const std::string message =
            ctx.peer_actions.slac.present ? "ev_slac trigger_matching rejected" : "no ev_slac peer";
        ctx.enqueue(Event{InternalFault{api::FaultType::Internal, message}});
        return;
    }
    ctx.vars.slac_state = "MATCHING";
    ctx.arm_timer(std::chrono::seconds(30));
}

StateBase::Result SlacMatching::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::SlacState: {
        const auto& p = std::get<SlacStatePayload>(ev.payload);
        if (!m_wake_pulse && p.state == "MATCHED") {
            // The link is matched again; clear the torn-down flag so a later
            // resume that did not tear SLAC down takes the direct path.
            ctx.vars.slac_unmatched = false;
            return {false, std::make_unique<V2GNegotiating>(ctx)};
        }
        return {true, nullptr};
    }
    case EK::StateDeadline:
        if (m_wake_pulse) {
            m_wake_pulse = false;
            ctx.set_cp(::types::ev_board_support::EvCpState::B);
            start_matching();
            return {false, nullptr};
        }
        ctx.vars.last_fault =
            api::FaultReport{api::FaultType::SlacTimeout, std::string{"SLAC match deadline exceeded"}, std::nullopt};
        return {false, std::make_unique<Faulted>(ctx)};
    case EK::BspEvent:
        return handle_pilot_event(ev);
    case EK::InternalFault: {
        const auto& p = std::get<InternalFault>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::SlacMatching);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::BcbToggle:
    case EK::SetSoc:
        return reject(ev, "slac matching in progress");
    case EK::Enable:
    case EK::BspMeasurement:
    case EK::IsoDerNegotiated:
    case EK::IsoDerControl:
    case EK::EvInfo:
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
