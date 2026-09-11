// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "context.hpp"

namespace iso15118::din {

class Context;

enum class Event {
    RESET,
    V2GTP_MESSAGE,
    CONTROL_MESSAGE,
    TIMEOUT,

    FAILED,
};

enum class StateID {
    SessionSetup,
    ServiceDiscovery,
    ServicePaymentSelection,
    ContractAuthentication,
    ChargeParameterDiscovery,
    CableCheck,
    PreChargeStart,
    PreCharge,
    CurrentDemandStart,
    CurrentDemand,
    WeldingDetection,
    SessionStop,
};

struct Result {
    constexpr Result() = default;
    Result(BasePointerType result_state) : unhandled(false), new_state(std::move(result_state)) {
    }

    bool unhandled{true};
    BasePointerType new_state{nullptr};
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

    // The one place a DIN request is consumed: it pulls the decoded request, reports its type and runs
    // the [V2G-DC-391] SessionID check before handing the message to the state. Non-virtual on purpose,
    // since an override could reintroduce a second consume point.
    Result feed(Event ev);

    virtual void leave(){};

protected:
    // Control events and timeouts; four of the states wait on nothing but the wire.
    virtual Result on_event(Event) {
        return {};
    }

    // The state's accepted set, ending in respond_sequence_error [V2G-DC-666].
    virtual Result on_request(const message_din::Variant& received) = 0;

    Context& m_ctx;

private:
    StateID m_id;
};

} // namespace iso15118::din
