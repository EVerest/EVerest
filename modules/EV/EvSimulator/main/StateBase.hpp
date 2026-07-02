// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "Events.hpp"

#include <everest_api_types/ev_simulator/API.hpp>
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
    virtual Result feed(EventType ev) = 0;
    // Template method: cancels any state-scoped deadline after the override
    // hook runs. Non-virtual so derived states cannot accidentally skip the
    // base cleanup; they specialize via the protected on_leave() hook.
    void leave();
    virtual API_types::ev_simulator::FsmState get_id() const = 0;

protected:
    FsmContext& ctx;

    // Blanket command rejection: publishes a Rejected command_ack whose verb
    // is the central command_verb(kind_of(ev)) and whose reason is the
    // caller-supplied per-state reason, then signals "handled, no transition".
    // Centralizes the boilerplate the states' fall-through reject arms share.
    Result reject(const Event& ev, std::string_view reason);

    // Shared BspEvent handling: a "Disconnected" board-support event drops the
    // session and transitions to Unplugged; any other BspEvent is left
    // unhandled (returned as such) for the runtime to pass on. Replaces the
    // eight byte-identical copies that lived in the connected states.
    Result handle_disconnect(const Event& ev);

    // Override to add state-specific cleanup. Default is a no-op. Called by
    // leave() before the base-class timer cancel; do not call StateBase::leave()
    // from overrides — it runs unconditionally.
    virtual void on_leave() {
    }
};

} // namespace module
