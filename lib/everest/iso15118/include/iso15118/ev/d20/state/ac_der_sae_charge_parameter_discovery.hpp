// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/ev/d20/der/sae_negotiation.hpp>
#include <iso15118/ev/d20/der/sae_profile.hpp>
#include <iso15118/ev/d20/secc_clock.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AC_DER_SAE_ChargeParameterDiscovery : public StateBase {
public:
    explicit AC_DER_SAE_ChargeParameterDiscovery(Context& ctx) :
        StateBase(ctx, StateID::AC_DER_SAE_ChargeParameterDiscovery) {
    }

    static message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest
    make_request(const der::SaeInverterProfile& profile, const params::AcChargeParams& params,
                 message_20::datatypes::AcConnector connector, message_20::datatypes::Processing processing,
                 std::uint32_t enabled_modes, std::uint64_t update_time, const SeccClock& secc_clock);

    static message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest make_first_request(const Context& ctx);

    void enter() final;

    Result feed(Event) final;

private:
    // Rounds the SECC has answered. Saturates at cpd_rounds(), so the count never wraps the wire back to Ongoing.
    std::uint16_t m_rounds_answered{0};
    der::SaeNegotiation m_negotiation{};
};

} // namespace iso15118::ev::d20::state
