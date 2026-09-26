// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Faulted.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"

#include <everest/logging.hpp>

namespace module {

namespace api = API_types::ev_simulator;

void Faulted::enter() {
    // Reset before any early return so session-scoped state cannot survive
    // into a re-entered session after ClearFault. Faulted only transitions to
    // Unplugged (ClearFault / Unplug), so dropping the whole session here is
    // observationally equivalent to the prior partial reset and removes the
    // last scattered session-field reset.
    ctx.vars.session.reset();
    ctx.vars.was_full = false;
    // Drop what the fault leaves asserted, on every fault type including the software ones.
    // transition_to_disabled does both and so did the module this replaces; without them,
    // entering Faulted from Charging leaves power permitted and the V2G session live until the
    // fault is cleared or the cable pulled. No effect against YetiSimulator's log-only EV-side
    // stub, but config-CB-EVAL-EV.yaml drives a real ev_board_support.
    ctx.allow_power_on(false);
    ctx.iso_stop_charging();
    if (!ctx.vars.last_fault) {
        EVLOG_error << "EvSimulator: Faulted entered without last_fault set";
        ctx.publish_e2m_fault(
            api::FaultReport{api::FaultType::Internal, std::string{"last_fault unset"}, std::nullopt});
        ctx.publish_e2m_state(api::FsmState::Faulted);
        return;
    }
    const auto& f = *ctx.vars.last_fault;
    switch (f.type) {
    case api::FaultType::DiodeFail:
        if (ctx.peer_actions.bsp.present) {
            ctx.peer_actions.bsp.diode_fail(true);
        }
        break;
    case api::FaultType::RcdError:
        if (ctx.peer_actions.bsp.present) {
            ctx.peer_actions.bsp.set_rcd_error(f.rcd_mA.value_or(0.0f));
        }
        break;
    case api::FaultType::CpErrorE:
        ctx.set_cp(::types::ev_board_support::EvCpState::E);
        break;
    case api::FaultType::SlacTimeout:
    case api::FaultType::V2GTimeout:
    case api::FaultType::Internal:
        break; // no BSP call for software faults
    }
    ctx.publish_e2m_fault(f);
    ctx.publish_e2m_state(api::FsmState::Faulted);
}

void Faulted::on_leave() {
    if (ctx.vars.last_fault) {
        switch (ctx.vars.last_fault->type) {
        case api::FaultType::DiodeFail:
            if (ctx.peer_actions.bsp.present) {
                ctx.peer_actions.bsp.diode_fail(false);
            }
            break;
        case api::FaultType::RcdError:
            if (ctx.peer_actions.bsp.present) {
                ctx.peer_actions.bsp.set_rcd_error(0.0f);
            }
            break;
        case api::FaultType::CpErrorE:
            // CP normalized by next state's enter (Unplugged sets A).
            break;
        case api::FaultType::SlacTimeout:
        case api::FaultType::V2GTimeout:
        case api::FaultType::Internal:
            break;
        }
    }
}

StateBase::Result Faulted::feed(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::ClearFault:
        return {false, std::make_unique<Unplugged>(ctx)};
    case EK::Unplug:
        return {false, std::make_unique<Unplugged>(ctx)};
    // QueryState answers in Faulted like it does in the other ten states. The
    // command is operator-facing (logging, UI state display), so a faulted
    // simulator is exactly when a pull-model query must still report where it
    // is; leaving it inert made a UI show a stale pre-fault state.
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Faulted);
    case EK::Enable:
    case EK::Disable:
    case EK::Plug:
    case EK::SetSoc:
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::InjectFault:
    case EK::BcbToggle:
    case EK::RunScenario:
    case EK::BspEvent:
    case EK::BspMeasurement:
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
    // an internal Plugged-only self-advance. Listed for switch exhaustiveness.
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
