// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <variant>

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>
#include <iso15118/ev/session/stop_reason.hpp>

namespace iso15118::ev::d2::state {

// Shared AC + DC state. Start -> DC CurrentDemand / AC ChargingStatus; Stop -> DC WeldingDetection /
// AC SessionStop; Renegotiate -> ChargeParameterDiscovery, which keeps the session alive.
struct PowerDelivery : public StateBase {
    struct Start {};
    // A Stop always says why, for SessionStop to answer; there is no default reason.
    struct Stop {
        explicit Stop(session::StopReason stop_reason) : reason(stop_reason) {
        }
        session::StopReason reason;
    };
    struct Renegotiate {};
    using Phase = std::variant<Start, Stop, Renegotiate>;

    PowerDelivery(Context& ctx, Phase phase) : StateBase(ctx, StateID::PowerDelivery), m_phase(phase) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    Phase m_phase;
};

// Sends the PowerDeliveryReq for `phase`, with the ChargingProfile on Start, and creates the
// PowerDelivery state awaiting it.
BasePointerType to_power_delivery(Context& ctx, PowerDelivery::Phase phase);

// Sends the first charge-loop request, CurrentDemand for DC or ChargingStatus for AC, and creates
// that state.
BasePointerType to_charge_loop(Context& ctx);

} // namespace iso15118::ev::d2::state
