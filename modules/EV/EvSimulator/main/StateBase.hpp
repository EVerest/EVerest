// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "Events.hpp"

#include <everest_api_types/ev_simulator/API.hpp>
#include <memory>

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
    // Default cancels any state-scoped deadline. Overrides MUST call
    // StateBase::leave() at the end for extra cleanup; that's a
    // documentation-level contract enforced manually in review.
    virtual void leave();
    virtual API_types::ev_simulator::FsmState get_id() const = 0;

protected:
    FsmContext& ctx;
};

} // namespace module
