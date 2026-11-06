// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "Events.hpp"

#include <everest_api_types/ev_controller/API.hpp>
#include <generated/types/board_support_common.hpp>
#include <memory>
#include <string_view>

namespace module {

class FsmContext;

class StateBase {
public:
    using ContainerType = std::unique_ptr<StateBase>;
    using EventType = Event;

    struct Result {
        bool unhandled{false};
        ContainerType new_state{nullptr};
    };

    explicit StateBase(FsmContext& ctx_) : ctx(ctx_) {
    }
    virtual ~StateBase() = default;

    virtual void enter() {
    }
    Result feed(EventType ev);
    // Template method: cancels any state-scoped deadline after the override
    // hook runs. Non-virtual so derived states cannot accidentally skip the
    // base cleanup; they specialize via the protected on_leave() hook.
    void leave();
    virtual API_types::ev_controller::FsmState get_id() const = 0;

protected:
    FsmContext& ctx;
    virtual Result on_event(EventType ev) = 0;

    // Blanket command rejection: publishes a Rejected command_ack whose verb
    // is the central command_verb(kind_of(ev)) and whose reason is the
    // caller-supplied per-state reason, then signals "handled, no transition".
    // Centralizes the boilerplate the states' fall-through reject arms share.
    Result reject(const Event& ev, std::string_view reason);

    Result handle_pilot_event(const Event& ev);
    static bool is_charger_fault(::types::board_support_common::Event pilot);
    // Pilot E (0 V: charger off or pilot shorted) or F (charger fault). Stops
    // drawing power and reports a ChargerFault naming the pilot state.
    Result handle_charger_fault(::types::board_support_common::Event pilot);

    // Override to add state-specific cleanup. Default is a no-op. Called by
    // leave() before the base-class timer cancel; do not call StateBase::leave()
    // from overrides; it runs unconditionally.
    virtual void on_leave() {
    }
};

} // namespace module
