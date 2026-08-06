// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "context.hpp"

namespace iso15118::ev::d20 {

// FIXME(SL): Copied directly from d20/states.hpp. This should be refactored!

class Context;

enum class Event {
    RESET,
    V2GTP_MESSAGE,
    CONTROL_MESSAGE,

    // internal events
    FAILED,
};

enum class StateID {
    SupportedAppProtocol,
    SessionSetup,
    AuthorizationSetup,
    Authorization,
    ServiceDetail,
    ServiceDiscovery,
    ServiceSelection,
    AC_ChargeParameterDiscovery,
    AC_ChargeLoop,
    AC_DER_IEC_ChargeParameterDiscovery,
    AC_DER_IEC_ChargeLoop,
    DC_ChargeParameterDiscovery,
    DC_PreCharge,
    DC_ChargeLoop,
    DC_WeldingDetection,
    DC_CableCheck,
    PowerDelivery,
    ScheduleExchange,
    SessionStop
};

// What a state did with the event, declared rather than inferred.
//
// A state that consumes a response must leave exactly one thing behind: a new request, a stopped
// session, or a successor whose enter() emits. Do none of those and no timer stays armed, so the
// session hangs with nothing to show for it. Session::check_disposition() holds each of these to
// its word.
enum class Disposition {
    Ignored,       // the event was not this state's to handle; nothing was consumed
    Awaiting,      // a request was emitted; the session now waits for its response
    Stopping,      // the session is being torn down
    Transitioning, // control passes to new_state, whose enter() drives the next step
};

struct Result {
    // Transition. The successor's enter() is responsible for emitting.
    Result(BasePointerType result_state) :
        unhandled(false), new_state(std::move(result_state)), disposition(Disposition::Transitioning) {
    }

    static Result ignored() {
        return Result{Disposition::Ignored};
    }

    // Pairs with a send_request() on this path.
    static Result awaiting() {
        return Result{Disposition::Awaiting};
    }

    // Pairs with a stop_session() on this path, including the ones inside expect_response().
    static Result stopping() {
        return Result{Disposition::Stopping};
    }

    bool unhandled{true};
    BasePointerType new_state{nullptr};
    Disposition disposition{Disposition::Ignored};

private:
    // Deliberately private: `return {}` would say nothing about which of the four outcomes
    // happened, which is what let a silently hanging session look like ordinary code.
    constexpr Result() = default;

    explicit Result(Disposition d) : disposition(d) {
    }
};

struct StateBase {
    using ContainerType = BasePointerType;
    using EventType = Event;

    StateBase(Context& ctx, StateID id) : m_ctx(ctx), m_id(id){};

    virtual ~StateBase() = default;

    StateID get_id() const {
        return m_id;
    }

    virtual void enter(){};
    virtual Result feed(Event) = 0;
    virtual void leave(){};

protected:
    Context& m_ctx;

private:
    StateID m_id;
};

} // namespace iso15118::ev::d20
