// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "context.hpp"

namespace iso15118::d2 {

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
    ServiceSelection,
    Identification,
    PaymentDetails,
    Authorization,
    ChargeParameterDiscovery,
    AcPowerDelivery,
    MeteringReceipt,
    SessionStop,
    CableCheck,
    PreChargeStart,
    PreCharge,
    DcChargeLoop,
    PostCharge,
    WeldingDetection,
    AcChargeLoopStart,
    AcChargeLoop,
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

    // Consumes a V2GTP message once and applies what holds in every state -- the SessionID [V2G2-460].
    Result feed(Event ev);

    virtual void leave(){};

protected:
    // Control events and timeouts; states that react to neither need not override it.
    virtual Result on_event(Event) {
        return {};
    }

    // The message is already consumed, so a state cannot hand it on: every state is a wait node and the
    // action it takes is a function call, not a transition. Answering [V2G2-459] is its own business.
    virtual Result on_request(const message_2::Variant& received) = 0;

    Context& m_ctx;

private:
    StateID m_id;
};

} // namespace iso15118::d2
