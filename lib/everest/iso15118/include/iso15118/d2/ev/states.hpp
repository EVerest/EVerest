// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "context.hpp"

namespace iso15118::d2::ev {

class Context;

enum class Event {
    RESET,
    SEND_REQUEST,
    V2GTP_MESSAGE,
    CONTROL_MESSAGE,
    TIMEOUT,

    // internal events
    FAILED,
};

enum class StateID {
    SessionSetup,
    ServiceDiscovery,
    PaymentServiceSelection,
    // Plug-and-Charge (Contract) branch, entered from PaymentServiceSelection.
    CertificateInstallation,
    PaymentDetails,
    Authorization,
    ChargeParameterDiscovery,
    PowerDelivery,
    SessionStop,
    // DC branch
    CableCheck,
    PreCharge,
    CurrentDemand,
    WeldingDetection,
    // AC branch
    ChargingStatus,
    // Plug-and-Charge signed metering (entered from the charge loop when ReceiptRequired is set).
    MeteringReceipt,
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
    virtual Result feed(Event) = 0;
    virtual void leave(){};

protected:
    Context& m_ctx;

private:
    StateID m_id;
};

} // namespace iso15118::d2::ev
