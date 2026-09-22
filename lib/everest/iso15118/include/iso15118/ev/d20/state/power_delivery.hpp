// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include "../states.hpp"
#include <iso15118/ev/d20/der/sae_negotiation.hpp>
#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/message/power_delivery.hpp>

namespace iso15118::ev::d20::state {

struct PowerDelivery : public StateBase {
public:
    struct Start {
        // AC_DER_SAE only: what the first charge loop request echoes.
        der::SaeNegotiation sae{};
    };
    // A Stop always says why, for SessionStop to answer; there is no default reason.
    struct Stop {
        explicit Stop(session::StopReason stop_reason) : reason(stop_reason) {
        }
        session::StopReason reason;
    };
    struct Standby {};
    struct ScheduleRenegotiation {};
    using ChargeProgress = std::variant<Start, Stop, Standby, ScheduleRenegotiation>;

    PowerDelivery(Context& ctx, ChargeProgress charge_progress) :
        StateBase(ctx, StateID::PowerDelivery), m_charge_progress(charge_progress) {
    }

    static message_20::PowerDeliveryRequest make_request(message_20::datatypes::Progress charge_progress,
                                                         std::optional<message_20::datatypes::PowerProfile> profile);

    Result feed(Event) final;

private:
    ChargeProgress m_charge_progress;
};

// Sends the PowerDeliveryReq for `charge_progress` and creates the PowerDelivery state awaiting it.
BasePointerType to_power_delivery(Context& ctx, PowerDelivery::ChargeProgress charge_progress);

} // namespace iso15118::ev::d20::state
