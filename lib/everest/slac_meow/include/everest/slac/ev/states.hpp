// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <string>

#include <everest/slac/ev/context.hpp>
#include <everest/slac/ev/event.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/types.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::ev {

enum class StateID {
    Reset,
    Idle,
    WaitParmCnf,
    Sounding,
    WaitAttenCharInd,
    WaitMatchCnf,
    WaitSetKeyCnf,
    Matched,
    Failed,
};

std::string to_string(StateID id);

struct Result {
    constexpr Result() = default;
    /// handled, but no transition
    explicit constexpr Result(bool handled) : unhandled(not handled) {
    }
    /// handled, transition to the given state
    Result(BasePointerType next_state) : unhandled(false), new_state(std::move(next_state)) {
    }

    bool unhandled{true};
    BasePointerType new_state{nullptr};
};

/// Consumed the event without changing state.
inline Result handled() {
    return Result{true};
}

struct StateBase {
    using ContainerType = BasePointerType;
    using EventType = SlacEvent;

    StateBase(Context& ctx, StateID id) : m_ctx(ctx), m_id(id) {
    }
    virtual ~StateBase() = default;

    StateID get_id() const {
        return m_id;
    }

    virtual void enter() {
    }
    virtual Result feed(SlacEvent const&) = 0;
    virtual void leave() {
    }

protected:
    Context& m_ctx;

private:
    StateID m_id;
};

} // namespace everest::slac::ev
