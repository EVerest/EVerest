// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Unplugged.hpp"

#include "../CurvePlayer.hpp"
#include "../FsmContext.hpp"
#include "Plugged.hpp"

#include <algorithm>

namespace module {

namespace api = API_types::ev_controller;

void Unplugged::enter() {
    // The BSP peer's simulation loop is gated on its own enabled flag; arm
    // it on every Unplugged entry so the path from Disabled (where the BSP
    // is parked) back to Unplugged restarts the peer's CP/PWM propagation.
    // Idempotent on transitions between non-Disabled states.
    ctx.enable_bsp(true);
    if (!ctx.pilot_present) {
        ctx.set_cp(::types::ev_board_support::EvCpState::A);
    }
    ctx.allow_power_on(false);
    ctx.iso_stop_charging();
    ctx.slac_reset();
    ctx.reset_session_vars();
    ctx.vars.hold_session = false;
    ctx.publish_e2m_state(api::FsmState::Unplugged);
    m_pilot_start_pending = ctx.pilot_present;
    if (m_pilot_start_pending) {
        ctx.enqueue(Event{BeginSessionEvt{ctx.vars.plug_generation}});
    }
}

StateBase::Result Unplugged::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::BeginSession:
        if (m_pilot_start_pending && ctx.pilot_present &&
            std::get<BeginSessionEvt>(ev.payload).plug_generation == ctx.vars.plug_generation) {
            m_pilot_start_pending = false;
            return {false, std::make_unique<Plugged>(ctx)};
        }
        return {true, nullptr};
    case EK::StateDeadline:
        return {true, nullptr};
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::SetSoc: {
        auto p = std::get<api::SetSocParams>(ev.payload);
        // Clamp at the write site so a single out-of-range value (e.g. 1e30)
        // cannot publish on e2m/ev_info before the next-tick clamp catches it.
        const float clamped_soc = std::clamp(p.soc_pct, 0.0f, 100.0f);
        ctx.vars.soc_pct = clamped_soc;
        ctx.vars.battery_charge_wh = ctx.vars.battery_capacity_wh * (clamped_soc / 100.0f);
        return {false, nullptr};
    }
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Unplugged);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::ResumeSession:
    case EK::SetChargingCurrent:
    case EK::BcbToggle:
        return reject(ev, "no session active");
    case EK::BspEvent: {
        using Pilot = ::types::board_support_common::Event;
        const auto pilot = std::get<BspEventPayload>(ev.payload).bsp_event.event;
        if (pilot == Pilot::Disconnected) {
            if (m_pilot_start_pending) {
                ctx.set_cp(::types::ev_board_support::EvCpState::A);
            }
            m_pilot_start_pending = false;
        }
        if (ctx.pilot_present && is_charger_fault(pilot)) {
            m_pilot_start_pending = false;
            return handle_charger_fault(pilot);
        }
        if (!ctx.pilot_present && (pilot == Pilot::A || pilot == Pilot::B || pilot == Pilot::C)) {
            return {false, std::make_unique<Plugged>(ctx)};
        }
        return {true, nullptr};
    }
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
    // RaiseError / ClearError are intercepted on the loop thread before the
    // FSM feed; listed only to keep the switch exhaustive (-Werror=switch).
    // ConfigureSession is intercepted pre-FSM (loop thread).
    case EK::ConfigureSession:
    case EK::SetPresentValues:
    case EK::RaiseError:
    case EK::ClearError:
    case EK::InternalFault:
    case EK::Shutdown:
    case EK::Enable:
        return {true, nullptr};
    }
    return {true, nullptr};
}

} // namespace module
