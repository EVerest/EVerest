// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <iso15118/ev/d20/der/sae_negotiation.hpp>
#include <iso15118/ev/d20/der/sae_profile.hpp>
#include <iso15118/ev/d20/der/sae_validation.hpp>
#include <iso15118/ev/d20/energy_request.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AC_DER_SAE_ChargeLoop : public StateBase {
public:
    // What AC_DER_SAE_ChargeParameterDiscovery negotiated, echoed by the first request.
    AC_DER_SAE_ChargeLoop(Context& ctx, der::SaeNegotiation negotiation) :
        StateBase(ctx, StateID::AC_DER_SAE_ChargeLoop), negotiation_(negotiation) {
    }

    static message_20::DER_SAE_AC_ChargeLoopRequest
    make_request(const der::SaeInverterProfile& profile, const params::AcChargeParams& params,
                 message_20::datatypes::AcConnector connector, std::uint32_t enabled_modes, bool permit_service,
                 std::uint64_t settings_update_time, const EnergyRequest& energy);
    // Each round's request, read from the live params and echoing `negotiation`.
    static message_20::DER_SAE_AC_ChargeLoopRequest make_request(const Context& ctx,
                                                                 const der::SaeNegotiation& negotiation);

    void enter() final;
    Result feed(Event) final;

private:
    der::SaeNegotiation negotiation_;
    d20::der::DerControlProblems last_problems_;
};

} // namespace iso15118::ev::d20::state
