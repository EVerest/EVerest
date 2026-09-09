// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EventDispatch.hpp"

#include "states/Faulted.hpp"

#include <everest/logging.hpp>
#include <everest_api_types/ev_simulator/API.hpp>

#include <exception>
#include <optional>
#include <string>

namespace module {

namespace {

// Replace the active FSM with a fresh one rooted at Faulted carrying an
// Internal fault. Used when a handler throws — the FSM's `feed` does not
// expose a way to reseed mid-handler, so we destroy and reconstruct.
void force_internal_fault(std::unique_ptr<fsm::v2::FSM<StateBase>>& fsm, FsmContext& ctx, const std::string& message) {
    using API_types::ev_simulator::FaultReport;
    using API_types::ev_simulator::FaultType;
    ctx.vars.last_fault = FaultReport{FaultType::Internal, message, std::nullopt};
    // Destroy first so the prior state's leave() runs before Faulted::enter()
    // observes ctx.vars.last_fault.
    fsm.reset();
    fsm = std::make_unique<fsm::v2::FSM<StateBase>>(std::make_unique<Faulted>(ctx));
}

} // namespace

void feed_with_fault_isolation(std::unique_ptr<fsm::v2::FSM<StateBase>>& fsm, FsmContext& ctx, const Event& ev) {
    try {
        fsm->feed(ev);
    } catch (const std::exception& ex) {
        EVLOG_error << "EvSimulator: exception while feeding event kind=" << static_cast<int>(kind_of(ev)) << ": "
                    << ex.what();
        try {
            force_internal_fault(fsm, ctx, ex.what());
        } catch (const std::exception& nested) {
            EVLOG_error << "EvSimulator: secondary exception while transitioning to Faulted: " << nested.what();
        } catch (...) {
            EVLOG_error << "EvSimulator: secondary unknown exception while transitioning to Faulted";
        }
    } catch (...) {
        EVLOG_error << "EvSimulator: unknown exception while feeding event kind=" << static_cast<int>(kind_of(ev));
        try {
            force_internal_fault(fsm, ctx, "unknown exception");
        } catch (...) {
            EVLOG_error << "EvSimulator: secondary unknown exception while transitioning to Faulted";
        }
    }
}

} // namespace module
