// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <utility>

namespace iso15118::ev {

// Shared by every EV protocol generation. The Context and StateID types differ per generation.

enum class Event {
    RESET,
    V2GTP_MESSAGE,
    CONTROL_MESSAGE,

    // internal events
    FAILED,
};

// What a state did with the event, declared rather than inferred. Session::feed_fsm() verifies it:
// a consumed response must leave a request, a stopped session, a successor whose enter() emits, or a handover.
enum class Disposition {
    Ignored,       // the event was not this state's to handle; nothing was consumed
    Awaiting,      // a request was emitted; the session now waits for its response
    Stopping,      // the session is being torn down
    Transitioning, // control passes to new_state, whose enter() drives the next step
    Handover,      // SAP negotiated another protocol generation; the Session switches engines
};

// Returns the violated expectation, or nullptr when the declared disposition matches what happened.
// `consumed` is true when the feed pulled a response (Event::V2GTP_MESSAGE).
const char* disposition_violation(Disposition d, bool consumed, bool has_request, bool session_stopped,
                                  bool transitioned, bool handover = false);

template <typename StateBaseT> struct BasicResult {
    using BasePointerType = std::unique_ptr<StateBaseT>;

    // Transition. The successor's enter() is responsible for emitting.
    BasicResult(BasePointerType result_state) :
        unhandled(false), new_state(std::move(result_state)), output(Disposition::Transitioning) {
    }

    static BasicResult ignored() {
        return BasicResult{Disposition::Ignored};
    }

    // Pairs with a send_request() on this path.
    static BasicResult awaiting() {
        return BasicResult{Disposition::Awaiting};
    }

    // Pairs with a stop_session() on this path.
    static BasicResult stopping() {
        return BasicResult{Disposition::Stopping};
    }

    // Pairs with a negotiated protocol other than the running engine's.
    static BasicResult handover() {
        return BasicResult{Disposition::Handover};
    }

    // Derived from output: only Ignored leaves the event unhandled. The fsm engine reads `unhandled`
    // and forwards `output` in its FeedResult.
    bool unhandled;
    BasePointerType new_state{nullptr};
    Disposition output{Disposition::Ignored};

private:
    // Private: `return {}` would not say which outcome happened.
    BasicResult() : BasicResult(Disposition::Ignored) {
    }

    explicit BasicResult(Disposition d) : unhandled(d == Disposition::Ignored), output(d) {
    }
};

template <typename ContextT, typename StateIdT, typename Derived> struct BasicStateBase {
    using ContainerType = std::unique_ptr<Derived>;
    using EventType = Event;
    using ResultType = BasicResult<Derived>;

    BasicStateBase(ContextT& ctx, StateIdT id) : m_ctx(ctx), m_id(id){};

    virtual ~BasicStateBase() = default;

    StateIdT get_id() const {
        return m_id;
    }

    virtual void enter(){};
    virtual ResultType feed(Event) = 0;
    virtual void leave(){};

protected:
    ContextT& m_ctx;

private:
    StateIdT m_id;
};

} // namespace iso15118::ev
