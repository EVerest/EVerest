// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Paused.hpp"

#include "../FsmContext.hpp"
#include "BcbToggling.hpp"
#include "Charging.hpp"
#include "Stopping.hpp"

#include <everest/logging.hpp>

#include <chrono>

namespace module {

namespace api = API_types::ev_controller;

void Paused::enter() {
    ctx.allow_power_on(false);
    ctx.iso_pause_charging();
    // A resume requested mid-teardown is deferred until IsoV2GFinished; start
    // each pause with a clean slate so a stale flag from an earlier cycle can
    // never auto-fire a resume.
    ctx.vars.resume_pending = false;
    // PowerDeliveryRes confirms the pause before WeldingDetectionReq, for which
    // ISO 15118-2 [V2G2-920]..[V2G2-922] require CP B. Transport teardown is too late.
    // With no live session there is no response to wait for.
    if (not ctx.vars.iso_session_active) {
        ctx.set_cp(::types::ev_board_support::EvCpState::B);
    }
    ctx.arm_timer(std::chrono::hours(1));
    ctx.publish_e2m_state(api::FsmState::Paused);
}

StateBase::Result Paused::on_event(EventType ev) {
    using EK = EventKind;
    switch (kind_of(ev)) {
    case EK::ResumeSession:
        // AcIec resume is a direct CP B->C transition: there is no HLC to
        // re-establish, so BCB toggling does not apply. ISO modes (and the
        // no-session test path) keep the BcbToggling round-trip that
        // signals EVCC's intent to resume HLC to the SECC.
        if (ctx.vars.charge_mode() == api::ChargeMode::AcIec) {
            return {false, std::make_unique<Charging>(ctx)};
        }
        // The ISO15118_ev module runs one V2G comm session per start_charging. If the paused
        // session has not finished tearing down yet, defer the resume: starting
        // the BCB wake-up + re-SLAC now would let the previous session's lagging
        // SessionStop clobber the freshly re-established link (the SECC then
        // drops to D-LINK_PAUSE + PWM off and never re-applies the charging PWM,
        // so the resume hangs). feed(IsoV2GFinished) releases it once the
        // session is gone. If the session already finished, fall through.
        if (ctx.vars.iso_session_active) {
            ctx.vars.resume_pending = true;
            ctx.vars.bcb_pending = 6;
            // Arm a short fallback timer: if the ISO15118_ev module never publishes
            // v2g_session_finished (abnormal teardown), IsoV2GFinished never
            // arrives and the deferred resume would otherwise hang on the 1h
            // pause timer. When this fires with resume_pending still set,
            // feed(StateDeadline) does a best-effort resume instead.
            ctx.arm_timer(std::chrono::seconds(15));
            return {false, nullptr};
        }
        // bcb_remaining is the edge count consumed by BcbToggling; see
        // BcbToggling::enter for how the default 6 maps to round-trips.
        ctx.vars.bcb_remaining = 6;
        return {false, std::make_unique<BcbToggling>(ctx)};
    case EK::BcbToggle: {
        // Explicit BcbToggle accepts the user-provided round-trip count via
        // BcbToggleParams::count. count <= 0 is rejected; absent count falls
        // back to the default of 3 round-trips (= 6 edges) so the behavior
        // matches the implicit ResumeSession path. An upper bound guards
        // against signed-overflow UB on `round_trips * 2` and against a
        // pathologically long toggle loop (cap is paranoia-only; well
        // beyond any realistic test usage at the 250 ms-per-edge timer).
        constexpr int32_t kMaxBcbRoundTrips = 1000;
        auto p = std::get<api::BcbToggleParams>(ev.payload);
        int32_t round_trips = p.count.value_or(3);
        if (round_trips <= 0) {
            ctx.publish_e2m_command_ack("bcb_toggle", "count must be > 0");
            return {false, nullptr};
        }
        if (round_trips > kMaxBcbRoundTrips) {
            ctx.publish_e2m_command_ack("bcb_toggle", "count exceeds 1000");
            return {false, nullptr};
        }
        // An explicit BcbToggle wakes the SECC the same way an implicit resume
        // does, so it must honor the same gate: starting the BCB edges while the
        // paused V2G session is still tearing down lets the previous session's
        // lagging SessionStop clobber the freshly re-established link. Defer
        // until IsoV2GFinished (or the bounded fallback), carrying the requested
        // edge count in bcb_pending.
        if (ctx.vars.iso_session_active) {
            ctx.vars.bcb_pending = round_trips * 2;
            ctx.vars.resume_pending = true;
            ctx.arm_timer(std::chrono::seconds(15));
            return {false, nullptr};
        }
        ctx.vars.bcb_remaining = round_trips * 2;
        return {false, std::make_unique<BcbToggling>(ctx)};
    }
    case EK::V2gMessage: {
        // The resume's PowerDelivery(Start) is sent after Paused has been left,
        // so a PowerDeliveryRes here acknowledges the stop.
        const auto& msg = std::get<V2gMessagePayload>(ev.payload);
        if (msg.id == "PowerDeliveryRes") {
            ctx.set_cp(::types::ev_board_support::EvCpState::B);
        }
        return {false, nullptr};
    }
    case EK::IsoV2GFinished:
        // The paused V2G session has finished tearing down (iso_session_active
        // was cleared pre-feed). If a resume was requested while it was still
        // live, release it now: seed the BCB edge count and wake the SECC
        // (BcbToggling drives CP from here on).
        if (ctx.vars.resume_pending) {
            ctx.vars.resume_pending = false;
            // Release the edge count the deferred resume carried. bcb_pending is
            // 6 for an implicit ResumeSession and 2*count for an explicit
            // BcbToggle; fall back to 6 if it was never seeded.
            ctx.vars.bcb_remaining = ctx.vars.bcb_pending > 0 ? ctx.vars.bcb_pending : 6;
            ctx.vars.bcb_pending = 0;
            return {false, std::make_unique<BcbToggling>(ctx)};
        }
        // No resume pending. CP should already be at B from
        // feed(V2gMessage) on PowerDeliveryRes; re-assert it as a backstop for
        // the case where the peer publishes no per-message signal at all (an
        // older ISO peer), so a missing signal degrades to the previous
        // late-drop behaviour rather than leaving CP stuck at C.
        ctx.set_cp(::types::ev_board_support::EvCpState::B);
        return {false, nullptr};
    case EK::StateDeadline:
        // A deferred resume armed a short fallback timer. If it fires while the
        // resume is still pending, IsoV2GFinished was never observed (abnormal
        // V2G teardown): do a best-effort resume into BcbToggling rather than
        // hanging to the test timeout. Release the carried edge count the same
        // way IsoV2GFinished would have.
        if (ctx.vars.resume_pending) {
            EVLOG_warning << "EvController: resume fallback fired (IsoV2GFinished "
                             "not observed); resuming best-effort";
            ctx.vars.resume_pending = false;
            ctx.vars.bcb_remaining = ctx.vars.bcb_pending > 0 ? ctx.vars.bcb_pending : 6;
            ctx.vars.bcb_pending = 0;
            return {false, std::make_unique<BcbToggling>(ctx)};
        }
        return {false, std::make_unique<Stopping>(ctx)};
    case EK::BspEvent:
        return handle_pilot_event(ev);
    case EK::InternalFault: {
        const auto& p = std::get<InternalFault>(ev.payload);
        return transition_to_fault(ctx, p);
    }
    case EK::Disable:
        return transition_to_disabled(ctx);
    case EK::QueryState:
        return handle_query_state(ctx, api::FsmState::Paused);
    case EK::StopSession:
    case EK::PauseSession:
    case EK::SetChargingCurrent:
    case EK::SetSoc:
        return reject(ev, "session paused");
    case EK::Enable:
    case EK::BspMeasurement:
    case EK::IsoDerNegotiated:
    case EK::IsoDerControl:
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
