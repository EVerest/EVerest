// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "Faulted.hpp"

#include "../FsmContext.hpp"
#include "Unplugged.hpp"

#include <everest/logging.hpp>
#include <fmt/format.h>

#include <string>

namespace module {

namespace api = API_types::ev_controller;

namespace {
// Faulted is where a throwing handler is rerooted, so its own enter() must not throw. A failure is
// logged and appended to `failures` for the fault report.
template <typename F> void contained(std::string& failures, const char* what, F&& f) {
    std::string reason;
    try {
        f();
        return;
    } catch (const std::exception& ex) {
        reason = ex.what();
    } catch (...) {
        reason = "unknown exception";
    }
    EVLOG_error << "EvController: Faulted could not " << what << ": " << reason;
    failures += fmt::format("; could not {}: {}", what, reason);
}
} // namespace

void Faulted::enter() {
    ctx.vars.session.reset();
    ctx.vars.was_full = false;
    std::string failures;
    // Software faults must also remove power permission, release a held C or D and stop the V2G session.
    contained(failures, "remove power permission", [this] { ctx.allow_power_on(false); });
    using Cp = ::types::ev_board_support::EvCpState;
    if (ctx.vars.commanded_cp == Cp::C || ctx.vars.commanded_cp == Cp::D) {
        contained(failures, "release CP to B", [this] { ctx.set_cp(Cp::B); });
    }
    contained(failures, "stop the V2G session", [this] { ctx.iso_stop_charging(); });
    if (!ctx.vars.last_fault) {
        EVLOG_error << "EvController: Faulted entered without last_fault set";
    }
    auto f = ctx.vars.last_fault.value_or(
        api::FaultReport{api::FaultType::Internal, std::string{"last_fault unset"}, std::nullopt});
    const auto reason = f.message;
    if (!failures.empty()) {
        f.message = reason.value_or("") + failures;
    }
    ctx.vars.last_fault = f;
    contained(failures, "publish the fault", [this, &f] { ctx.publish_e2m_fault(f); });
    contained(failures, "publish the state", [this] { ctx.publish_e2m_state(api::FsmState::Faulted); });
    // The kept fault also names the publish failures that the report could not carry.
    if (!failures.empty()) {
        ctx.vars.last_fault->message = reason.value_or("") + failures;
    }
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
    case EK::IsoPowerDeliveryStopped:
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
