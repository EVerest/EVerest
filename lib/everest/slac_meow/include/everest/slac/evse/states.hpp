// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <everest/slac/evse/context.hpp>
#include <everest/slac/evse/event.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse {

enum class StateID {
    Init,
    Reset,
    ResetChip,
    Idle,
    Matching,
    WaitForLink,
    Matched,
    Failed,
    SessionWaitStartAtten,
    SessionSounding,
    SessionFinalizeSounding,
    SessionWaitAttenRsp,
    SessionWaitSlacMatch,
    SessionMatchComplete,
    SessionFailed,
};

std::string to_string(StateID id);

// A snapshot of the active state configuration; states report themselves into it. Deliberately
// independent of everest_api_types - only the facade knows how to turn it into telemetry.
struct StateTree {
    std::string name{};
    std::vector<StateTree> children{};
    std::vector<StateTree> sessions{};
};

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

    /// Cheap change detection, evaluated after every event. Composites append their children.
    virtual void signature(std::string& out) const {
        out += std::to_string(static_cast<int>(m_id));
        out += ';';
    }

    /// Full snapshot, only built when the signature changed.
    virtual void describe(StateTree& out) const {
        out.name = to_string(m_id);
    }

protected:
    Context& m_ctx;

private:
    StateID m_id;
};

} // namespace everest::slac::evse
