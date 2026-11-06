// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d20/der/sae_negotiation.hpp>
#include <iso15118/ev/d20/energy_request.hpp>
#include <iso15118/message/schedule_exchange.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct ScheduleExchange : public StateBase {
public:
    // Every service but AC_DER_SAE, which throws std::logic_error here: it must carry its negotiation.
    explicit ScheduleExchange(Context& ctx);
    // AC_DER_SAE: what AC_DER_SAE_ChargeParameterDiscovery negotiated, for the charge loop.
    ScheduleExchange(Context& ctx, der::SaeNegotiation sae) : StateBase(ctx, StateID::ScheduleExchange), m_sae(sae) {
    }

    static message_20::ScheduleExchangeRequest make_request(message_20::datatypes::ControlMode control_mode,
                                                            const EnergyRequest& energy);
    // The request for the session's control mode and start_charging's energy request.
    static message_20::ScheduleExchangeRequest make_request(const Context& ctx);

    void enter() final;

    Result feed(Event) final;

private:
    der::SaeNegotiation m_sae;
};

} // namespace iso15118::ev::d20::state
