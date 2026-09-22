// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <variant>

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/session/stop_reason.hpp>

namespace iso15118::ev::din::state {

// Start -> CurrentDemand (ReadyToChargeState=true); Stop -> WeldingDetection (ReadyToChargeState=false).
struct PowerDelivery : public StateBase {
    struct Start {};
    // A Stop always says why, for SessionStop to answer; there is no default reason.
    struct Stop {
        explicit Stop(session::StopReason stop_reason) : reason(stop_reason) {
        }
        session::StopReason reason;
    };
    using Phase = std::variant<Start, Stop>;

    PowerDelivery(Context& ctx, Phase phase) : StateBase(ctx, StateID::PowerDelivery), m_phase(phase) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    Phase m_phase;
};

// Sends the PowerDeliveryReq for `phase` and creates the PowerDelivery state awaiting it.
BasePointerType to_power_delivery(Context& ctx, PowerDelivery::Phase phase);

} // namespace iso15118::ev::din::state
